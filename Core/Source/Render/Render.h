#pragma once

#include "FrameContext.h"
#include "FrameUniformBuffer.h"
#include "RenderBackend.h"
#include "RenderGraph.h"
#include "VulkanCubicTexture.h"
#include "VulkanMaterial.h"
#include "VulkanTerrainMaterial.h"
#include "PipelineCache.h"
#include "VulkanStorageBuffer.h"
#include "VulkanUniformBuffer.h"
#include "LightStorageBuffer.h"
#include "TileLightBuffer.h"
#include "ShadowManager.h"
#include "ReflectionProbeAtlas.h"
#include "ReflectionProbeStorageBuffer.h"
#include "IrradianceVolumeStorage.h"
#include "GTAOConstants.h"
#include "Assets/Handle.h"
#include "ParticleInstance.h"

#ifdef YA_EDITOR
#include <entt/fwd.hpp>
#include "GpuProfiler.h"
#include "Editor/GizmoRenderer.h"
#include "Editor/ShaderHotReload.h"
#include "OffscreenRenderer.h"
#include "ReflectionProbeBaker.h"
#include "IrradianceVolumeBaker.h"
#endif

namespace YAEngine
{
  struct RenderStats
  {
    uint32_t drawCalls = 0;
    uint32_t triangles = 0;
    uint32_t vertices = 0;
    // Shadow atlas draws are counted separately: they are not part of the visible
    // scene budget above, and collapsing them is what the indirect path is for.
    uint32_t shadowDrawCalls = 0;
    // Commands packed into the vkCmdDrawIndexedIndirect calls counted above. Zero
    // on the legacy path, which is how the two paths tell themselves apart in the UI.
    uint32_t shadowIndirectCommands = 0;
    // Triangles that survived frustum culling and reached an indirect command or a
    // legacy draw, split per atlas tile group. A point light sums its six faces,
    // because that is the granularity its cost is budgeted at.
    uint32_t shadowTrianglesPerCascade[CSM_CASCADE_COUNT] {};
    uint32_t shadowTrianglesPerSpot[MAX_SHADOW_SPOTS] {};
    uint32_t shadowTrianglesPerPoint[MAX_SHADOW_POINTS] {};
    uint32_t shadowTriangles = 0;
  };

#ifdef YA_EDITOR
  // What the baked irradiance volume node gizmos encode in their color.
  // Indices must match the combo in RenderSettingsPanel.
  enum class VolumeNodeColorMode : uint8_t
  {
    Irradiance = 0, // L0 normalized against the peak of the volume
    Ringing = 1     // |L1| / L0, flags nodes whose L1 fit goes negative
  };

  // Outcome of one ID-buffer pick.
  struct PickResult
  {
    bool hit = false;      // false means nothing was rasterized into the clicked pixel
    uint32_t entityId = 0; // raw entt handle, only meaningful when hit is set
  };
#endif

  class Render
  {
  public:
    static constexpr uint32_t MAX_INSTANCES = 100000;
    static constexpr uint32_t MAX_PARTICLES_PER_FRAME = 8192;
    static constexpr uint32_t MAX_PARTICLE_BATCHES_PER_FRAME = 16;
    static constexpr int32_t DEBUG_VIEW_WIREFRAME = 7;
    static constexpr int MIN_PROBE_BOUNCES = 1;
    static constexpr int MAX_PROBE_BOUNCES = 4;
    static constexpr int MIN_VOLUME_BOUNCES = 1;
    static constexpr int MAX_VOLUME_BOUNCES = 4;

    void Init(GLFWwindow* window, const RenderSpecs &specs);
    void Destroy();
    void Resize();
    void WaitIdle();
    void ResetBoundState()
    {
      m_BoundSkybox = {};
      for (auto& set : m_IBLDescriptorSets)
      {
        set.WriteCombinedImageSampler(0,
          m_ProbeAtlas.GetIrradianceView(), m_ProbeAtlas.GetIrradianceSampler());
        set.WriteCombinedImageSampler(1,
          m_ProbeAtlas.GetPrefilterView(), m_ProbeAtlas.GetPrefilterSampler());
        set.WriteCombinedImageSampler(2, m_NoneTexture.GetView(), m_NoneTexture.GetSampler());
        set.WriteCombinedImageSampler(3, m_NoneCubeMap.GetView(), m_NoneCubeMap.GetSampler());
      }
      // Unlike the probe buffer, the volume atlas is not rebuilt from the per-frame
      // snapshot, so a teardown has to clear it explicitly - otherwise the next scene
      // is lit, and baked, by the previous one's volumes.
      m_VolumeStorage.Reset(m_Backend.GetContext());
      m_VolumeUploadDirty = m_Backend.GetContext().maxFramesInFlight;
      WriteIrradianceVolumeDescriptors();
      m_InstanceBuffer.ResetAllocator();
    }

