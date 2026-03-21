#pragma once

#include "DescriptorLayoutCache.h"
#include "RenderContext.h"
#include "RenderSpecs.h"
#include "VulkanCommandBuffer.h"
#include "VulkanDescriptorPool.h"
#include "VulkanDevice.h"
#include "VulkanImGui.h"
#include "VulkanInstance.h"
#include "VulkanMemoryAllocator.h"
#include "VulkanPhysicalDevice.h"
#include "VulkanSurface.h"
#include "VulkanSwapchain.h"
#include "VulkanSync.h"

#include <optional>

namespace YAEngine
{
  class RenderBackend
  {
  public:

    void Init(GLFWwindow* window, const RenderSpecs& specs);
    void Destroy();

    std::optional<uint32_t> BeginFrame();
    bool EndFrame(uint32_t imageIndex, bool resized);

    void InitImGui(GLFWwindow* window, VkRenderPass swapChainRenderPass);

    const RenderContext& GetContext() const { return m_Context; }

    VulkanSwapChain& GetSwapChain() { return m_SwapChain; }
    VulkanCommandBuffer& GetCommandBuffer() { return m_CommandBuffer; }
    VulkanDescriptorPool& GetDescriptorPool() { return m_DescriptorPool; }
    DescriptorLayoutCache& GetLayoutCache() { return m_LayoutCache; }

    VkCommandBuffer GetCurrentCommandBuffer() { return m_CommandBuffer.GetCurrentBuffer(); }
    uint32_t GetCurrentFrameIndex() const { return m_CurrentFrameIndex; }
    uint32_t GetMaxFramesInFlight() const { return m_MaxFramesInFlight; }

  private:

    VulkanInstance m_VulkanInstance;
    VulkanPhysicalDevice m_PhysicalDevice;
    VulkanDevice m_Device;
    VulkanSurface m_Surface;
    VulkanSwapChain m_SwapChain;
    VulkanMemoryAllocator m_Allocator;
    VulkanCommandBuffer m_CommandBuffer;
    VulkanSync m_Sync;
    VulkanDescriptorPool m_DescriptorPool;
    VulkanImGui m_ImGUI {};
    DescriptorLayoutCache m_LayoutCache;

    RenderContext m_Context {};
    VkPipelineCache m_PipelineCache {};

    uint32_t m_CurrentFrameIndex = 0;
    uint32_t m_MaxFramesInFlight = 2;
  };
}
