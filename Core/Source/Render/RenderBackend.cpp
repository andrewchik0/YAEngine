#include "RenderBackend.h"

#include "Log.h"

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
    m_Sync.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), m_SwapChain.GetImageCount());
    m_CommandBuffer.SetGraphicsQueue(m_Sync.GetQueue());

    m_DescriptorPool.Init(m_Device.Get());

    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
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
    auto result = m_Sync.WaitIdle(m_SwapChain.Get(), &imageIndex);
    if (!result)
      return std::nullopt;

    m_CommandBuffer.Set(m_CurrentFrameIndex);
    return imageIndex;
  }

  bool RenderBackend::EndFrame(uint32_t imageIndex, bool resized)
  {
    m_CommandBuffer.End(m_CurrentFrameIndex);
    auto result = m_Sync.Submit(m_CommandBuffer.GetCurrentBuffer(), m_SwapChain.Get(), imageIndex, resized);
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