    void Draw(FrameContext& frame);

    void DrawQuad(VkCommandBuffer cmd);
    void DrawMeshes(VkCommandBuffer cmd, uint32_t frameIndex, FrameContext& frame,
      VkDescriptorSet frameUBOOverride = VK_NULL_HANDLE);
    void DrawMeshesDepthOnly(VkCommandBuffer cmd, uint32_t frameIndex, FrameContext& frame,
      VkDescriptorSet frameUBOOverride = VK_NULL_HANDLE);

    uint32_t AllocateInstanceData(uint32_t size)
    {
      return m_InstanceBuffer.Allocate(size);
    }

    void FreeInstanceData(uint32_t offset, uint32_t size)
    {
      m_InstanceBuffer.Free(offset, size);
    }

    float& GetGamma() { return m_Gamma; }
    float& GetExposure() { return m_Exposure; }
    int GetDebugView() const { return m_CurrentTexture; }
    void SetDebugView(int view) { m_CurrentTexture = view; }
    bool& GetAOEnabled() { return b_AOEnabled; }
    bool& GetAODenoiseEnabled() { return b_AODenoiseEnabled; }
    int& GetAOQualityLevel() { return m_AOQualityLevel; }
    float& GetAORadius() { return m_AORadius; }
    float& GetAOStrength() { return m_AOStrength; }
    float& GetAOSpecularStrength() { return m_AOSpecularStrength; }
    float& GetAOMultiBounce() { return m_AOMultiBounce; }
    float& GetAORadiusMultiplier() { return m_AORadiusMultiplier; }
    float& GetAOFalloffRange() { return m_AOFalloffRange; }
    float& GetAOSampleDistributionPower() { return m_AOSampleDistributionPower; }
    float& GetAOThinOccluderCompensation() { return m_AOThinOccluderCompensation; }
    float& GetAOFinalValuePower() { return m_AOFinalValuePower; }
    float& GetAODepthMipSamplingOffset() { return m_AODepthMipSamplingOffset; }
    bool& GetSSREnabled() { return b_SSREnabled; }
    float& GetSSRIntensity() { return m_SSRIntensity; }
    bool& GetTAAEnabled() { return b_TAAEnabled; }
    float& GetTAAClampSigma() { return m_TAAClampSigma; }
    bool& GetShadowsEnabled() { return b_ShadowsEnabled; }
    bool& GetShadowIndirectEnabled() { return b_ShadowIndirectEnabled; }
    int& GetTonemapMode() { return m_TonemapMode; }
    bool& GetAutoExposureEnabled() { return b_AutoExposureEnabled; }
    float& GetAdaptSpeedUp() { return m_AdaptSpeedUp; }
    float& GetAdaptSpeedDown() { return m_AdaptSpeedDown; }
    float& GetLowPercentile() { return m_LowPercentile; }
    float& GetHighPercentile() { return m_HighPercentile; }
    bool& GetBloomEnabled() { return b_BloomEnabled; }
    float& GetBloomIntensity() { return m_BloomIntensity; }
    float& GetBloomThreshold() { return m_BloomThreshold; }
    float& GetBloomSoftKnee() { return m_BloomSoftKnee; }
    bool& GetFogEnabled() { return b_FogEnabled; }
    float& GetFogDensity() { return m_FogDensity; }
    float& GetFogHeightFalloff() { return m_FogHeightFalloff; }
    glm::vec3& GetFogColor() { return m_FogColor; }
    float& GetFogMaxOpacity() { return m_FogMaxOpacity; }
    float& GetFogStartDistance() { return m_FogStartDistance; }
    int& GetProbeBounceCount() { return m_ProbeBounceCount; }
    int& GetVolumeBounceCount() { return m_VolumeBounceCount; }
    bool& GetIrradianceVolumesEnabled() { return b_IrradianceVolumesEnabled; }
    float& GetIrradianceNormalBias() { return m_IrradianceNormalBias; }

