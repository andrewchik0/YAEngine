#pragma once

#include "AntialiasingMode.h"
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
#include "VulkanVertexBuffer.h"
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
    // Ceiling on G-buffer draws whose object motion can reach the velocity buffer;
    // draws past it fall back to camera-only motion instead of overrunning the buffer.
    static constexpr uint32_t MAX_PREV_WORLD_MATRICES = 32768;
    static constexpr uint32_t MAX_PARTICLES_PER_FRAME = 8192;
    // Slot in m_BloomHistorySrcSets that reads the DLSS output instead of a TAA history.
    static constexpr uint32_t BLOOM_SRC_DLSS = 2;
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
    bool& GetSSGIEnabled() { return b_SSGIEnabled; }
    float& GetSSGIRadius() { return m_SSGIRadius; }
    float& GetSSGIThickness() { return m_SSGIThickness; }
    float& GetSSGIIntensity() { return m_SSGIIntensity; }
    bool& GetSSREnabled() { return b_SSREnabled; }
    float& GetSSRIntensity() { return m_SSRIntensity; }
    // What the user picked. The mode rendering actually runs in can differ when the
    // selection needs hardware that is not there - see GetEffectiveAntialiasingMode.
    AntialiasingMode& GetAntialiasingMode() { return m_AntialiasingMode; }
    AntialiasingMode GetEffectiveAntialiasingMode() const { return m_EffectiveAntialiasingMode; }
    bool IsDLSSAvailable() const { return m_Backend.GetStreamline().IsDLSSAvailable(); }
    const std::string& GetDLSSUnavailableReason() const
    {
      return m_Backend.GetStreamline().GetUnavailableReason(StreamlineFeature::DLSS);
    }
    // What the scene is rasterized at, and what reaches the screen. Equal outside the
    // DLSS upscale modes.
    VkExtent2D GetRenderExtent() const { return m_Graph.GetExtent(); }
    VkExtent2D GetOutputExtent() const { return m_Graph.GetOutputExtent(); }
    float& GetTAAClampSigma() { return m_TAAClampSigma; }
    bool& GetShadowsEnabled() { return b_ShadowsEnabled; }
    bool& GetShadowLodEnabled() { return b_ShadowLodEnabled; }
    int* GetShadowCascadeLods() { return m_ShadowCascadeLods; }
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
    bool b_SSGIEnabled = false;
    // World-space gather radius of the SSGI march, in meters. Deliberately wider than
    // the AO radius: the diffuse bounce needs to reach lit surfaces several meters away,
    // while the contact occlusion integral keeps its own falloff at m_AORadius.
    float m_SSGIRadius = 3.0f;
    // World-space depth extent assumed behind every screen sample, in meters. The depth
    // buffer only knows front surfaces; this is how far each one occludes behind itself.
    float m_SSGIThickness = 0.3f;
    // Artistic multiplier on the screen-gathered irradiance only. The volume fallback
    // weight is untouched, so values above 1 break energy conservation knowingly.
    float m_SSGIIntensity = 1.0f;
    // Forces the reprojection validity to zero for one frame: set whenever the TAA
    // history was cleared or resized, so SSGI never gathers radiance from a cleared
    // or stale history image.
    bool b_SSGIInvalidatePending = true;
    bool b_SSREnabled = true;
    // Artistic multiplier on the SSR mask. Fresnel keeps dielectric reflections near 4%,
    // so values above 1 are the usual way to make them readable.
    float m_SSRIntensity = 1.0f;
    AntialiasingMode m_AntialiasingMode = AntialiasingMode::TAA;
    // Resolved once per frame from the selection and what the hardware can actually do.
    AntialiasingMode m_EffectiveAntialiasingMode = AntialiasingMode::TAA;
    bool b_DLSSFallbackWarned = false;
    // Mode and output size the current render extent was queried for. A mismatch is
    // what triggers the render-resolution half of the graph to be rebuilt.
    AntialiasingMode m_ResolutionMode = AntialiasingMode::TAA;
    VkExtent2D m_ResolutionOutputExtent {};
    // Modes whose optimal settings the driver refused, so the query is not repeated
    // every frame. Indexed by AntialiasingMode.
    std::array<bool, size_t(AntialiasingMode::Count)> m_DLSSModeRejected {};
    // Width of the variance clipping box in sigmas. Low values collapse the box on locally
    // uniform neighbourhoods and throw away converged history on sub-pixel geometry.
    float m_TAAClampSigma = 0.979f;
    bool b_ShadowsEnabled = true;
    // Distant cascades cover so much world per texel that a simplified silhouette is
    // indistinguishable from the original, so they draw a cheaper index stream.
    // Only the CSM tiles use this: spot and point tiles are small and fit their
    // caster tightly, so there is nothing to win there.
    bool b_ShadowLodEnabled = true;
    // Mesh LOD level each cascade submits. Only cascade 0 stays on the source mesh;
    // cascade 1 already draws LOD 1. That makes cascade 1 the tightest constraint on
    // the LOD 1 simplification error budget: it is the nearest slice the camera sees
    // simplified geometry in, and its texel is the smallest one a LOD 1 silhouette
    // error has to stay under before the shadow visibly leaves its caster.
    int m_ShadowCascadeLods[CSM_CASCADE_COUNT] = { 0, 1, 1, 2 };
    // False at startup and after any invalidation that bypasses the digests
    // (probe/volume bakes, a shadows-off stretch).
    bool b_ShadowAtlasContentValid = false;
    // Reason to charge the next redraw with when the digests cannot carry it.
    ShadowInvalidation m_ShadowCachePendingReason = ShadowInvalidation::None;
    // State of the last RENDERED (not skipped) frame, compared wholesale.
    uint64_t m_ShadowCachedIdentityDigest = 0;
    uint64_t m_ShadowCachedTransformDigest = 0;
    uint64_t m_ShadowCachedLightDigest = 0;
    uint64_t m_ShadowCachedSettingsDigest = 0;
    uint64_t m_ShadowCachedGeometryVersion = 0;
    uint32_t m_ShadowCachedSpotCount = 0;
    uint32_t m_ShadowCachedPointCount = 0;
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
    // Turns the selected mode into the one this frame can really run.
    void ResolveAntialiasingMode();
    void InitPipelines();

    // Per-frame-in-flight model SSBOs and indirect command buffers for the batched
    // shadow path, plus the descriptor sets that point at them.
    void CreateShadowIndirectResources();
    void DestroyShadowIndirectResources();

    // Set 3 bindings 5-9. Re-run after every volume atlas rebuild - the image
    // views change and stale ones would dangle.
    void WriteIrradianceVolumeDescriptors();

    void SetupRenderGraph(VkExtent2D renderExtent, VkExtent2D outputExtent);
    // Tears down and rebuilds everything sized against the graph. The only entry point
    // that moves either extent.
    void ResizeGraph(VkExtent2D renderExtent, VkExtent2D outputExtent);
    // Render extent DLSS wants for an output extent. Falls back to 1:1 and warns when
    // the driver refuses the mode.
    VkExtent2D ComputeRenderExtent(AntialiasingMode mode, VkExtent2D outputExtent);
    // Re-queries the render extent when the mode or the output size changed and resizes
    // the render-resolution half of the graph. Returns true when anything moved.
    bool UpdateResolutionForMode(VkExtent2D outputExtent);
    void RunDLSSEvaluate(VkCommandBuffer cmd, FrameContext& frame);
    // The image this frame's anti-aliasing resolved into: the DLSS output, or the TAA
    // history slot the resolve just filled.
    RGHandle GetResolvedColorHandle() const
    {
      if (IsDLSSMode(m_EffectiveAntialiasingMode))
        return m_DLSSOutput;

      return m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
    }
    // The previous frame's resolved image. DLSS keeps a single output image, which still
    // holds the previous frame everywhere the evaluate has not run yet.
    RGHandle GetPreviousResolvedColorHandle() const
    {
      if (IsDLSSMode(m_EffectiveAntialiasingMode))
        return m_DLSSOutput;

      return m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;
    }
    void CreateTAAFramebuffers();
    void ClearHistoryBuffers();
    void CreateHiZResources();
    void DestroyHiZResources();
    void InitGTAOStaticResources();
    void CreateGTAOResources();
    void DestroyGTAOResources();
    void UpdateGTAOConstants(uint32_t frameIndex);
    void CreateSSGIResources();
    void DestroySSGIResources();
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
    // SSGI resources. Radiance is last frame's TAA history reprojected to this frame,
    // validity in alpha; Working/Final carry screen irradiance rgb + fallback weight
    // in alpha; the bent targets hold the mean unoccluded direction, octahedral.
    RGHandle m_SSGIRadiance {};    // RGBA16F, GTAO_DEPTH_MIP_LEVELS mips
    RGHandle m_SSGIWorking {};     // RGBA16F: raw main pass output
    RGHandle m_SSGIFinal {};       // RGBA16F: denoised, consumed by deferred lighting
    RGHandle m_SSGIBentWorking {}; // R8G8_UNORM: octahedral bent normal, raw
    RGHandle m_SSGIBentFinal {};   // R8G8_UNORM: octahedral bent normal, denoised
    RGHandle m_TAAHistory0 {};
    RGHandle m_TAAHistory1 {};
    // RGBA16F at output resolution, written by slEvaluateFeature as a storage image and
    // read by everything the TAA history would otherwise feed. Persists across frames so
    // the SSGI prefilter can reproject the previous frame's stabilized image.
    RGHandle m_DLSSOutput {};

