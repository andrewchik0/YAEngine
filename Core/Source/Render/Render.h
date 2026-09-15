#pragma once

#include "AntialiasingMode.h"
#include "RenderPath.h"
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
#include "RayTracingMaterialTable.h"
#include "TlasBuilder.h"
#include "ReflectionProbeAtlas.h"
#include "ReflectionProbeStorageBuffer.h"
#include "IrradianceVolumeStorage.h"
#include "BakeLimits.h"
#include "GTAOConstants.h"
#include "PathTraceData.h"
#include "PathTraceGlass.h"
#include "Assets/Handle.h"
#include "ParticleInstance.h"
#include "Utils/FrameCaptureSpec.h"

#ifdef YA_EDITOR
#include <entt/fwd.hpp>
#include "GpuProfiler.h"
#include "Editor/GizmoRenderer.h"
#include "Editor/ShaderHotReload.h"
#include "OffscreenRenderer.h"
#include "ReflectionProbeBaker.h"
#include "RayTracedProbeBaker.h"
#include "Utils/IrradianceBrickLayout.h"
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

  enum class SwapchainReadbackState : uint8_t
  {
    Idle,
    Requested, // the next Draw copies the image it presents
    Recorded,  // the copy sits in a command buffer that is still in flight
    Ready      // ConsumeSwapchainReadback hands out the pixels or the error
  };

  // One presented editor frame, 8-bit RGBA in the swapchain's encoding.
  struct SwapchainReadback
  {
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    // Set instead of the pixels when the frame could not be copied
    std::string error;
  };

  struct IrradianceVolumeBakeAllResult
  {
    struct Volume
    {
      entt::entity entity {};
      // Baked, saved and uploaded by this call. A volume that failed before saving keeps its
      // earlier file, which the reload at the end still loads and marks baked; one saved but left
      // out of that upload is not baked.
      bool rebaked = false;
    };

    // False when no volume was attempted: the scene has no volumes, the ray traced baker is
    // unavailable, or the bake scene could not be built. The log names which.
    bool bakeSceneBuilt = false;
    // Every volume of the scene, filled once the bake scene is built.
    std::vector<Volume> volumes;
  };

  struct IrradianceVolumeBakeCounts
  {
    uint32_t uniqueNodes = 0;
    // Unique nodes that are not stitched; stitched ones are interpolated instead.
    uint32_t integratedNodes = 0;
    // Classified at their own position, so virtual offset nodes accepted afterwards still count here.
    uint32_t buriedNodes = 0;
    // Not buried but nearer to geometry than BakeLimits::VOLUME_MIN_PROBE_CLEARANCE_FRACTION allows.
    uint32_t tooCloseNodes = 0;
    // Neither buried nor too close, but with at least BakeLimits::VOLUME_MAX_ENCLOSED_FRACTION of its
    // rays hitting geometry within BakeLimits::VOLUME_ENCLOSURE_DISTANCE_FRACTION of its spacing.
    uint32_t enclosedNodes = 0;
    // Buried nodes whose nearest back face lies within their spacing, when the volume's virtual
    // offset is on. Accepted ones passed all three tests from their offset position and end valid;
    // rejected ones, those whose offset would exceed their spacing included, are dilated.
    uint32_t virtualOffsetCandidates = 0;
    uint32_t virtualOffsetAccepted = 0;
    uint32_t virtualOffsetRejected = 0;
    // The extra integration of the offset positions.
    double virtualOffsetSeconds = 0.0;
  };

  // Everything a placement preview depends on besides the scene geometry, compared exactly: a
  // preview whose fingerprint no longer matches its volume is stale.
  struct IrradianceVolumePlacementFingerprint
  {
    glm::vec3 center { 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 halfExtents { 0.0f };
    float minSpacing = 0.0f;
    float maxSpacing = 0.0f;
    float backfaceRatioThreshold = 0.0f;

    bool operator==(const IrradianceVolumePlacementFingerprint&) const = default;
  };

  struct IrradianceVolumePlacementPreview
  {
    IrradianceVolumePlacementFingerprint fingerprint;
    // Empty when the layout built. Otherwise why there is none: the preview could not run (no ray
    // traced baker, no bake scene) or the layout failed. Only the fingerprint, the query counters
    // and the timings are then set.
    std::string error;
    // What the Details panel, the brick gizmo and the bridge read. The layout itself is dropped
    // after validation: at the limits it takes hundreds of megabytes per volume.
    uint32_t minSpacingIndex = 0;
    uint32_t maxSpacingIndex = 0;
    std::vector<IrradianceBrick> bricks;
    std::array<IrradianceBrickLevelStats, IRRADIANCE_SPACINGS.size()> levelStats {};
    uint32_t indirectionCells = 0;
    uint32_t queryBatches = 0;
    uint32_t queryPoints = 0;
    // Validation runs only on a layout that built.
    bool validationPassed = false;
    std::string validationFailure;
    double totalSeconds = 0.0;
    // BuildIrradianceBrickLayout, its geometry queries included.
    double layoutSeconds = 0.0;
    double querySeconds = 0.0;
  };

  // Cost of a brick layout, sized with the brick format as planned, before it exists.
  struct IrradianceVolumePlacementEstimate
  {
    uint32_t bricks = 0;
    uint32_t uniqueNodes = 0;
    uint32_t stitchedNodes = 0;
    // Unique nodes that get integrated; stitched ones are interpolated instead.
    uint32_t bakedNodes = 0;
    uint32_t indirectionCells = 0;
    uint64_t runtimeBytes = 0;
    uint64_t diskBytes = 0;
    uint64_t primarySamples = 0;
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
    // And the one that reads the path tracer's accumulation image, which is what the
    // path tracing render path resolves into.
    static constexpr uint32_t BLOOM_SRC_PATHTRACE = 3;
    static constexpr uint32_t MAX_PARTICLE_BATCHES_PER_FRAME = 16;
    static constexpr int32_t DEBUG_VIEW_WIREFRAME = 7;
    static constexpr int MIN_PROBE_BOUNCES = 1;
    static constexpr int MAX_PROBE_BOUNCES = 4;
    // The volume bake traces pt_path.glsl's estimator, so it takes the path tracer's ranges.
    static constexpr int MIN_VOLUME_BOUNCES = PT_MIN_BOUNCES;
    static constexpr int MAX_VOLUME_BOUNCES = PT_MAX_BOUNCES;
    static constexpr int MIN_VOLUME_SAMPLES = int(BakeLimits::VOLUME_MIN_SAMPLES_PER_PROBE);
    static constexpr int MAX_VOLUME_SAMPLES = int(BakeLimits::RT_PROBE_MAX_SAMPLES_PER_PROBE);

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
      // The path tracer's set is rewritten every frame, so it takes the fallback the same
      // way the IBL sets just did rather than through a descriptor write of its own.
      m_SkyboxView = m_NoneCubeMap.GetView();
      m_SkyboxSampler = m_NoneCubeMap.GetSampler();
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
    // Which pipeline shades the frame, selected and resolved, on exactly the same rule as
    // the anti-aliasing mode above: the selection is what the scene stores and the user
    // sees, the effective value is what this frame can really run.
    RenderPath& GetRenderPath() { return m_RenderPath; }
    RenderPath GetEffectiveRenderPath() const { return m_EffectiveRenderPath; }
    bool IsPathTracingActive() const { return m_EffectiveRenderPath == RenderPath::PathTracing; }
    // Why a path cannot run on this device right now, or nullptr when it can. Drives the
    // disabled entry and its tooltip in the editor combo.
    const char* GetRenderPathUnavailableReason(RenderPath path) const
    {
      if (path != RenderPath::PathTracing)
        return nullptr;
      if (!IsRayTracingAvailable())
        return "Hardware ray tracing is unavailable on this device.";
      if (!IsPathTracerAvailable())
        return "The path tracing pipeline could not be built.\n"
          "It needs hardware ray tracing and bindless descriptors.";
      // Ray reconstruction is the path's resolve: one sample per pixel is what the tracer
      // produces, and without a denoiser there is nothing to display. The developer
      // override below is the deliberate way past this, not a second supported mode.
      if (!b_PathTraceDevResolve && !IsRayReconstructionAvailable())
        return "DLSS Ray Reconstruction is unavailable, and it is what turns the tracer's\n"
          "one sample per pixel into a frame. Tick Developer Resolve in the Developer\n"
          "panel's Path Tracer group to path trace without a denoiser.";

      return nullptr;
    }
    bool IsDLSSAvailable() const { return m_Backend.GetStreamline().IsDLSSAvailable(); }
    const std::string& GetDLSSUnavailableReason() const
    {
      return m_Backend.GetStreamline().GetUnavailableReason(StreamlineFeature::DLSS);
    }
    bool IsRayReconstructionAvailable() const
    {
      return m_Backend.GetStreamline().IsRayReconstructionAvailable();
    }
    const std::string& GetRayReconstructionUnavailableReason() const
    {
      return m_Backend.GetStreamline().GetUnavailableReason(StreamlineFeature::RayReconstruction);
    }
    // Developer escape hatch: path trace with NO denoiser, resolving out of the running
    // mean instead. It is what makes the raw sample and the converged reference reachable
    // while the render path owns the frame, and it is the only way into the path at all on
    // a device without ray reconstruction. Deliberately NOT serialized - a scene must never
    // come back missing its denoiser.
    bool IsPathTraceDevResolveEnabled() const { return b_PathTraceDevResolve; }
    void SetPathTraceDevResolve(bool enabled)
    {
      if (b_PathTraceDevResolve == enabled)
        return;

      b_PathTraceDevResolve = enabled;
      // The two resolves hold unrelated histories, and the extent the tracer runs at moves
      // with the override, so whichever one takes over starts from nothing.
      b_ResetDLSSPending = true;
      b_PathTraceResetPending = true;
    }
    // Bound straight to the panel widget; RunRayReconstructionEvaluate notices when the
    // preset moved and restarts the denoiser's history.
    RayReconstructionSettings& GetRayReconstructionSettings() { return m_RRSettings; }
    // What the scene is rasterized at, and what reaches the screen. Equal outside the
    // DLSS upscale modes.
    VkExtent2D GetRenderExtent() const { return m_Graph.GetExtent(); }
    VkExtent2D GetOutputExtent() const { return m_Graph.GetOutputExtent(); }
    float& GetTAAClampSigma() { return m_TAAClampSigma; }
    bool& GetShadowsEnabled() { return b_ShadowsEnabled; }
    bool& GetShadowLodEnabled() { return b_ShadowLodEnabled; }
    int* GetShadowCascadeLods() { return m_ShadowCascadeLods; }
    int& GetTonemapMode() { return m_TonemapMode; }
    float& GetTonemapPower() { return m_TonemapPower; }
    float& GetTonemapSaturation() { return m_TonemapSaturation; }
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
    int& GetVolumeSampleCount() { return m_VolumeSampleCount; }
    float& GetVolumeFireflyClamp() { return m_VolumeFireflyClamp; }
    bool& GetIrradianceVolumesEnabled() { return b_IrradianceVolumesEnabled; }
    float& GetIrradianceNormalBias() { return m_IrradianceNormalBias; }
    bool IsRayTracingAvailable() const { return m_Backend.GetContext().raytracingSupported; }
    // The path tracer needs more than the device flag: the entry points, the bindless table
    // and a pipeline that actually built. A valid handle means all three.
    bool IsPathTracerAvailable() const { return static_cast<bool>(m_PathTracePipeline); }
    // True while any view served by the path tracing pass is selected - the two radiance
    // views and the three energy diagnostics, all of which come out of the one dispatch.
    bool IsPathTraceView() const
    {
      return m_CurrentTexture == DEBUG_VIEW_PT_NOISY
        || m_CurrentTexture == DEBUG_VIEW_PT_REFERENCE
        || IsPathTraceDebugView();
    }
    // The energy diagnostics - which bounce contributed most and how much, the next event
    // estimation and environment totals - plus the non-finite locator. They replace the
    // radiance in the noisy image, so nothing may accumulate while one is on. The range has
    // to stay contiguous: GetPathTraceDebugMode maps it onto PT_DEBUG_* by offset.
    bool IsPathTraceDebugView() const
    {
      return m_CurrentTexture >= DEBUG_VIEW_PT_MAX_CONTRIB
        && m_CurrentTexture <= DEBUG_VIEW_PT_NONFINITE;
    }
    int GetPathTraceDebugMode() const
    {
      if (!IsPathTraceDebugView())
        return PT_DEBUG_OFF;

      return PT_DEBUG_MAX_CONTRIB + (m_CurrentTexture - DEBUG_VIEW_PT_MAX_CONTRIB);
    }
    // Serialized in the scene settings node since the path tracer became a render path the
    // user selects: they change what the frame looks like, not just what a diagnostic does.
    int& GetPathTraceMaxBounces() { return m_PathTraceMaxBounces; }
    float& GetPathTraceFireflyClamp() { return m_PathTraceFireflyClamp; }
    bool& GetPathTraceGlass() { return b_PathTraceGlass; }
    int& GetPathTraceMaxTransmissionDepth() { return m_PathTraceMaxTransmissionDepth; }
    PathTraceGlassOverflow& GetPathTraceGlassOverflow() { return m_PathTraceGlassOverflow; }
    PathTraceGlassHandling& GetPathTraceSecondaryGlass() { return m_PathTraceSecondaryGlass; }
    int& GetPathTraceGlassReflectionBounces() { return m_PathTraceGlassReflectionBounces; }
    PathTraceGlassHandling& GetPathTraceGlassReflectionGlass() { return m_PathTraceGlassReflectionGlass; }
    // How many samples the PT Reference image has averaged since its last reset. Zero means
    // the next frame rewrites it.
    int GetPathTraceSampleCount() const { return m_PathTraceSampleIndex; }
    void ResetPathTraceAccumulation() { b_PathTraceResetPending = true; }

    // FrameCapture. Draw services a request at the end of the frame it is already
    // recording, which is the only point where "the frame that just finished" is
    // unambiguous - the TAA and global frame indices have not advanced yet.
    // Refused, with a warning, while an earlier request has not been serviced yet. Drops a result
    // nobody consumed, so it cannot be mistaken for this request's.
    bool RequestCapture(const FrameCaptureRequest& request);
    bool IsCaptureRequestPending() const { return b_CaptureRequested; }
    // True once per serviced request; the result is dropped after it is taken.
    bool ConsumeCaptureResult(FrameCaptureResult& outResult);
    // Whether after=<name> names a pass of the graph, by name or alt name. The pass may still be
    // disabled by the time the capture frame comes.
    bool HasCapturePass(std::string_view name) const { return m_Graph.FindPass(name) != RG_INVALID_PASS; }
    void WriteCaptureSession(const FrameCaptureSessionInfo& info);
    // One log line per capturable target, for --capture-list-targets.
    void LogCaptureTargets();
    // Every graph resource with its capture format, and every alias with what it names in
    // the current state. The data behind LogCaptureTargets and the agent bridge listing.
    FrameCaptureTargetList GetCaptureTargets();
    uint64_t GetGlobalFrameIndex() const { return m_GlobalFrameIndex; }

    // Rebuilds the volume atlas from freshly loaded assets and rewrites the IBL
    // descriptors. outSlots receives the atlas slot of every input volume.
    void UploadIrradianceVolumes(const std::vector<IrradianceVolumeFileData>& volumes,
      std::vector<uint32_t>& outSlots);

    const RenderStats& GetStats() const { return m_Stats; }

    void ResetTAAHistory() { b_ResetTAAPending = true; }
    void ResetAutoExposure() { b_ResetAutoExposurePending = true; }

    void SubmitParticles(std::span<const ParticleInstance> particles, TextureHandle texture);

  private:

    // A grade on top of the sRGB encode the output format already applies, not the encode
    // itself - see the sceneColor comment in Render.Setup.cpp. 1.0 is the neutral value.
    float m_Gamma = 1.0f;
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
    RenderPath m_RenderPath = RenderPath::Raster;
    // Resolved once per frame the same way, by ResolveRenderPath.
    RenderPath m_EffectiveRenderPath = RenderPath::Raster;
    bool b_PathTracingFallbackWarned = false;
    // What the two resolves settled on LAST frame, captured before this frame's pair of
    // resolves overwrites them. The history reset flags are raised once, at the end of
    // ResolveRenderPath, because that is the only point where both are final: the path
    // resolve can still promote the mode, and comparing inside ResolveAntialiasingMode
    // would then see a change on every single frame the promotion happens.
    AntialiasingMode m_PreviousEffectiveAntialiasingMode = AntialiasingMode::TAA;
    RenderPath m_PreviousEffectiveRenderPath = RenderPath::Raster;
    // Resolves the path tracer out of its running mean instead of ray reconstruction. A
    // development toggle, never serialized - see SetPathTraceDevResolve.
    bool b_PathTraceDevResolve = false;
    // m_RRAppliedSettings is last frame's copy, so a preset change is spotted whatever
    // mutated it.
    RayReconstructionSettings m_RRSettings;
    RayReconstructionSettings m_RRAppliedSettings;
    // Mode, path, developer resolve and output size the current render extent was queried
    // for. A mismatch is what triggers the render-resolution half of the graph to be
    // rebuilt. The path is part of the key because it answers the question through ray
    // reconstruction's own optimal settings rather than super resolution's, and the
    // override because it forces the two extents equal - see ComputeRenderExtent.
    AntialiasingMode m_ResolutionMode = AntialiasingMode::TAA;
    RenderPath m_ResolutionPath = RenderPath::Raster;
    bool b_ResolutionDevResolve = false;
    VkExtent2D m_ResolutionOutputExtent {};
    // Modes whose optimal settings the driver refused, so the query is not repeated
    // every frame. Indexed by AntialiasingMode.
    std::array<bool, size_t(AntialiasingMode::Count)> m_DLSSModeRejected {};
    // The same memo for ray reconstruction, which answers through its own plugin and can
    // refuse a mode super resolution accepts. Kept apart so a refusal on one side never
    // takes the mode away from the other.
    std::array<bool, size_t(AntialiasingMode::Count)> m_RRModeRejected {};
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
    // (reflection probe bakes, a shadows-off stretch).
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
    // Grade on top of the tone mapping curve; 1.0 each leaves the curve alone.
    float m_TonemapPower = 1.0f;
    float m_TonemapSaturation = 1.0f;
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
    // Path bounces, samples per probe and firefly clamp (0 = off) of the ray traced
    // irradiance volume bake. A bake path starts at the probe ray's hit, which is bounce one of
    // a path tracer path - its bounce zero is the G-buffer vertex reading the probe - so 2 here
    // matches m_PathTraceMaxBounces' default of 3. The clamp defaults to 10, not 100: small,
    // very bright emitters near a node (string light bulbs) otherwise bake a colored blob, and
    // against the path traced reference 10 errs no more than 100, while 2 already biases dark.
    int m_VolumeBounceCount = 2;
    int m_VolumeSampleCount = int(BakeLimits::VOLUME_DEFAULT_SAMPLES_PER_PROBE);
    float m_VolumeFireflyClamp = 10.0f;
    bool b_IrradianceVolumesEnabled = true;
    // The volume UBO is not rebuilt from the per-frame snapshot, so it is uploaded
    // only when it actually changed - once per frame in flight after each change.
    bool b_VolumeUniformsEnabled = true;
    uint32_t m_VolumeUploadDirty = 0;
    // Distance the diffuse sample point is pushed along the surface normal before
    // it is looked up in a volume. First-line mitigation against light leaking
    // through walls thinner than the node spacing.
    float m_IrradianceNormalBias = 0.25f;
    // False until the path tracing pass has filled its two images at least once since the last
    // graph resize. They are never cleared by a pass, so the views have to fall back to the
    // final image until then rather than display whatever the allocation happened to hold.
    bool b_PathTraceOutputValid = false;
    // Bounce budget of one path, counting the G-buffer vertex as bounce zero. Three is where
    // an interior stops changing visibly per extra bounce.
    int m_PathTraceMaxBounces = 3;
    // Ceiling on what one bounce may add to the pixel, 0 = off. The only bias in the tracer,
    // and the reason a converged image is a reference rather than a ground truth.
    float m_PathTraceFireflyClamp = 10.0f;
    // A demonstration switch, too expensive for real use. Off, no material is glass to the path
    // tracer - every transparent surface is raster-only in the TLAS, the shaders take no glass
    // branch - and the forward transparent layer draws them over the traced image instead.
    bool b_PathTraceGlass = false;
    // Refractive events one path may take on top of its bounces, and the rest of the glass cost and
    // quality knobs - see PT_GLASS_* in PathTraceData.h.
    int m_PathTraceMaxTransmissionDepth = PT_DEFAULT_TRANSMISSION_DEPTH;
    PathTraceGlassOverflow m_PathTraceGlassOverflow = PathTraceGlassOverflow::Straight;
    PathTraceGlassHandling m_PathTraceSecondaryGlass = PathTraceGlassHandling::Straight;
    int m_PathTraceGlassReflectionBounces = PT_DEFAULT_GLASS_REFLECTION_BOUNCES;
    PathTraceGlassHandling m_PathTraceGlassReflectionGlass = PathTraceGlassHandling::Straight;
    // Index of the sample the next PT frame contributes to the running mean. Zero rewrites
    // the accumulation image, which is how a reset is expressed - nothing clears it.
    int m_PathTraceSampleIndex = 0;
    bool b_PathTraceResetPending = true;
    // Everything the accumulated image depends on, as of the last accumulated frame. Any
    // mismatch means the samples already in the buffer describe a different image and the
    // mean has to start over. The projection compared here is the UNJITTERED one: the
    // jitter is what gives the reference its anti-aliasing, so it must not reset anything.
    glm::mat4 m_PathTraceCachedView = glm::mat4(1.0f);
    glm::mat4 m_PathTraceCachedProj = glm::mat4(1.0f);
    uint64_t m_PathTraceCachedIdentityDigest = 0;
    uint64_t m_PathTraceCachedTransformDigest = 0;
    uint64_t m_PathTraceCachedLightDigest = 0;
    uint64_t m_PathTraceCachedLightBufferDigest = 0;
    uint64_t m_PathTraceCachedMaterialDigest = 0;
    uint64_t m_PathTraceCachedGlassKey = 0;
    bool b_PathTraceCachedTransparentLayer = false;
    float m_LastFrameTime = 0.0f;
    float m_DeltaTime = 0.0f;

    RenderStats m_Stats {};

    // Near plane of the cube faces rendered by OffscreenRenderer::RenderFace
    static constexpr float PROBE_SHADOW_NEAR_PLANE = 0.01f;

    // probeCenter != nullptr fits the CSM cascades around that point instead of the
    // camera frustum, so one atlas render covers all six faces of a probe bake.
    void RenderShadowMaps(FrameContext& frame, VkCommandBuffer cmd,
      uint32_t frameIndex, const glm::vec3* probeCenter = nullptr);
    void SetUpCamera(FrameContext& frame);
    // Every glass setting the path traced image depends on, packed so one compare spots a change.
    uint64_t GetPathTraceGlassKey() const
    {
      return uint64_t(uint32_t(m_PathTraceMaxTransmissionDepth))
        | (uint64_t(m_PathTraceGlassOverflow) << 32)
        | (uint64_t(m_PathTraceSecondaryGlass) << 36)
        | (uint64_t(m_PathTraceGlassReflectionGlass) << 40)
        | (uint64_t(b_PathTraceGlass) << 47)
        | (uint64_t(uint16_t(m_PathTraceGlassReflectionBounces)) << 48);
    }
    // Decided once per frame at the top of Draw: the path tracing render path has transparent
    // surfaces the tracer does not trace, or particles. The forward transparent layer draws them, and
    // the light culling and shadow atlas it shades with run, exactly then.
    bool b_PathTraceTransparencyActive = false;
    bool HasPathTraceRasterTransparency(FrameContext& frame) const;
    // The frames the forward transparent layer is drawn on, and the tracer lays it into its sample: every
    // frame ray reconstruction resolves, whose input carries it whether anything draws or not, and the
    // developer resolve's while anything does.
    bool IsPathTraceTransparentLayerDrawn() const
    {
      return b_PathTraceTransparencyActive || IsRayReconstructionResolve();
    }
    // Turns the selected mode into the one this frame can really run.
    void ResolveAntialiasingMode();
    // The same for the render path, and the last word on the effective anti-aliasing mode:
    // it can promote it to DLAA, because ray reconstruction is what resolves a traced frame
    // and it only exists inside the DLSS family. Runs after ResolveAntialiasingMode, and
    // raises the history reset flags for both.
    void ResolveRenderPath();
    void InitPipelines();

    // Per-frame-in-flight model SSBOs and indirect command buffers for the batched
    // shadow path, plus the descriptor sets that point at them.
    void CreateShadowIndirectResources();
    void DestroyShadowIndirectResources();

    // Set 3 bindings 5-10. Re-run after every volume atlas rebuild - the image
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
    // The path tracer's resolve: the noisy sample plus the four guides through ray
    // reconstruction, into the same DLSSOutput super resolution writes.
    void RunRayReconstructionEvaluate(VkCommandBuffer cmd, FrameContext& frame);
    // The reflection layer's RR instance (viewport 1).
    void RunRayReconstructionLayerEvaluate(VkCommandBuffer cmd, FrameContext& frame);
    // Everything both evaluates describe identically - the camera, the frame, the depth and
    // velocity tags and the reset flag - so the two conventions-heavy halves are written
    // once. Each caller then names its own scaling input and, for ray reconstruction, its
    // guides.
    DLSSEvaluateDesc MakeStreamlineEvaluateDesc(VkCommandBuffer cmd, FrameContext& frame,
      StreamlineFrameToken token);
    // One graph resource in the shape Streamline needs, since it cannot query a VkImage.
    // layout must be the layout the graph really left the image in.
    DLSSImage DescribeStreamlineImage(RGHandle handle, VkImageLayout layout);
    // True while ray reconstruction is what turns this frame into an image. ResolveRenderPath
    // already refuses the path outright when RR is unavailable, so the override is the only
    // thing left to test here.
    bool IsRayReconstructionResolve() const
    {
      return IsPathTracingActive() && !b_PathTraceDevResolve;
    }
    // The image this frame's anti-aliasing resolved into: the DLSS output, or the TAA
    // history slot the resolve just filled.
    //
    // The path tracer takes the DLSS output too, because ray reconstruction writes exactly
    // where super resolution would - denoise, upscale and anti-alias are one evaluate. The
    // developer resolve is the exception: with no denoiser the accumulation image is what
    // the frame comes out of, its running mean being the single noisy sample at index 0
    // whenever anything moves and the converged reference whenever nothing does.
    RGHandle GetResolvedColorHandle() const
    {
      if (m_EffectiveRenderPath == RenderPath::PathTracing)
        return IsRayReconstructionResolve() ? m_DLSSOutput : m_PathTraceAccum;

      if (IsDLSSMode(m_EffectiveAntialiasingMode))
        return m_DLSSOutput;

      return m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
    }
    // The previous frame's resolved image. Both DLSS resolves keep a single output image,
    // which still holds the previous frame everywhere the evaluate has not run yet; so does
    // the path tracer's accumulation image, which the trace has not touched that early
    // either. One image covers both roles in every case but the TAA ping-pong.
    RGHandle GetPreviousResolvedColorHandle() const
    {
      if (m_EffectiveRenderPath == RenderPath::PathTracing)
        return IsRayReconstructionResolve() || b_PathTraceTransparencyActive ? m_DLSSOutput : m_PathTraceAccum;

      if (IsDLSSMode(m_EffectiveAntialiasingMode))
        return m_DLSSOutput;

      return m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;
    }
    void CreateTAAFramebuffers();
    void ClearHistoryBuffers();
    // Blacks out the path tracer's three images. They are never cleared by a pass - the
    // sample index resets the mean by rewriting it - so a frame that resolves them, or
    // hands them to ray reconstruction, without the trace having run would otherwise work
    // off whatever the allocation held. Runs wherever they are (re)allocated, next to
    // ClearHistoryBuffers.
    void ClearPathTraceOutputs();
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

    // Points set 1 at this frame's TLAS, instance records and material records (bindings 0-2)
    // plus the path tracer's own resources from binding 3 up.
    void WritePathTraceDescriptors(uint32_t frameIndex);
    // The guide pass's set 1: the G-buffer in, the three ray reconstruction guides out.
    void WritePathTraceGuideDescriptors(uint32_t frameIndex);
    // The reflection layer's guide and composite sets.
    void WritePathTraceLayerGuideDescriptors(uint32_t frameIndex);
    void WriteRRLayerCompositeDescriptors(uint32_t frameIndex);
    // Decides whether this frame extends the reference image or starts it over, and advances
    // the sample index. Runs before the graph, because the pass reads the result as a push
    // constant.
    void UpdatePathTraceAccumulation(const FrameContext& frame);
    // The exact condition the path tracing pass runs under. Shared with the accumulation
    // counter, which may not advance on a frame that traced nothing: the mean would then
    // weight a later sample as if the missing ones were in the buffer. Only meaningful once
    // the TLAS build for this frame has been recorded, which precedes the graph.
    bool IsPathTracePassEnabled() const
    {
      return (IsPathTracingActive() || IsPathTraceView())
        && IsPathTracerAvailable()
        && m_TlasBuilder.IsValid(m_Backend.GetCurrentFrameIndex())
        && m_MaterialTable.IsValid(m_Backend.GetCurrentFrameIndex());
    }
    // Whether this frame extends the running mean. The render path always does - the mean
    // IS the image it presents - and in raster mode only the reference view does, which is
    // the one that displays it.
    bool IsPathTraceAccumulating() const
    {
      // A diagnostic frame contributes no radiance sample, so it must not advance the mean
      // either - the pass stores the diagnostic instead of the radiance.
      if (IsPathTraceDebugView())
        return false;

      return IsPathTracingActive() || m_CurrentTexture == DEBUG_VIEW_PT_REFERENCE;
    }

    void CaptureFrame();
    // A single-target alias to its handle, or the graph resource of that name.
    RGHandle ResolveCaptureAlias(std::string_view name) const;
    // Capture after a named pass. Draw arms the graph for the Execute of the frame that
    // services the request; the hook copies the pass outputs into staging buffers inside that
    // frame's own command buffer, and CaptureFrame reads them once the frame has finished.
    // Returns false when the request names no pass, or one the graph does not have.
    bool ArmCaptureAfterPass();
    void RecordCaptureAfterPass(VkCommandBuffer cmd);
    // Waits for the frame the copies were recorded into before freeing them, unless the caller
    // already waited for the device.
    void ReleaseCaptureStagedOutputs(bool deviceIdle);

    RenderBackend m_Backend;
    RenderGraph m_Graph;

#ifdef YA_EDITOR
    GpuProfiler m_GpuProfiler;
#endif

    FrameCaptureRequest m_CaptureRequest;
    FrameCaptureResult m_CaptureResult;
    bool b_CaptureRequested = false;
    bool b_CaptureResultReady = false;

    struct CaptureStagedOutput
    {
      RGHandle handle = RG_INVALID_HANDLE;
      // As recorded: a failed present resizes the graph before CaptureFrame reads the copy.
      VkExtent2D extent {};
      VulkanBuffer staging;
    };
    uint32_t m_CaptureAfterPassIndex = RG_INVALID_PASS;
    // Set by the hook, which is what tells a pass skipped this frame apart from one that ran.
    bool b_CaptureAfterPassRecorded = false;
    // The graph extents the copies were recorded at, for the manifest of the same reason.
    VkExtent2D m_CaptureAfterPassRenderExtent {};
    VkExtent2D m_CaptureAfterPassOutputExtent {};
    std::vector<CaptureStagedOutput> m_CaptureStagedOutputs;

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
    // The path tracer's outputs, both at render resolution. Noisy is RGBA16F and holds this
    // frame's single sample - radiance in rgb, primary hit distance in alpha, which is what
    // stage 5 hands ray reconstruction. Accumulation is RGBA32F because a running mean over
    // thousands of samples stops moving in fp16 long before it has converged; it persists
    // across frames and is never cleared, the sample index resets it instead.
    RGHandle m_PathTraceNoisy {};
    RGHandle m_PathTraceAccum {};
    // Guide buffers for ray reconstruction, all at render resolution and written by
    // pt_guides.comp from the first path vertex below while the path tracing render path is
    // effective. The two albedos are what the denoiser demodulates the radiance by; they are
    // RGBA16F because PSR scales them by a mirror chain's Fresnel, which bands in 8 bits on a
    // dark tint. Normal and roughness share one RGBA16F in the PACKED layout sl_dlss_d.h
    // documents - world normal in xyz, roughness in w.
    RGHandle m_PTDiffuseAlbedo {};
    RGHandle m_PTSpecularAlbedo {};
    RGHandle m_PTNormalRoughness {};
    // R16F, render resolution: the world-space length of the first bounce ray measured from
    // the primary surface, written by pt_main.rgen on the frames that bounce sampled the
    // specular lobe and zero otherwise. Ray reconstruction builds its specular motion vectors
    // out of it - see section 4.1.9 of NVIDIA's DLSS-RR guide.
    RGHandle m_PTHitDistance {};
    // RG16F, render resolution: specular motion vectors written by pt_main.rgen in exactly
    // MainVelocity's convention, since both tags share one sl::Constants::mvecScale.
    RGHandle m_PTSpecularMotion {};
    // The first path vertex as ray reconstruction is told about it, written by pt_main.rgen for
    // every pixel: a copy of the raster G-buffer, except on metallic delta mirrors, where Primary
    // Surface Replacement writes the first non-mirror surface seen through them instead.
    // pt_guides.comp builds the demodulation guides out of these, and RR's depth and motion tags
    // name the last two. Nothing raster reads them - every later raster pass keeps m_MainDepth.
    //  - albedo RGBA8: albedo in rgb, metallic in a, the G-buffer's own precision;
    //  - normal RGBA16F: world normal in xyz (mirrored back through the chain), roughness in w;
    //  - throughput RGBA16F: mirror chain reflectance in rgb, 1 in a where the surface has a PBR
    //    response. Float because a dark tint through a coloured metal would band in 8 bits;
    //  - depth R32F: reversed-Z like m_MainDepth, the virtual point for a replaced surface;
    //  - motion RG16F: MainVelocity's convention, the virtual point's motion where replaced.
    RGHandle m_PTPrimaryAlbedo {};
    RGHandle m_PTPrimaryNormal {};
    RGHandle m_PTPrimaryThroughput {};
    RGHandle m_PTDepth {};
    RGHandle m_PTMotion {};
    // The reflection layer: on smooth dielectric and glass pixels the first vertex is split into a
    // base path (m_PathTraceNoisy) and a mirror path whose radiance goes to m_PTLayerRadiance and
    // is denoised by a second RR instance (viewport 1). The layer first-vertex images describe the
    // reflected surface the way PSR does for metal mirrors; everywhere else they copy the primary
    // ones. m_DLSSLayerOutput is that instance's output.
    RGHandle m_PTLayerRadiance {};
    RGHandle m_PTLayerAlbedo {};
    RGHandle m_PTLayerNormal {};
    RGHandle m_PTLayerThroughput {};
    RGHandle m_PTLayerDepth {};
    RGHandle m_PTLayerMotion {};
    RGHandle m_PTLayerDiffuseAlbedo {};
    RGHandle m_PTLayerSpecularAlbedo {};
    RGHandle m_PTLayerNormalRoughness {};
    RGHandle m_DLSSLayerOutput {};
    // RGBA16F, render resolution: what the path tracing render path does not trace - transparent
    // surfaces that are not glass to it, and particles - shaded by the forward transparent pass,
    // premultiplied over a clear of zero, and laid by the tracer into its sample - see
    // IsPathTraceTransparentLayerDrawn.
    RGHandle m_PTTransparentLayer {};
    // RGBA16F, render resolution: PathTraceNoisy as it was before the layer went in, which ray
    // reconstruction takes as kBufferTypeColorBeforeTransparency.
    RGHandle m_PTColorBeforeTransparency {};

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
    bool b_VolumeBricksVisible = true;
    // Wire boxes the editor's baked node overlay queued this frame; reset with the gizmo renderer.
    uint32_t m_VolumeNodeGizmosDrawn = 0;
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

    // Swapchain readback. Copied inside the presenting frame's own command buffer: once an
    // image is presented its contents belong to the presentation engine.
    VulkanBuffer m_SwapchainReadbackBuffer;
    VkExtent2D m_SwapchainReadbackExtent {};
    VkFormat m_SwapchainReadbackFormat = VK_FORMAT_UNDEFINED;
    uint32_t m_SwapchainReadbackSlot = 0;
    SwapchainReadbackState m_SwapchainReadbackState = SwapchainReadbackState::Idle;
    SwapchainReadback m_SwapchainReadback;
    void RecordSwapchainReadback(VkCommandBuffer cmd, uint32_t imageIndex);
    void LatchSwapchainReadback();
    void DestroySwapchainReadback();
#endif

    // Pass indices
    uint32_t m_DepthPrepassIndex {};
    uint32_t m_GBufferPassIndex {};
    uint32_t m_GTAODepthPrefilterPassIndex {};
    uint32_t m_SSGIRadiancePrefilterPassIndex {};
    uint32_t m_GTAOPassIndex {};
    uint32_t m_HiZPassIndex {};
    uint32_t m_PathTracePassIndex {};
    uint32_t m_PathTraceGuidesPassIndex {};
    uint32_t m_GTAODenoisePassIndex {};
    uint32_t m_LightCullPassIndex {};
    uint32_t m_DeferredLightingPassIndex {};
    uint32_t m_SSRPassIndex {};
    uint32_t m_BloomPassIndex {};
    uint32_t m_TAAPassIndex {};
    uint32_t m_DLSSEvaluatePassIndex {};
    uint32_t m_DLSSRayReconstructionPassIndex {};
    // The reflection layer's passes.
    uint32_t m_PathTraceLayerGuidesPassIndex {};
    uint32_t m_DLSSRayReconstructionLayerPassIndex {};
    uint32_t m_RRLayerCompositePassIndex {};
    uint32_t m_PathTraceTransparentPassIndex {};
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
    // The display cubemap the IBL sets bind at set 3 binding 3, kept here as well because
    // the path tracer's set is rebuilt from scratch every frame and cannot rely on a write
    // that only happens when the skybox changes.
    VkImageView m_SkyboxView {};
    VkSampler m_SkyboxSampler {};

    FrameUniformBuffer m_FrameUniformBuffer {};
    LightStorageBuffer m_LightBuffer;
    TileLightBuffer m_TileLightBuffer;
    ShadowManager m_ShadowManager;
    TlasBuilder m_TlasBuilder;
    // Alongside the TLAS rather than inside it: both are per frame in flight and both are
    // refilled from the same frame's asset state, but the material table is indexed by
    // material slot and is independent of the instance list - a later pass that traces
    // without rebuilding the structure still wants it.
    RayTracingMaterialTable m_MaterialTable;
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
    // Set 1 of the path tracing pass, one per frame in flight: bindings 0-2 are the ray tracing
    // scene, and from 3 up it carries the G-buffer, the sky, the lights and the outputs. Empty
    // wherever the path tracer could not be built.
    std::vector<VulkanDescriptorSet> m_PathTraceDescriptorSets;
    // Set 1 of the guide pass. Plain compute over the G-buffer, so it needs none of the
    // scene bindings above and exists on every device, ray tracing or not.
    std::vector<VulkanDescriptorSet> m_PathTraceGuideDescriptorSets;
    // The same guide layout over the reflection layer's first-vertex images, and the
    // composite's own set.
    std::vector<VulkanDescriptorSet> m_PathTraceLayerGuideDescriptorSets;
    std::vector<VulkanDescriptorSet> m_RRLayerCompositeDescriptorSets;
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
    // The path tracer proper: pt_main.rgen over the same hit group, with a second miss
    // shader for shadow rays. Invalid where the device cannot build it, which gates both
    // path traced views.
    PipelineHandle m_PathTracePipeline {};
    // The guide buffer pass. A plain compute shader over the G-buffer, so unlike the tracer
    // it builds everywhere and needs no availability test.
    PipelineHandle m_PathTraceGuidesPipeline {};
    // Base + reflection layer composite into DLSSOutput.
    PipelineHandle m_RRLayerCompositePipeline {};
    // The forward transparent layer of a path traced frame: the four forward transparent variants
    // and the particles over the layer's render pass.
    PipelineHandle m_PathTraceTransparentPipelines[4] {};
    PipelineHandle m_PathTraceParticlePipeline {};
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
    // maxFramesInFlight + 1 slots: the extra one belongs to the reflection probe
    // baker, which renders the atlas outside the frame loop.
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

    // pathTraceLayer draws the path tracing render path's layer: only what the tracer does not trace,
    // through the layer's pipelines.
    void DrawTransparent(VkCommandBuffer cmd, uint32_t frameIndex, FrameContext& frame, bool pathTraceLayer);

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
    bool& GetVolumeBricksVisible() { return b_VolumeBricksVisible; }
    // Set by the node overlay once it has queued its gizmos; the brick preview budget yields to it.
    uint32_t GetVolumeNodeGizmosDrawn() const { return m_VolumeNodeGizmosDrawn; }
    void SetVolumeNodeGizmosDrawn(uint32_t count) { m_VolumeNodeGizmosDrawn = count; }
    VolumeNodeColorMode& GetVolumeNodeColorMode() { return m_VolumeNodeColorMode; }
    bool& GetCollidersVisible() { return b_CollidersVisible; }
    bool& GetCameraFrustumsVisible() { return b_CameraFrustumsVisible; }
    GizmoRenderer& GetGizmoRenderer() { return m_GizmoRenderer; }
    void SetSelectedEntityPosition(const glm::vec3& pos) { b_HasSelectedEntity = true; m_SelectedEntityPosition = pos; }
    void ClearSelectedEntity() { b_HasSelectedEntity = false; }
    // The whole editor window as presented, for agent screenshots. The copy is recorded into
    // the frame that presents the image and read once that frame's fence has been waited on.
    bool IsSwapchainReadbackSupported() const { return m_Backend.GetSwapChain().SupportsTransferSource(); }
    void RequestSwapchainReadback();
    SwapchainReadbackState GetSwapchainReadbackState() const { return m_SwapchainReadbackState; }
    // True once per request, with the pixels or an error; the state returns to Idle.
    bool ConsumeSwapchainReadback(SwapchainReadback& outReadback);

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
    // Ray traced, into a bake scene built for this call. Saves the .yaiv, then reloads every
    // volume; a failure before saving reloads nothing, and a volume the reload leaves out (no
    // room next to the others) fails the bake too.
    bool BakeIrradianceVolume(entt::entity entity, class Scene& scene, class AssetManager& assets);
    // Builds one bake scene for every volume, bakes them all to disk, then reloads them.
    IrradianceVolumeBakeAllResult BakeAllIrradianceVolumes(class Scene& scene, class AssetManager& assets);
    // Irradiance volumes bake only by ray tracing, so false disables every volume bake.
    bool IsRayTracedBakeAvailable() const { return m_RayTracedProbeBaker.IsAvailable(); }
    // Fills the TLAS and material table bake slots from a bake snapshot on a single-time
    // command buffer. Nothing that reads either bake slot may still be in flight. Returns
    // whether both slots hold a traceable scene afterwards.
    bool BuildRayTracingBakeScene(const SceneSnapshot& snapshot, class AssetManager& assets);

    // Brick wireframes drawn for the selected volume's preview at most. GizmoRenderer shares
    // 32768 wire instances per frame between every depth tested gizmo, and the baked node
    // overlay can already take 20000 of them, so the cap drops by the node boxes that overlay
    // draws, down to MAX_DRAWN_PREVIEW_BRICKS_WITH_NODES.
    static constexpr uint32_t MAX_DRAWN_PREVIEW_BRICKS = 10000;
    static constexpr uint32_t MAX_DRAWN_PREVIEW_BRICKS_WITH_NODES = 4000;
    // Every how many bricks of each spacing level the wireframes draw, in preview order. The
    // cap is shared out per level, a level needing less than its share passing the rest on, so
    // the few fine bricks that show refinement survive a coarse majority. A level draws
    // ceil(levelStats[level].bricks / stride) bricks. drawnNodeGizmos is GetVolumeNodeGizmosDrawn.
    static std::array<uint32_t, IRRADIANCE_SPACINGS.size()> GetPreviewBrickDrawStrides(
      const IrradianceVolumePlacementPreview& preview, uint32_t drawnNodeGizmos);
    // Lays out the sparse bricks the volume would bake with, by ray traced geometry queries into
    // a bake scene built for this call, validates the layout and keeps its summary for the entity,
    // a failed layout included. nullptr, with an error logged, when the entity has no volume, the
    // ray traced baker is unavailable or the bake scene could not be built; the last two replace
    // the entity's kept preview with one carrying the error.
    const IrradianceVolumePlacementPreview* PreviewIrradianceVolumePlacement(entt::entity entity,
      class Scene& scene, class AssetManager& assets);
    const IrradianceVolumePlacementPreview* FindIrradianceVolumePlacementPreview(entt::entity entity) const;
    void ClearIrradianceVolumePlacementPreviews() { m_VolumePlacementPreviews.clear(); }
    // Drops the previews of entities that were destroyed or lost their volume.
    void PruneIrradianceVolumePlacementPreviews(class Scene& scene);
    static IrradianceVolumePlacementFingerprint ComputeIrradianceVolumePlacementFingerprint(class Scene& scene,
      entt::entity entity);
    static IrradianceVolumePlacementEstimate EstimateIrradianceVolumePlacement(const IrradianceVolumePlacementPreview& preview,
      uint32_t samplesPerProbe);
    // By spacing index, coarse to fine: blue, cyan, green, yellow, red.
    static glm::vec4 GetPlacementBrickColor(uint32_t spacingIndex);
    // See IrradianceVolumeStorage::GetGeneration.
    uint32_t GetIrradianceVolumeGeneration() const { return m_VolumeStorage.GetGeneration(); }
    // Position and half extents of the box the volume in an uploaded slot was baked in, as its file
    // stores them. False for a slot outside the uploaded set.
    bool GetBakedIrradianceVolumeBox(uint32_t slot, glm::vec3& outPosition, glm::vec3& outHalfExtents) const
    {
      const IrradianceVolumeBuffer& buffer = m_VolumeStorage.GetBufferData();
      if (slot >= uint32_t(buffer.volumeCount))
        return false;
      // The buffer holds the inverse of the baked position and rotation
      outPosition = glm::vec3(glm::inverse(buffer.volumes[slot].worldToLocal)[3]);
      outHalfExtents = glm::vec3(buffer.volumes[slot].halfExtentsFade);
      return true;
    }
#endif

    ReflectionProbeAtlas& GetProbeAtlas() { return m_ProbeAtlas; }

  private:
#ifdef YA_EDITOR
    ShaderHotReload m_ShaderHotReload;
    ReflectionProbeBaker m_ProbeBaker;
    RayTracedProbeBaker m_RayTracedProbeBaker;
    // Per volume entity; cleared on scene change, pruned of destroyed entities.
    std::unordered_map<entt::entity, IrradianceVolumePlacementPreview> m_VolumePlacementPreviews;

    // The brick layout of a volume placement, by geometry queries into the bake scene
    // BuildRayTracingBakeScene last built. The one path both the placement preview and the bake
    // lay bricks out by, so the preview shows what the bake gets. Not validated.
    IrradianceBrickLayout BuildIrradianceVolumeBrickLayout(const IrradianceVolumePlacementFingerprint& placement,
      double& outQuerySeconds);

    // One volume against the bake scene BuildRayTracingBakeScene last built, with the lights
    // of the BuildBakeSceneSnapshot call that scene came from: layout, integration, dilation,
    // stitching. Saves the .yaiv and logs the per-volume result.
    bool BakeIrradianceVolumeInBakeScene(entt::entity entity, class Scene& scene,
      class AssetManager& assets, const LightBuffer& lights, IrradianceVolumeBakeCounts& outCounts);
#endif

    friend class OffscreenRenderer;
    friend class RayTracedProbeBaker;
  };
}