    // Rebuilds the volume atlas from freshly loaded assets and rewrites the IBL
    // descriptors. outSlots receives the atlas slot of every input volume.
    void UploadIrradianceVolumes(const std::vector<IrradianceVolumeFileData>& volumes,
      std::vector<uint32_t>& outSlots);

    const RenderStats& GetStats() const { return m_Stats; }

#ifdef YA_EDITOR
    // Arms a one-shot YA_LOG_INFO breakdown of the heaviest cascade. Nothing is
    // tracked per mesh until the next shadow pass sees this set.
    void RequestShadowBreakdownDump() { b_ShadowBreakdownPending = true; }
#endif

    void ResetTAAHistory() { b_ResetTAAPending = true; }
    void ResetAutoExposure() { b_ResetAutoExposurePending = true; }

    void SubmitParticles(std::span<const ParticleInstance> particles, TextureHandle texture);

  private:

    float m_Gamma = 2.2f;
    float m_Exposure = 1.0f;
    int m_CurrentTexture = 0;
    bool b_AOEnabled = true;
    bool b_AODenoiseEnabled = true;
    int m_AOQualityLevel = GTAO_QUALITY_HIGH;
    // World space radius of the occlusion sphere, in meters.
    float m_AORadius = 0.5f;
    float m_AOStrength = 1.0f;
    float m_AOSpecularStrength = 1.0f;
    float m_AOMultiBounce = 1.0f;
    // Auto-tuned XeGTAO heuristics. The defaults were fitted against a ray traced ground
    // truth, so they are a starting point rather than taste - see GTAOConstants.h.
    float m_AORadiusMultiplier = 1.457f;
    float m_AOFalloffRange = 0.615f;
    float m_AOSampleDistributionPower = 2.0f;
    float m_AOThinOccluderCompensation = 0.0f;
    float m_AOFinalValuePower = 2.2f;
    float m_AODepthMipSamplingOffset = 3.3f;
    bool b_SSREnabled = true;
    // Artistic multiplier on the SSR mask. Fresnel keeps dielectric reflections near 4%,
    // so values above 1 are the usual way to make them readable.
    float m_SSRIntensity = 1.0f;
    bool b_TAAEnabled = true;
    // Width of the variance clipping box in sigmas. Low values collapse the box on locally
    // uniform neighbourhoods and throw away converged history on sub-pixel geometry.
    float m_TAAClampSigma = 0.979f;
    bool b_ShadowsEnabled = true;
    // Batches opaque shadow casters into vkCmdDrawIndexedIndirect. Kept as a live
    // A/B switch rather than a build flag: the legacy per-draw path is the
    // reference the indirect output is compared against.
    bool b_ShadowIndirectEnabled = true;
    int m_TonemapMode = TONEMAP_AGX;
    bool b_AutoExposureEnabled = true;
    float m_AdaptSpeedUp = 2.0f;
    float m_AdaptSpeedDown = 1.0f;
    float m_LowPercentile = 0.1f;
    float m_HighPercentile = 0.98f;
    bool b_BloomEnabled = true;
    float m_BloomIntensity = 0.04f;
    float m_BloomThreshold = 1.0f;
    float m_BloomSoftKnee = 0.5f;
    bool b_FogEnabled = false;
    float m_FogDensity = 0.02f;
    float m_FogHeightFalloff = 0.1f;
    glm::vec3 m_FogColor = glm::vec3(0.7f, 0.75f, 0.8f);
    float m_FogMaxOpacity = 1.0f;
    float m_FogStartDistance = 10.0f;
    // Number of probe bake passes in a bake-all run. Every pass after the first
    // lets probes pick up the light their neighbours captured in the previous one.
    int m_ProbeBounceCount = 1;
    // Same idea for irradiance volumes. Two by default: bake time grows linearly
    // with the pass count and is the binding constraint here, while a third bounce
    // adds only a few percent of energy at typical albedo.
    int m_VolumeBounceCount = 2;
    bool b_IrradianceVolumesEnabled = true;
    // The volume UBO is not rebuilt from the per-frame snapshot, so it is uploaded
    // only when it actually changed - once per frame in flight after each change.
    bool b_VolumeUniformsEnabled = true;
    uint32_t m_VolumeUploadDirty = 0;
    // Distance the diffuse sample point is pushed along the surface normal before
    // it is looked up in a volume. First-line mitigation against light leaking
    // through walls thinner than the node spacing.
    float m_IrradianceNormalBias = 0.25f;
    float m_LastFrameTime = 0.0f;
    float m_DeltaTime = 0.0f;

