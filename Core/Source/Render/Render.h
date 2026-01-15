#pragma once

#include "PerFrameData.h"
#include "VulkanMaterial.h"
#include "VulkanDevice.h"
#include "VulkanInstance.h"
#include "RenderSpecs.h"
#include "SkyBox.h"
#include "VulkanCommandBuffer.h"
#include "VulkanDescriptorPool.h"
#include "VulkanFramebuffer.h"
#include "VulkanImGui.h"
#include "VulkanMemoryAllocator.h"
#include "VulkanPhysicalDevice.h"
#include "VulkanPipeline.h"
#include "VulkanRenderPass.h"
#include "VulkanStorageBuffer.h"
#include "VulkanSurface.h"
#include "VulkanSwapchain.h"
#include "VulkanSync.h"
#include "Assets/MeshManager.h"

namespace YAEngine
{
  struct InstanceData
  {
    glm::mat4 model;
  };

  struct MeshBatch
  {
    MeshHandle* mesh;
    std::vector<InstanceData> instances;
  };

  struct Light
  {
    glm::vec3 position;
    float cutOff;
    glm::vec3 color;
    float outerCutOff;
  };

  class Render
  {
  public:
    void Init(GLFWwindow* window, const RenderSpecs &specs);
    void Destroy();
    void Resize(uint32_t width, uint32_t height);

    void Draw(Application* app);

    void DrawQuad();

    uint32_t AllocateInstanceData(uint32_t size)
    {
      return m_InstanceBuffer.Allocate(size);
    }

    float m_Gamma = 2.2f;
    float m_Exposure = 1.0f;

    int m_CurrentTexture = 0;

    static constexpr size_t MAX_LIGHTS = 2;
    struct
    {
      Light lights[MAX_LIGHTS];
      int lightsCount = MAX_LIGHTS;
    } m_Lights;

  private:

    void SetViewportAndScissor();

    void DrawMeshes(Application* app);
    void SetUpCamera(Application* app);
    void InitPipelines();

    uint32_t m_CurrentFrameIndex = 0;
    uint64_t m_GlobalFrameIndex = 0;
    uint32_t m_TAAIndex = 0;
    static uint32_t s_MaxFramesInFlight;
    bool b_Resized = false;

    PerFrameData m_PerFrameData {};
    SkyBox m_SkyBox;

    std::unordered_map<MeshHandle, MeshBatch> m_Batches;

    VulkanFramebuffer m_MainPassFrameBuffer;
    VulkanDescriptorSet m_SwapChainDescriptorSet;

    VulkanFramebuffer m_SSRFrameBuffer;
    std::vector<VulkanDescriptorSet> m_SSRPassDescriptorSets;

    std::array<VulkanFramebuffer, 2> m_HistoryFrameBuffers;
    VulkanDescriptorSet m_TAADescriptorSet;

    VulkanDescriptorSet m_InstanceDescriptorSet;
    VulkanStorageBuffer m_InstanceBuffer;

    VulkanDescriptorSet m_LightsDescriptorSet;
    VulkanStorageBuffer m_LightsBuffer;

    VulkanInstance m_VulkanInstance;
    VulkanPhysicalDevice m_PhysicalDevice;
    VulkanDevice m_Device;
    VulkanSurface m_Surface;
    VulkanSwapChain m_SwapChain;
    VulkanRenderPass m_MainRenderPass;
    VulkanRenderPass m_TAARenderPass;
    VulkanRenderPass m_SwapChainRenderPass;
    VulkanRenderPass m_SSRRenderPass;

    VulkanPipeline m_ForwardPipeline;
    VulkanPipeline m_ForwardPipelineNoShading;
    VulkanPipeline m_ForwardPipelineDoubleSided;
    VulkanPipeline m_ForwardPipelineInstanced;
    VulkanPipeline m_ForwardPipelineDoubleSidedInstanced;
    VulkanPipeline m_QuadPipeline;
    VulkanPipeline m_TAAPipeline;
    VulkanPipeline m_SSRPipeline;

    VulkanCommandBuffer m_CommandBuffer;
    VulkanSync m_Sync;
    VulkanMemoryAllocator m_Allocator;
    VulkanDescriptorPool m_DescriptorPool;
    VulkanMaterial m_DefaultMaterial {};
    VulkanImGui m_ImGUI {};
  };
}
