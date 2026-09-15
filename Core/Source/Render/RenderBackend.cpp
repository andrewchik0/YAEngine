#include "RenderBackend.h"

#include "DebugMarker.h"
#include "RayTracingRequirements.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void RenderBackend::Init(GLFWwindow* window, const RenderSpecs& specs)
  {
    m_MaxFramesInFlight = specs.maxFramesInFlight;

    // Streamline loads its plugins inside slInit and that has to happen before the very
    // first Vulkan call, then it dictates what the instance and the device must carry.
    if (specs.enableDLSS)
    {
      // Both plugins are asked for at once: they are probed and reported independently, so
      // a device that has super resolution and not ray reconstruction still gets the first,
      // and an unsupported one contributes no extensions to the device we are about to make.
      m_Streamline.Init({ StreamlineFeature::DLSS, StreamlineFeature::RayReconstruction });
      m_Streamline.ApplyRequirements(m_Requirements);
    }
    else
    {
      YA_LOG_INFO("Render", "DLSS disabled by launch option, Streamline not initialized");
    }

    RegisterRayTracingRequirements(m_Requirements);

    m_VulkanInstance.Init(specs, m_Requirements);
    DebugMarker::Init(m_VulkanInstance.Get(), specs.debugUtils);
    m_Surface.Init(m_VulkanInstance.Get(), window);
    m_PhysicalDevice.Init(m_VulkanInstance.Get(), m_Surface.Get(), m_Requirements);

    {
      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(m_PhysicalDevice.Get(), &props);
      uint32_t major = VK_API_VERSION_MAJOR(props.apiVersion);
      uint32_t minor = VK_API_VERSION_MINOR(props.apiVersion);
      if (props.apiVersion < VK_API_VERSION_1_3)
      {
        YA_LOG_ERROR("Render", "Vulkan %u.%u found, but 1.3+ required", major, minor);
        throw std::runtime_error("Vulkan 1.3+ required");
      }
      YA_LOG_INFO("Render", "Vulkan %u.%u - %s", major, minor, props.deviceName);
    }

    m_Device.Init(m_VulkanInstance, m_PhysicalDevice, m_Surface.Get(), m_Requirements);

    {
      // SL is told where its own queues start, falling back to the engine queue when it
      // asked for none.
      VulkanQueueLocation graphicsForSL = m_Device.GetExtraGraphicsQueues().empty()
        ? m_Device.GetGraphicsQueueLocation() : m_Device.GetExtraGraphicsQueues().front();
      VulkanQueueLocation computeForSL = m_Device.GetExtraComputeQueues().empty()
        ? m_Device.GetGraphicsQueueLocation() : m_Device.GetExtraComputeQueues().front();

      m_Streamline.SetVulkanInfo(m_VulkanInstance.Get(), m_PhysicalDevice.Get(), m_Device.Get(),
        graphicsForSL.family, graphicsForSL.index, computeForSL.family, computeForSL.index);
    }

    m_Allocator.Init(m_VulkanInstance.Get(), m_Device.Get(), m_PhysicalDevice.Get(),
      m_Device.IsBufferDeviceAddressSupported());
    m_SwapChain.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), window, m_Allocator.Get());

    m_CommandBuffer.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), m_MaxFramesInFlight);
    m_Sync.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), m_MaxFramesInFlight, m_SwapChain.GetImageCount());
    m_CommandBuffer.SetGraphicsQueue(m_Sync.GetQueue());

    // The extension requests were resolved against the physical device above, so this
    // already knows whether acceleration structure descriptors are a legal type here.
    m_DescriptorPool.Init(m_Device.Get(),
      m_Requirements.IsDeviceExtensionEnabled(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME));

    std::vector<char> cacheData;
    {
      std::ifstream file("pipeline_cache.bin", std::ios::binary | std::ios::ate);
      if (file.is_open())
      {
        auto size = file.tellg();
        if (size > 0)
        {
          cacheData.resize(static_cast<size_t>(size));
          file.seekg(0);
          file.read(cacheData.data(), size);
          YA_LOG_INFO("Render", "Loaded pipeline cache from disk (%zu bytes)", cacheData.size());
        }
      }
    }

    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = cacheData.size();
    cacheInfo.pInitialData = cacheData.empty() ? nullptr : cacheData.data();
    if (vkCreatePipelineCache(m_Device.Get(), &cacheInfo, nullptr, &m_PipelineCache) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create pipeline cache");
      throw std::runtime_error("Failed to create pipeline cache!");
    }

    m_Context.device = m_Device.Get();
    m_Context.allocator = m_Allocator.Get();
    m_Context.graphicsQueue = m_Sync.GetQueue();
    m_Context.commandBuffer = &m_CommandBuffer;
    m_Context.descriptorPool = &m_DescriptorPool;
    m_Context.maxFramesInFlight = m_MaxFramesInFlight;
    m_Context.pipelineCache = m_PipelineCache;
    m_Context.layoutCache = &m_LayoutCache;
    m_Context.geometryArena = &m_GeometryArena;
    m_Context.bindlessTextures = &m_BindlessTextures;
    m_Context.depthClampSupported = m_Device.IsDepthClampSupported();
    m_Context.multiDrawIndirectSupported = m_Device.IsMultiDrawIndirectSupported();
    m_Context.drawIndirectFirstInstanceSupported = m_Device.IsDrawIndirectFirstInstanceSupported();
    m_Context.unorm16VertexSupported = m_Device.IsUnorm16VertexSupported();

    VkPhysicalDeviceProperties limitProps {};
    vkGetPhysicalDeviceProperties(m_PhysicalDevice.Get(), &limitProps);
    m_Context.maxImageDimension3D = limitProps.limits.maxImageDimension3D;
    m_Context.timestampPeriod = limitProps.limits.timestampPeriod;

    // timestampComputeAndGraphics only promises a non-zero bit count. The actual
    // count is per queue family, and the bits above it hold undefined garbage.
    auto families = VulkanPhysicalDevice::FindQueueFamilies(m_PhysicalDevice.Get(), m_Surface.Get());
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice.Get(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> familyProps(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice.Get(), &familyCount, familyProps.data());

    uint32_t graphicsFamily = families.graphicsFamily.value_or(0);
    m_Context.timestampValidBits = graphicsFamily < familyCount
      ? familyProps[graphicsFamily].timestampValidBits : 0;
    m_Context.timestampsSupported = limitProps.limits.timestampComputeAndGraphics == VK_TRUE
      && m_Context.timestampValidBits > 0;
    m_Context.maxDrawIndirectCount = limitProps.limits.maxDrawIndirectCount;

    YA_LOG_INFO("Render", "Indirect draw support: multiDrawIndirect=%d, drawIndirectFirstInstance=%d, maxDrawIndirectCount=%u",
      m_Context.multiDrawIndirectSupported ? 1 : 0,
      m_Context.drawIndirectFirstInstanceSupported ? 1 : 0,
      m_Context.maxDrawIndirectCount);

    if (!m_Context.multiDrawIndirectSupported || !m_Context.drawIndirectFirstInstanceSupported)
      YA_LOG_WARN("Render", "Indirect shadow batching is unavailable on this device, the legacy per-draw path is forced");

    YA_LOG_INFO("Render", "Quantized shadow position support: R16G16B16A16_UNORM as a vertex format=%d",
      m_Context.unorm16VertexSupported ? 1 : 0);

    if (!m_Context.unorm16VertexSupported)
      YA_LOG_WARN("Render", "Quantized shadow positions are unavailable on this device, the exact position stream is forced");

    ResolveRayTracingCapabilities(m_PhysicalDevice.Get(), m_Requirements, m_Context);

    if (m_Context.raytracingSupported && !m_Context.rayTracing.Load(m_Context.device))
    {
      // An enabled extension whose entry points the device will not hand back leaves
      // nothing callable, so the capability is withdrawn rather than crashed into.
      YA_LOG_WARN("Vulkan", "Ray tracing entry points could not be loaded, ray tracing is disabled");
      m_Context.raytracingSupported = false;
    }

    // Separate from the acceleration structure group on purpose: a device could grant the
    // structures without the pipeline extension, and everything built on the structures
    // alone - the TLAS - has to keep working there.
    if (m_Context.raytracingSupported && !m_Context.rayTracing.LoadPipeline(m_Context.device))
    {
      YA_LOG_WARN("Vulkan",
        "Ray tracing pipeline entry points could not be loaded, the path tracer and the ray traced bakers are unavailable");
    }

    if (m_Context.raytracingSupported)
    {
      YA_LOG_INFO("Vulkan",
        "Ray tracing entry points loaded, every mesh builds a bottom level acceleration structure (scratch alignment %u bytes)",
        m_Context.accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment);
    }

    if (m_Context.rayTracing.IsPipelineLoaded())
    {
      YA_LOG_INFO("Vulkan",
        "Ray tracing pipelines available: handle size %u, handle alignment %u, base alignment %u, max recursion %u",
        m_Context.rayTracingPipelineProperties.shaderGroupHandleSize,
        m_Context.rayTracingPipelineProperties.shaderGroupHandleAlignment,
        m_Context.rayTracingPipelineProperties.shaderGroupBaseAlignment,
        m_Context.rayTracingPipelineProperties.maxRayRecursionDepth);
    }

    m_GeometryArena.Init(m_Context);
    // After the capability resolve above, which is what decides whether the table exists at
    // all, and before Render::Init builds the first pipeline layout that names its set.
    m_BindlessTextures.Init(m_Context);
  }

  void RenderBackend::Destroy()
  {
    {
      size_t cacheSize = 0;
      vkGetPipelineCacheData(m_Device.Get(), m_PipelineCache, &cacheSize, nullptr);
      if (cacheSize > 0)
      {
        std::vector<char> cacheData(cacheSize);
        vkGetPipelineCacheData(m_Device.Get(), m_PipelineCache, &cacheSize, cacheData.data());
        std::ofstream file("pipeline_cache.bin", std::ios::binary);
        if (file.is_open())
        {
          file.write(cacheData.data(), static_cast<std::streamsize>(cacheSize));
          YA_LOG_INFO("Render", "Saved pipeline cache to disk (%zu bytes)", cacheSize);
        }
        else
        {
          YA_LOG_WARN("Render", "Failed to open pipeline_cache.bin for writing");
        }
      }
    }

    // Streamline still holds Vulkan resources of its own at this point.
    m_Streamline.Shutdown();

    m_Sync.Destroy();
    m_ImGUI.Destroy();
    // Before the layout cache and the allocator: the table holds an image of its own, and
    // its layout is one the cache owns.
    m_BindlessTextures.Destroy(m_Context);
    m_GeometryArena.Destroy(m_Context);
    m_CommandBuffer.Destroy();
    m_LayoutCache.Destroy(m_Device.Get());
    vkDestroyPipelineCache(m_Device.Get(), m_PipelineCache, nullptr);
    m_DescriptorPool.Destroy();
    m_SwapChain.Destroy();
    m_Allocator.Destroy();
    m_Device.Destroy();
    m_Surface.Destroy();
    m_VulkanInstance.Destroy();
  }

  std::optional<uint32_t> RenderBackend::BeginFrame()
  {
    uint32_t imageIndex;
    auto result = m_Sync.WaitIdle(m_SwapChain.Get(), m_CurrentFrameIndex, &imageIndex);
    if (!result)
      return std::nullopt;

    m_CommandBuffer.Set(m_CurrentFrameIndex);
    return imageIndex;
  }

  bool RenderBackend::EndFrame(uint32_t imageIndex, bool resized)
  {
    m_CommandBuffer.End(m_CurrentFrameIndex);
    auto result = m_Sync.Submit(m_CommandBuffer.GetCurrentBuffer(), m_SwapChain.Get(), m_CurrentFrameIndex, imageIndex, resized);
    m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % m_MaxFramesInFlight;
    return result;
  }

  void RenderBackend::InitImGui(GLFWwindow* window, VkRenderPass swapChainRenderPass)
  {
    m_ImGUI.Init(
      window,
      m_VulkanInstance.Get(),
      m_PhysicalDevice.Get(),
      m_Device.Get(),
      m_Sync.GetQueue(),
      m_SwapChain.GetImageCount(),
      VulkanPhysicalDevice::FindQueueFamilies(
        m_PhysicalDevice.Get(),
        m_Surface.Get()
      ).graphicsFamily.value(),
      swapChainRenderPass
    );
  }
}