    RenderStats m_Stats {};

    // Near plane of the cube faces rendered by OffscreenRenderer::RenderFace
    static constexpr float PROBE_SHADOW_NEAR_PLANE = 0.01f;

    // probeCenter != nullptr fits the CSM cascades around that point instead of the
    // camera frustum, so one atlas render covers all six faces of a probe bake.
    // probeRadius > 0 widens that fit to a box of capture points - an irradiance
    // volume renders the atlas once for the whole box, never once per node.
    void RenderShadowMaps(FrameContext& frame, VkCommandBuffer cmd,
      uint32_t frameIndex, const glm::vec3* probeCenter = nullptr,
      float probeRadius = 0.0f);
    void SetUpCamera(FrameContext& frame);
    void InitPipelines();

    // Per-frame-in-flight model SSBOs and indirect command buffers for the batched
    // shadow path, plus the descriptor sets that point at them.
    void CreateShadowIndirectResources();
    void DestroyShadowIndirectResources();

    // Set 3 bindings 5-9. Re-run after every volume atlas rebuild - the image
    // views change and stale ones would dangle.
    void WriteIrradianceVolumeDescriptors();

    void SetupRenderGraph(uint32_t width, uint32_t height);
    void CreateTAAFramebuffers();
    void ClearHistoryBuffers();
    void CreateHiZResources();
    void DestroyHiZResources();
    void InitGTAOStaticResources();
    void CreateGTAOResources();
    void DestroyGTAOResources();
    void UpdateGTAOConstants(uint32_t frameIndex);
    void CreateBloomResources();
    void DestroyBloomResources();

    // Debug frame capture, armed via the YA_CAPTURE_DIR environment variable
    void InitFrameCapture();
    void CaptureFrame();

    RenderBackend m_Backend;
    RenderGraph m_Graph;

#ifdef YA_EDITOR
    GpuProfiler m_GpuProfiler;
#endif

    std::string m_CaptureDir;
    int m_CaptureWarmup = 90;
    int m_CaptureFramesLeft = 16;
    int m_CaptureIndex = 0;
    bool b_CaptureManifestOpen = false;

    // Render graph resource handles - G-buffer
    RGHandle m_GBuffer0 {};       // R8G8B8A8_UNORM: albedo.rgb + metallic
    RGHandle m_GBuffer1 {};       // A2B10G10R10_UNORM: octNormal.xy + roughness + shadingModel
    RGHandle m_MainDepth {};
    RGHandle m_MainVelocity {};

    // Render graph resource handles - lighting & post
    RGHandle m_LitColor {};       // R16G16B16A16_SFLOAT: deferred lighting output
    RGHandle m_SSRColor {};
    RGHandle m_HiZResource {};
    RGHandle m_GTAODepth {};      // R16_SFLOAT, GTAO_DEPTH_MIP_LEVELS mips: linear view depth
    RGHandle m_GTAOWorkingAO {};  // R8_UNORM: raw visibility, scaled down for packing
    RGHandle m_GTAOEdges {};      // R8_UNORM: 2 bits of edge strength per neighbour
    RGHandle m_AOFinal {};        // R8_UNORM: denoised occlusion consumed by the lighting pass
    RGHandle m_TAAHistory0 {};
    RGHandle m_TAAHistory1 {};

#ifdef YA_EDITOR
    RGHandle m_SceneColor {};
    uint32_t m_SceneComposePassIndex {};
    uint32_t m_GizmoScenePassIndex {};
    uint32_t m_GizmoPassIndex {};
    VkDescriptorSet m_SceneImGuiDescriptor {};
    uint32_t m_ViewportWidth = 0;
    uint32_t m_ViewportHeight = 0;
    uint32_t m_PendingViewportWidth = 0;
    uint32_t m_PendingViewportHeight = 0;
    void ResizeViewport();
    GizmoRenderer m_GizmoRenderer;
    bool b_GizmosEnabled = true;
    bool b_ProbeVolumesVisible = true;
    bool b_IrradianceVolumesVisible = true;
    bool b_VolumeNodesVisible = false;
    bool b_VolumeInvalidNodesVisible = false;
    VolumeNodeColorMode m_VolumeNodeColorMode = VolumeNodeColorMode::Irradiance;
    bool b_CollidersVisible = false;
    bool b_HasSelectedEntity = false;
    glm::vec3 m_SelectedEntityPosition { 0.0f };
    GizmoMode m_GizmoMode = GizmoMode::Translate;
    uint32_t m_PendingInvalidateSlot = 0;

