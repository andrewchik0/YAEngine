#pragma once

#include "PerFrameData.h"
#include "VulkanMaterial.h"
#include "VulkanDevice.h"
#include "VulkanInstance.h"
#include "RenderSpecs.h"
#include "SkyBox.h"
#include "VulkanCommandBuffer.h"
#include "VulkanDescriptorPool.h"
#include "VulkanImGui.h"
#include "VulkanMemoryAllocator.h"
#include "VulkanPhysicalDevice.h"
#include "VulkanPipeline.h"
#include "VulkanRenderPass.h"
#include "VulkanSurface.h"
#include "VulkanSwapchain.h"
#include "VulkanSync.h"

namespace YAEngine
{
  class Render
  {
  public:
    void Init(GLFWwindow* window, const RenderSpecs &specs);
    void Destroy();
    void Resize(uint32_t width, uint32_t height);

    void Draw(Application* app);

  private:

    void SetViewportAndScissor();

    void DrawMeshes(Application* app);
    void SetUpCamera(Application* app);

    uint32_t m_CurrentFrameIndex = 0;
    static uint32_t s_MaxFramesInFlight;
    bool b_Resized = false;

    PerFrameData m_PerFrameData {};
    SkyBox m_SkyBox;

    VulkanInstance m_VulkanInstance;
    VulkanPhysicalDevice m_PhysicalDevice;
    VulkanDevice m_Device;
    VulkanSurface m_Surface;
    VulkanSwapChain m_SwapChain;
    VulkanRenderPass m_RenderPass;
    VulkanPipeline m_ForwardPipeline;
    VulkanPipeline m_ForwardPipelineDoubleSided;
    VulkanCommandBuffer m_CommandBuffer;
    VulkanSync m_Sync;
    VulkanMemoryAllocator m_Allocator;
    VulkanDescriptorPool m_DescriptorPool;
    VulkanMaterial m_DefaultMaterial {};
    VulkanImGui m_ImGUI {};
  };
}
