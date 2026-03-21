#pragma once

#include "PerFrameData.h"
#include "RenderBackend.h"
#include "RenderGraph.h"
#include "VulkanCubicTexture.h"
#include "VulkanMaterial.h"
#include "SkyBox.h"
#include "VulkanPipeline.h"
#include "VulkanStorageBuffer.h"
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

    void SetupRenderGraph(uint32_t width, uint32_t height);
    void CreateTAAFramebuffers();
    void ClearHistoryBuffers();

    RenderBackend m_Backend;
    RenderGraph m_Graph;

    // Render graph resource handles
    RGHandle m_MainColor {};
    RGHandle m_MainNormals {};
    RGHandle m_MainDepth {};
    RGHandle m_SSRColor {};
    RGHandle m_TAAHistory0 {};
    RGHandle m_TAAHistory1 {};

    // Pass indices
    uint32_t m_MainPassIndex {};
    uint32_t m_SSRPassIndex {};
    uint32_t m_TAAPassIndex {};
    uint32_t m_SwapchainPassIndex {};

    // TAA external framebuffers (ping-pong)
    VulkanImage m_TAADepth;
    std::array<VkFramebuffer, 2> m_TAAFramebuffers {};

    uint64_t m_GlobalFrameIndex = 0;
    uint32_t m_TAAIndex = 0;
    bool b_Resized = false;
    Application* m_CurrentApp = nullptr;

    PerFrameData m_PerFrameData {};
    SkyBox m_SkyBox;

    std::unordered_map<MeshHandle, MeshBatch> m_Batches;

    VulkanDescriptorSet m_SwapChainDescriptorSet;
    std::vector<VulkanDescriptorSet> m_SSRPassDescriptorSets;
    VulkanDescriptorSet m_TAADescriptorSet;

    VulkanDescriptorSet m_InstanceDescriptorSet;
    VulkanStorageBuffer m_InstanceBuffer;

    VulkanDescriptorSet m_LightsDescriptorSet;
    VulkanStorageBuffer m_LightsBuffer;

    VulkanPipeline m_ForwardPipeline;
    VulkanPipeline m_ForwardPipelineNoShading;
    VulkanPipeline m_ForwardPipelineDoubleSided;
    VulkanPipeline m_ForwardPipelineInstanced;
    VulkanPipeline m_ForwardPipelineDoubleSidedInstanced;
    VulkanPipeline m_QuadPipeline;
    VulkanPipeline m_TAAPipeline;
    VulkanPipeline m_SSRPipeline;

    VulkanMaterial m_DefaultMaterial {};
    VulkanTexture m_NoneTexture;
    CubicTextureResources m_CubicResources;

  public:
    const RenderContext& GetContext() const { return m_Backend.GetContext(); }
    const VulkanTexture& GetNoneTexture() const { return m_NoneTexture; }
    CubicTextureResources& GetCubicResources() { return m_CubicResources; }
  };
}