    // ID pick readback. The pass renders entity ids into the clicked pixel and the copy
    // lands in a per-frame slot, read only once that slot's fence has been waited on, so
    // the CPU never blocks on the GPU. Both run only on frames that serve a request.
    struct PickSlot
    {
      VulkanBuffer buffer;
      uint32_t pixelX = 0;
      uint32_t pixelY = 0;
      bool pending = false;
    };
    std::vector<PickSlot> m_PickSlots;
    RGHandle m_PickId {};
    uint32_t m_PickIdPassIndex {};
    uint32_t m_PickCopyPassIndex {};
    PipelineHandle m_PickPipelines[6] {};
    glm::vec2 m_PickRequestPos { 0.0f };
    bool b_PickRequested = false; // set by the editor, consumed at the start of the frame
    bool b_PickThisFrame = false; // this frame renders and copies the pick
    PickResult m_PickResult;
    bool b_PickResultReady = false;
    void CreatePickResources();
    void DestroyPickResources();
    void BeginPickFrame();
    void DrawPickIds(VkCommandBuffer cmd, uint32_t frameIndex, FrameContext& frame);
    void CopyPickId(VkCommandBuffer cmd);
    void LatchPickResult();

    // Backface mask pipelines for the irradiance volume node classification. Their
    // render pass belongs to BackfaceRatioSampler, which is built lazily when a bake
    // first needs it, so they cannot be registered from InitPipelines like the rest.
    // [0] non-instanced, [1] instanced.
    PipelineHandle m_BackfaceMaskPipelines[2] {};
    void InitBackfaceMaskPipelines(VkRenderPass renderPass);
    void DrawMeshesBackfaceMask(VkCommandBuffer cmd, FrameContext& frame,
      VkDescriptorSet frameUBO);
#endif

    // Pass indices
    uint32_t m_DepthPrepassIndex {};
    uint32_t m_GBufferPassIndex {};
    uint32_t m_GTAODepthPrefilterPassIndex {};
    uint32_t m_GTAOPassIndex {};
    uint32_t m_HiZPassIndex {};
    uint32_t m_GTAODenoisePassIndex {};
    uint32_t m_LightCullPassIndex {};
    uint32_t m_DeferredLightingPassIndex {};
    uint32_t m_SSRPassIndex {};
    uint32_t m_BloomPassIndex {};
    uint32_t m_TAAPassIndex {};
    uint32_t m_ForwardTransparentPassIndex {};
    uint32_t m_HistogramPassIndex {};
    uint32_t m_ExposureAdaptPassIndex {};
    uint32_t m_SwapchainPassIndex {};

    // TAA external framebuffers (ping-pong)
    VulkanImage m_TAADepth;
    std::array<VkFramebuffer, 2> m_TAAFramebuffers {};
    std::array<VkFramebuffer, 2> m_TransparentFramebuffers {};

    uint64_t m_GlobalFrameIndex = 0;
    uint32_t m_TAAIndex = 0;
    bool b_Resized = false;
    bool b_ResetTAAPending = false;
    bool b_ResetAutoExposurePending = false;

    glm::mat4 m_PrevView = glm::mat4(1.0f);
    glm::mat4 m_PrevProj = glm::mat4(1.0f);

    CubeMapHandle m_BoundSkybox {};

    FrameUniformBuffer m_FrameUniformBuffer {};
    LightStorageBuffer m_LightBuffer;
    TileLightBuffer m_TileLightBuffer;
    ShadowManager m_ShadowManager;
    ReflectionProbeAtlas m_ProbeAtlas;
    ReflectionProbeStorageBuffer m_ProbeBuffer;
    IrradianceVolumeStorage m_VolumeStorage;

    std::vector<VulkanDescriptorSet> m_SwapChainDescriptorSets;
    std::vector<VulkanDescriptorSet> m_SSRPassDescriptorSets;
    std::vector<VulkanDescriptorSet> m_TAADescriptorSets;
    std::vector<VulkanDescriptorSet> m_GTAOPrefilterDescriptorSets;
    std::vector<VulkanDescriptorSet> m_GTAOPassDescriptorSets;
    std::vector<VulkanDescriptorSet> m_GTAODenoiseDescriptorSets;
    std::vector<VulkanDescriptorSet> m_LightCullInputDescriptorSets;
    std::vector<VulkanDescriptorSet> m_DeferredLightingDescriptorSets;
    std::vector<VulkanDescriptorSet> m_DeferredLightingLightDescriptorSets;
    std::vector<VulkanDescriptorSet> m_IBLDescriptorSets;

