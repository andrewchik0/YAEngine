#include "RenderBackend.h"

#include "Log.h"

#include <fstream>

namespace YAEngine
{
  void RenderBackend::Init(GLFWwindow* window, const RenderSpecs& specs)
  {
    m_MaxFramesInFlight = specs.maxFramesInFlight;

    m_VulkanInstance.Init(specs);
    m_Surface.Init(m_VulkanInstance.Get(), window);
    m_PhysicalDevice.Init(m_VulkanInstance.Get(), m_Surface.Get());
    m_Device.Init(m_VulkanInstance, m_PhysicalDevice, m_Surface.Get());
    m_Allocator.Init(m_VulkanInstance.Get(), m_Device.Get(), m_PhysicalDevice.Get());
    m_SwapChain.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), window, m_Allocator.Get());

    m_CommandBuffer.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), m_MaxFramesInFlight);
    m_Sync.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), m_MaxFramesInFlight, m_SwapChain.GetImageCount());
    m_CommandBuffer.SetGraphicsQueue(m_Sync.GetQueue());

    m_DescriptorPool.Init(m_Device.Get());

    // Load pipeline cache from disk if available
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
  }

  void RenderBackend::Destroy()
  {
    // Save pipeline cache to disk
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

    m_Sync.Destroy();
    m_ImGUI.Destroy();
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
