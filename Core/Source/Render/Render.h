#pragma once

#include "PerFrameData.h"
#include "RenderBackend.h"
#include "RenderGraph.h"
#include "VulkanCubicTexture.h"
#include "VulkanMaterial.h"
#include "SkyBox.h"
#include "VulkanComputePipeline.h"
#include "VulkanPipeline.h"
#include "VulkanStorageBuffer.h"
#include "VulkanUniformBuffer.h"

namespace YAEngine
{
  struct RenderStats
  {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t vertices = 0;
  };

  class Render
  {
  public:
    void Init(GLFWwindow* window, const RenderSpecs &specs);
    void Destroy();
    void Resize();
    void WaitIdle();

    void Draw(Application* app);

    void DrawQuad();

    uint32_t AllocateInstanceData(uint32_t size)
    {
      return m_InstanceBuffer.Allocate(size);
    }

    float& GetGamma() { return m_Gamma; }
    float& GetExposure() { return m_Exposure; }
    int GetDebugView() const { return m_CurrentTexture; }
    void SetDebugView(int view) { m_CurrentTexture = view; }
    bool& GetSSAOEnabled() { return b_SSAOEnabled; }
    bool& GetSSREnabled() { return b_SSREnabled; }
    bool& GetTAAEnabled() { return b_TAAEnabled; }

    const RenderStats& GetStats() const { return m_Stats; }

  private:

    float m_Gamma = 2.2f;
    float m_Exposure = 1.0f;
    int m_CurrentTexture = 0;
    bool b_SSAOEnabled = true;
    bool b_SSREnabled = true;
    bool b_TAAEnabled = true;

    RenderStats m_Stats {};

    void DrawMeshes(Application* app);
    void DrawMeshesDepthOnly(Application* app);
    void SetUpCamera(Application* app);
    void InitPipelines();

    void SetupRenderGraph(uint32_t width, uint32_t height);
    void CreateTAAFramebuffers();
    void ClearHistoryBuffers();
    void CreateHiZResources();
    void DestroyHiZResources();

    RenderBackend m_Backend;
    RenderGraph m_Graph;

    // Render graph resource handles
    RGHandle m_MainColor {};
    RGHandle m_MainNormals {};
    RGHandle m_MainDepth {};
    RGHandle m_MainMaterial {};
    RGHandle m_MainAlbedo {};
    RGHandle m_SSRColor {};
    RGHandle m_MainVelocity {};
    RGHandle m_HiZResource {};
    RGHandle m_SSAOColor {};
    RGHandle m_SSAOBlurIntermediate {};
    RGHandle m_SSAOBlurred {};
    RGHandle m_TAAHistory0 {};
    RGHandle m_TAAHistory1 {};

#ifdef YA_EDITOR
    RGHandle m_SceneColor {};
    uint32_t m_SceneComposePassIndex {};
    VkDescriptorSet m_SceneImGuiDescriptor {};
    uint32_t m_ViewportWidth = 0;
    uint32_t m_ViewportHeight = 0;
    uint32_t m_PendingViewportWidth = 0;
    uint32_t m_PendingViewportHeight = 0;
    void ResizeViewport();
#endif

    // Pass indices
    uint32_t m_DepthPrepassIndex {};
    uint32_t m_MainPassIndex {};
    uint32_t m_SSAOPassIndex {};
    uint32_t m_HiZPassIndex {};
    uint32_t m_SSAOBlurHPassIndex {};
    uint32_t m_SSAOBlurVPassIndex {};
    uint32_t m_SSRPassIndex {};
    uint32_t m_TAAPassIndex {};
    uint32_t m_SwapchainPassIndex {};

    // TAA external framebuffers (ping-pong)
    VulkanImage m_TAADepth;
    std::array<VkFramebuffer, 2> m_TAAFramebuffers {};

    uint64_t m_GlobalFrameIndex = 0;
    uint32_t m_TAAIndex = 0;
    bool b_Resized = false;

    glm::mat4 m_PrevView = glm::mat4(1.0f);
    glm::mat4 m_PrevProj = glm::mat4(1.0f);

    PerFrameData m_PerFrameData {};
    SkyBox m_SkyBox;

    std::vector<VulkanDescriptorSet> m_SwapChainDescriptorSets;
    std::vector<VulkanDescriptorSet> m_SSRPassDescriptorSets;
    std::vector<VulkanDescriptorSet> m_TAADescriptorSets;
    std::vector<VulkanDescriptorSet> m_SSAOPassDescriptorSets;
    std::vector<VulkanDescriptorSet> m_SSAOBlurHPassDescriptorSets;
    std::vector<VulkanDescriptorSet> m_SSAOBlurVPassDescriptorSets;

    VulkanDescriptorSet m_InstanceDescriptorSet;
    VulkanStorageBuffer m_InstanceBuffer;

    VulkanPipeline m_ForwardPipeline;
    VulkanPipeline m_ForwardPipelineNoShading;
    VulkanPipeline m_ForwardPipelineDoubleSided;
    VulkanPipeline m_ForwardPipelineInstanced;
    VulkanPipeline m_ForwardPipelineDoubleSidedInstanced;
    VulkanPipeline m_QuadPipeline;
    VulkanPipeline m_TAAPipeline;
    VulkanPipeline m_SSRPipeline;
    VulkanPipeline m_DepthPrepassPipeline;
    VulkanPipeline m_DepthPrepassPipelineDoubleSided;
    VulkanPipeline m_DepthPrepassPipelineInstanced;
    VulkanPipeline m_DepthPrepassPipelineDoubleSidedInstanced;
    VulkanPipeline m_SSAOPipeline;
    VulkanPipeline m_SSAOBlurHPipeline;
    VulkanPipeline m_SSAOBlurVPipeline;
    VulkanComputePipeline m_HiZPipeline;

    VulkanMaterial m_DefaultMaterial {};
    VulkanTexture m_NoneTexture;
    VulkanTexture m_SSAONoise;
    VulkanUniformBuffer m_SSAOKernelUBO;
    CubicTextureResources m_CubicResources;

    std::vector<VkImageView> m_HiZMipViews;
    std::vector<VulkanDescriptorSet> m_HiZDescriptorSets;

  public:
    const RenderContext& GetContext() const { return m_Backend.GetContext(); }
    const VulkanTexture& GetNoneTexture() const { return m_NoneTexture; }
    CubicTextureResources& GetCubicResources() { return m_CubicResources; }
    VkExtent2D GetSwapChainExtent() { return m_Backend.GetSwapChain().GetExt(); }

#ifdef YA_EDITOR
    void* GetSceneTextureID() const { return (void*)m_SceneImGuiDescriptor; }
    void CreateSceneImGuiDescriptor();
    void DestroySceneImGuiDescriptor();
    void RequestViewportResize(uint32_t w, uint32_t h);
    uint32_t GetViewportWidth() const { return m_ViewportWidth; }
    uint32_t GetViewportHeight() const { return m_ViewportHeight; }
#endif
  };
}