    VulkanDescriptorSet m_InstanceDescriptorSet;
    VulkanStorageBuffer m_InstanceBuffer;

    PipelineCache m_PSOCache;
    PipelineHandle m_ForwardPipelines[8] {};
    PipelineHandle m_ForwardTransparentPipelines[4] {};
    PipelineHandle m_WireframePipelines[8] {};
    PipelineHandle m_WireframeTransparentPipelines[4] {};
    PipelineHandle m_DepthPipelines[8] {};
    PipelineHandle m_QuadPipeline {};
    PipelineHandle m_TAAPipeline {};
    PipelineHandle m_SSRPipeline {};
    PipelineHandle m_GTAOPrefilterPipeline {};
    PipelineHandle m_GTAOPipeline {};
    PipelineHandle m_GTAODenoisePipeline {};
    PipelineHandle m_HiZPipeline {};
    PipelineHandle m_LightCullPipeline {};
    PipelineHandle m_DeferredLightingPipeline {};
    PipelineHandle m_BloomDownsamplePipeline {};
    PipelineHandle m_BloomUpsamplePipeline {};
    PipelineHandle m_ExposureHistogramPipeline {};
    PipelineHandle m_ExposureAdaptPipeline {};
    PipelineHandle m_ShadowPipelines[8] {};

    VulkanMaterial m_DefaultMaterial {};
    VulkanTerrainMaterial m_TerrainMaterial {};
    VulkanTexture m_NoneTexture;
    VulkanImage m_NoneCubeMap;
    VulkanTexture m_GTAOHilbertLUT;
    std::vector<VulkanUniformBuffer> m_GTAOConstantsUBOs;
    std::vector<VkImageView> m_GTAODepthMipViews;
    CubicTextureResources m_CubicResources;

    std::vector<VkImageView> m_HiZMipViews;
    std::vector<VulkanDescriptorSet> m_HiZDescriptorSets;

    // Bloom
    VulkanImage m_BloomImage;
    std::vector<VkImageView> m_BloomMipViews;
    std::vector<VulkanDescriptorSet> m_BloomDownsampleDescriptorSets;
    std::vector<VulkanDescriptorSet> m_BloomUpsampleDescriptorSets;
    std::vector<VulkanDescriptorSet> m_BloomHistorySrcSets;
    VulkanDescriptorSet m_BloomReadDescriptorSet;

    // Auto exposure
    VulkanStorageBuffer m_HistogramBuffer;
    VulkanStorageBuffer m_ExposureBuffer;
    std::vector<VulkanDescriptorSet> m_HistogramPassDescriptorSets;
    VulkanDescriptorSet m_HistogramOutputDescriptorSet;
    std::vector<VulkanDescriptorSet> m_ExposureAdaptDescriptorSets;
    std::vector<VulkanDescriptorSet> m_ExposureReadDescriptorSets;

    struct DrawCommand
    {
      bool instanced;
      bool doubleSided;
      bool noShading;
      bool isTerrain;
      bool isAlphaTest;
      bool isTransparent;
      uint32_t materialIndex;
      uint32_t materialGeneration;
      uint32_t meshIndex;
      uint32_t meshGeneration;
      glm::mat4 worldTransform;
      glm::vec3 boundsMin { std::numeric_limits<float>::max() };
      glm::vec3 boundsMax { std::numeric_limits<float>::lowest() };
      std::vector<glm::mat4>* instanceData;
      uint32_t instanceOffset;
      float cameraDistanceSq = 0.0f;
#ifdef YA_EDITOR
      uint32_t entityId = 0;
#endif

      uint8_t SortKey() const
      {
        if (isTransparent) return 8 + (instanced ? 2 : 0) + (doubleSided ? 1 : 0);
        if (isAlphaTest) return instanced ? 7 : 6;
        if (isTerrain) return 5;
        if (noShading) return 4;
        return (instanced ? 2 : 0) + (doubleSided ? 1 : 0);
      }