#ifdef YA_EDITOR
    RGHandle m_SceneColor {};
    // Scene depth resampled to output resolution. The gizmo passes draw into the
    // output-res m_SceneColor and a framebuffer cannot mix the two sizes.
    RGHandle m_ComposeDepth {};
    uint32_t m_SceneDepthUpscalePassIndex {};
    uint32_t m_SceneComposePassIndex {};
    uint32_t m_GizmoScenePassIndex {};
    uint32_t m_GizmoPassIndex {};
    VkDescriptorSet m_SceneImGuiDescriptor {};
    uint32_t m_ViewportWidth = 0;
    uint32_t m_ViewportHeight = 0;
    uint32_t m_PendingViewportWidth = 0;
    uint32_t m_PendingViewportHeight = 0;
    void ResizeViewport();
    PipelineHandle m_DepthCopyPipeline {};
    std::vector<VulkanDescriptorSet> m_DepthCopyDescriptorSets;
    GizmoRenderer m_GizmoRenderer;
    bool b_GizmosEnabled = true;
    bool b_ProbeVolumesVisible = true;
    bool b_IrradianceVolumesVisible = true;
    bool b_VolumeNodesVisible = false;
    bool b_VolumeInvalidNodesVisible = false;
    VolumeNodeColorMode m_VolumeNodeColorMode = VolumeNodeColorMode::Irradiance;
    bool b_CollidersVisible = false;
    bool b_CameraFrustumsVisible = true;
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
    uint32_t m_SSGIRadiancePrefilterPassIndex {};
    uint32_t m_GTAOPassIndex {};
    uint32_t m_HiZPassIndex {};
    uint32_t m_GTAODenoisePassIndex {};
    uint32_t m_LightCullPassIndex {};
    uint32_t m_DeferredLightingPassIndex {};
    uint32_t m_SSRPassIndex {};
    uint32_t m_BloomPassIndex {};
    uint32_t m_TAAPassIndex {};
    uint32_t m_DLSSEvaluatePassIndex {};
    uint32_t m_ForwardTransparentPassIndex {};
    uint32_t m_HistogramPassIndex {};
    uint32_t m_ExposureAdaptPassIndex {};
    uint32_t m_SwapchainPassIndex {};

    // TAA external framebuffers (ping-pong)
    VulkanImage m_TAADepth;
    std::array<VkFramebuffer, 2> m_TAAFramebuffers {};

    uint64_t m_GlobalFrameIndex = 0;
    uint32_t m_TAAIndex = 0;
    bool b_Resized = false;
    bool b_ResetTAAPending = false;
    bool b_ResetAutoExposurePending = false;
    // Tells DLSS to drop its accumulated history: set at startup, on every resize and
    // whenever the effective mode changes.
    bool b_ResetDLSSPending = true;

    glm::mat4 m_PrevView = glm::mat4(1.0f);
    glm::mat4 m_PrevProj = glm::mat4(1.0f);
    // This frame's projection before the camera jitter is folded in.
    glm::mat4 m_UnjitteredProj = glm::mat4(1.0f);

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
    std::vector<VulkanDescriptorSet> m_SSGIPrefilterDescriptorSets;
    std::vector<VulkanDescriptorSet> m_GTAOPassDescriptorSets;
    std::vector<VulkanDescriptorSet> m_GTAODenoiseDescriptorSets;
    std::vector<VulkanDescriptorSet> m_LightCullInputDescriptorSets;
    std::vector<VulkanDescriptorSet> m_DeferredLightingDescriptorSets;
    std::vector<VulkanDescriptorSet> m_DeferredLightingLightDescriptorSets;
    std::vector<VulkanDescriptorSet> m_IBLDescriptorSets;

    VulkanDescriptorSet m_InstanceDescriptorSet;
    VulkanStorageBuffer m_InstanceBuffer;

    // One previous-frame world matrix per G-buffer draw, refilled from scratch every
    // frame. Kept per frame in flight because frame N-1 is still reading its copy.
    std::vector<VulkanDescriptorSet> m_PrevWorldDescriptorSets;
    std::vector<VulkanStorageBuffer> m_PrevWorldBuffers;

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
    PipelineHandle m_GTAOSSGIPipeline {};
    PipelineHandle m_SSGIRadiancePrefilterPipeline {};
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
    std::vector<VkImageView> m_SSGIRadianceMipViews;
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
      glm::mat4 prevWorldTransform;
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

    // Everything the indirect path needs about one shadow draw command that it can
    // resolve once per frame: where its geometry sits in the arena and where its
    // world matrices sit in this frame's model SSBO. Indexed in lockstep with
    // m_ShadowDrawCommands. The index range is the one thing that does vary between
    // tiles, so every LOD level is resolved up front and the tile picks one.
    struct ShadowIndirectRecord
    {
      // Offset of this caster's matrices inside one tile block of the model SSBO, and
      // the same offset inside m_ShadowModelWorlds.
      uint32_t modelBase = 0;
      uint32_t instanceCount = 0;
      int32_t vertexOffset = 0;
      MeshLodRange lods[MeshSimplifier::LOD_COUNT] {};
      // Which of the arena's two index buffers this mesh draws from. Commands are
      // grouped by it, because one bind serves a whole contiguous range.
      VkIndexType indexType = VK_INDEX_TYPE_UINT32;
      // Restores the mesh's quantized positions to its local space. Folded into the
      // model matrices below, so the shader never sees it.
      glm::vec3 dequantScale { 1.0f };
      glm::vec3 dequantBias { 0.0f };
      // False for alpha-test casters and for meshes the arena could not accept; both
      // fall through to the legacy per-draw loop inside the indirect path.
      bool batchable = false;
    };

    // The atlas is a fixed grid, so the worst case is one indirect range per cull mode
    // in every tile it can hold.
    static constexpr uint32_t MAX_SHADOW_TILES =
      CSM_CASCADE_COUNT + MAX_SHADOW_SPOTS + MAX_SHADOW_POINTS * 6;

    // One matrix per shadow-casting instance per atlas tile. Starts at the instance
    // buffer's own capacity, which is the largest instance count the rest of the
    // engine accepts.
    static constexpr VkDeviceSize SHADOW_MODEL_INITIAL_BYTES = MAX_INSTANCES * sizeof(glm::mat4);
    static constexpr VkDeviceSize SHADOW_MODEL_CAP_BYTES = 4 * SHADOW_MODEL_INITIAL_BYTES;
    static constexpr VkDeviceSize SHADOW_INDIRECT_INITIAL_BYTES = 1024ull * 1024;
    static constexpr VkDeviceSize SHADOW_INDIRECT_CAP_BYTES = 64ull * 1024 * 1024;

    std::vector<ShadowIndirectRecord> m_ShadowIndirectRecords;
    // World matrices of every batchable shadow instance, in model SSBO order. Kept in
    // cached memory because each tile reads the whole set back to premultiply its own
    // projection into its block of the write-combined SSBO.
    std::vector<glm::mat4> m_ShadowModelWorlds;
    // Indices into m_ShadowDrawCommands that the indirect path still has to draw one
    // by one: alpha-test casters, and opaque meshes the arena could not accept.
    std::vector<uint32_t> m_ShadowLegacyIndices;

    // Everything the shadow ordering compares, pulled out of DrawCommand so the sort
    // moves 16 bytes per element instead of the whole command.
    struct ShadowSortEntry
    {
      uint8_t key;
      uint32_t materialIndex;
      uint32_t meshIndex;
      uint32_t commandIndex;
    };
    std::vector<ShadowSortEntry> m_ShadowSortEntries;
    // Destination of the sort permutation, swapped with m_ShadowDrawCommands. Kept as
    // a member so the two buffers alternate roles instead of reallocating per frame.
    std::vector<DrawCommand> m_ShadowSortedCommands;

    // Caster bounds in m_ShadowDrawCommands order. The per-tile frustum cull repeats
    // up to MAX_SHADOW_TILES times a frame and reads only these 24 bytes.
    struct ShadowBounds
    {
      glm::vec3 min;
      glm::vec3 max;
    };
    std::vector<ShadowBounds> m_ShadowBounds;
    // maxFramesInFlight + 1 slots: the extra one belongs to the probe and irradiance
    // volume bakers, which render the atlas outside the frame loop.
    std::vector<VulkanBuffer> m_ShadowModelBuffers;
    std::vector<VulkanDescriptorSet> m_ShadowModelDescriptorSets;
    std::vector<VulkanBuffer> m_ShadowIndirectBuffers;
    // [0] cull on, [1] cull off. Kept apart from m_ShadowPipelines, which the
    // per-draw path still needs for alpha-test casters and non-resident meshes.
    PipelineHandle m_ShadowIndirectPipelines[2] {};
    // The same pair reading the quantized position stream, used wherever the device
    // supports the format.
    PipelineHandle m_ShadowIndirectQuantizedPipelines[2] {};
    bool b_ShadowModelOverflowReported = false;
    bool b_ShadowIndirectOverflowReported = false;
    bool b_ShadowIndirectCountSplitReported = false;

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
    bool& GetCameraFrustumsVisible() { return b_CameraFrustumsVisible; }
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