      // Shadow-only ordering for the indirect path. Opaque casters collapse onto cull
      // mode alone, because that is the only pipeline state an indirect batch cannot
      // vary; alpha-test stays separated because it still draws one by one. Sorting on
      // this is what makes each indirect range a single contiguous run.
      uint8_t ShadowBatchKey() const
      {
        if (isAlphaTest) return instanced ? 3 : 2;
        return doubleSided ? 1 : 0;
      }
    };

    std::vector<DrawCommand> m_DrawCommands;
    std::vector<DrawCommand> m_DepthDrawCommands;
#ifdef YA_EDITOR
    std::vector<DrawCommand> m_PickDrawCommands;
    std::vector<DrawCommand> m_BackfaceDrawCommands;
#endif
    std::vector<DrawCommand> m_ShadowDrawCommands;
    std::vector<DrawCommand> m_TransparentDrawCommands;

    // Everything the indirect path needs about one shadow draw command that does not
    // change between atlas tiles: where its geometry sits in the arena and where its
    // world matrices sit in this frame's model SSBO. Resolved once per frame, indexed
    // in lockstep with m_ShadowDrawCommands.
    struct ShadowIndirectRecord
    {
      uint32_t modelBase = 0;
      uint32_t instanceCount = 0;
      uint32_t indexCount = 0;
      uint32_t firstIndex = 0;
      int32_t vertexOffset = 0;
      // False for alpha-test casters and for meshes the arena could not accept; both
      // fall through to the legacy per-draw loop inside the indirect path.
      bool batchable = false;
    };

    // The atlas is a fixed grid, so the worst case is one indirect range per cull mode
    // in every tile it can hold.
    static constexpr uint32_t MAX_SHADOW_TILES =
      CSM_CASCADE_COUNT + MAX_SHADOW_SPOTS + MAX_SHADOW_POINTS * 6;

    // One matrix per shadow-casting instance. Starts at the instance buffer's own
    // capacity, which is the largest instance count the rest of the engine accepts.
    static constexpr VkDeviceSize SHADOW_MODEL_INITIAL_BYTES = MAX_INSTANCES * sizeof(glm::mat4);
    static constexpr VkDeviceSize SHADOW_MODEL_CAP_BYTES = 4 * SHADOW_MODEL_INITIAL_BYTES;
    static constexpr VkDeviceSize SHADOW_INDIRECT_INITIAL_BYTES = 1024ull * 1024;
    static constexpr VkDeviceSize SHADOW_INDIRECT_CAP_BYTES = 64ull * 1024 * 1024;

    std::vector<ShadowIndirectRecord> m_ShadowIndirectRecords;
    // Indices into m_ShadowDrawCommands that the indirect path still has to draw one
    // by one: alpha-test casters, and opaque meshes the arena could not accept.
    std::vector<uint32_t> m_ShadowLegacyIndices;
    // maxFramesInFlight + 1 slots: the extra one belongs to the probe and irradiance
    // volume bakers, which render the atlas outside the frame loop.
    std::vector<VulkanBuffer> m_ShadowModelBuffers;
    std::vector<VulkanDescriptorSet> m_ShadowModelDescriptorSets;
    std::vector<VulkanBuffer> m_ShadowIndirectBuffers;
    // [0] cull on, [1] cull off. Kept apart from m_ShadowPipelines so the legacy path
    // stays intact and the toggle can switch between them without a rebuild.
    PipelineHandle m_ShadowIndirectPipelines[2] {};
    bool b_ShadowModelOverflowReported = false;
    bool b_ShadowIndirectOverflowReported = false;
    bool b_ShadowIndirectCountSplitReported = false;

#ifdef YA_EDITOR
    bool b_ShadowBreakdownPending = false;
    // Triangles each shadow draw command contributed to each cascade, laid out as
    // CSM_CASCADE_COUNT rows of m_ShadowDrawCommands.size(). Only sized while a dump
    // is armed, so the normal frame never pays for it.
    std::vector<uint32_t> m_ShadowBreakdownTriangles;

    // Logs the top meshes by submitted triangle count in the heaviest cascade and
    // disarms the request.
    void DumpShadowBreakdown();
#endif

    // Returns the slot index the shadow pass writes its per-frame buffers into.
    uint32_t GetShadowSlot(uint32_t frameIndex, bool isBake) const
    {
      return isBake ? m_Backend.GetContext().maxFramesInFlight : frameIndex;
    }

    // Grows a mapped buffer slot in place, returning true when the handle changed.
    // Only legal before anything is recorded into the slot this frame, at which point
    // its previous contents are already retired.
    static bool GrowMappedSlot(const RenderContext& ctx, VulkanBuffer& buffer,
      VkDeviceSize requiredBytes, VkDeviceSize capBytes, VkBufferUsageFlags usage,
      const char* name, bool& reportedFlag);

    struct ParticleBatch
    {
      uint32_t firstInstance;
      uint32_t count;
      TextureHandle texture;
    };

    PipelineHandle m_ParticlePipeline {};
    std::vector<VulkanStorageBuffer> m_ParticleInstanceBuffers;
    std::vector<VulkanDescriptorSet> m_ParticleDescriptorSets;
    std::vector<ParticleInstance> m_ParticleStage;
    std::vector<ParticleBatch> m_PendingParticleBatches;

    VulkanPipeline& GetForwardPipeline(const DrawCommand& dc);
    VulkanPipeline& GetForwardTransparentPipeline(const DrawCommand& dc);
    VulkanPipeline& GetWireframePipeline(const DrawCommand& dc);
    VulkanPipeline& GetWireframeTransparentPipeline(const DrawCommand& dc);
    VulkanPipeline& GetDepthPipeline(const DrawCommand& dc);
#ifdef YA_EDITOR
    VulkanPipeline& GetPickPipeline(const DrawCommand& dc);
#endif

    void DrawTransparent(VkCommandBuffer cmd, uint32_t frameIndex, FrameContext& frame);

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
    bool& GetGizmosEnabled() { return b_GizmosEnabled; }
    bool& GetProbeVolumesVisible() { return b_ProbeVolumesVisible; }
    bool& GetIrradianceVolumesVisible() { return b_IrradianceVolumesVisible; }
    bool& GetVolumeNodesVisible() { return b_VolumeNodesVisible; }
    bool& GetVolumeInvalidNodesVisible() { return b_VolumeInvalidNodesVisible; }
    VolumeNodeColorMode& GetVolumeNodeColorMode() { return m_VolumeNodeColorMode; }
    bool& GetCollidersVisible() { return b_CollidersVisible; }
    GizmoRenderer& GetGizmoRenderer() { return m_GizmoRenderer; }
    void SetSelectedEntityPosition(const glm::vec3& pos) { b_HasSelectedEntity = true; m_SelectedEntityPosition = pos; }
    void ClearSelectedEntity() { b_HasSelectedEntity = false; }
    // Asks for the entity under a normalized [0,1] viewport position. The ID pass renders
    // during the next Draw and the answer lands a few frames later.
    void RequestPick(const glm::vec2& normalizedPos);
    // True once per completed request; the result is dropped after it is taken.
    bool ConsumePickResult(PickResult& outResult)
    {
      if (!b_PickResultReady)
        return false;

      b_PickResultReady = false;
      outResult = m_PickResult;
      return true;
    }
    GizmoMode& GetGizmoMode() { return m_GizmoMode; }
    ShaderHotReload& GetShaderHotReload() { return m_ShaderHotReload; }
    void InitShaderHotReload(ThreadPool* threadPool);
    // writeToDisk == false updates the atlas only, for intermediate bounce passes
    void BakeProbe(entt::entity entity, class Scene& scene, class AssetManager& assets,
      bool writeToDisk = true);
    void BakeAllProbes(class Scene& scene, class AssetManager& assets);
    // writeToDisk == false skips the .yaiv write, for intermediate bounce passes.
    // outData receives the freshly baked volume when it is not null; the caller then
    // owns refreshing the atlas, which lets a bounce loop avoid a disk round-trip.
    bool BakeIrradianceVolume(entt::entity entity, class Scene& scene, class AssetManager& assets,
      bool writeToDisk = true, IrradianceVolumeFileData* outData = nullptr);
    void BakeAllIrradianceVolumes(class Scene& scene, class AssetManager& assets);
#endif

    ReflectionProbeAtlas& GetProbeAtlas() { return m_ProbeAtlas; }

  private:
#ifdef YA_EDITOR
    ShaderHotReload m_ShaderHotReload;
    ReflectionProbeBaker m_ProbeBaker;
    IrradianceVolumeBaker m_VolumeBaker;
#endif

    friend class OffscreenRenderer;
    friend class BackfaceRatioSampler;
  };
}
