#include "Render.h"

#include <imgui_impl_glfw.h>
#include <ImGui/imgui_impl_vulkan.h>

#include "Assets/AssetManager.h"
#include "Assets/CubeMapManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneSnapshot.h"
#include "DebugMarker.h"
#include "ImageBarrier.h"
#include "Utils/Log.h"

#include "Utils/ProfilerStorage.h"
#include "Utils/Utils.h"
#include "TileCullData.h"

namespace YAEngine
{
  void Render::Init(GLFWwindow* window, const RenderSpecs &specs)
  {
    m_Backend.Init(window, specs);
    auto& ctx = m_Backend.GetContext();

#ifdef YA_EDITOR
    m_GpuProfiler.Init(ctx);
    m_Graph.SetGpuProfiler(&m_GpuProfiler);
#endif

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    uint32_t whitePixel = 0xFFFFFFFF;
    m_NoneTexture.Load(ctx, &whitePixel, 1, 1, 4, VK_FORMAT_R8G8B8A8_SRGB);
    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_IMAGE,
      m_NoneTexture.GetImage(), "None Texture");

    // 1x1 black cubemap placeholder for IBL descriptors before skybox is loaded
    {
      ImageDesc cubeDesc;
      cubeDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
      cubeDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      cubeDesc.arrayLayers = 6;
      cubeDesc.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
      cubeDesc.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      SamplerDesc sampDesc;
      m_NoneCubeMap.Init(ctx, cubeDesc, &sampDesc);

      auto cmd = m_Backend.GetCommandBuffer().BeginSingleTimeCommands();
      TransitionImageLayout(cmd, m_NoneCubeMap.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
      const VkClearColorValue black {};
      const VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
      vkCmdClearColorImage(cmd, m_NoneCubeMap.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
      TransitionImageLayout(cmd, m_NoneCubeMap.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
      m_Backend.GetCommandBuffer().EndSingleTimeCommands(cmd);
      m_NoneCubeMap.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      // The path tracer rebuilds its descriptor set every frame and so cannot rely on the
      // one-off write the IBL sets get when a skybox is loaded. It reads the display
      // cubemap through these instead, which start on the same placeholder.
      m_SkyboxView = m_NoneCubeMap.GetView();
      m_SkyboxSampler = m_NoneCubeMap.GetSampler();
    }

    m_DefaultMaterial.Init(ctx, m_NoneTexture);
    m_TerrainMaterial.Init(ctx, m_NoneTexture);
    m_FrameUniformBuffer.Init(ctx);
    m_LightBuffer.Init(ctx);

    {
      uint32_t tileCountX = (uint32_t(width) + TILE_SIZE - 1) / TILE_SIZE;
      uint32_t tileCountY = (uint32_t(height) + TILE_SIZE - 1) / TILE_SIZE;
      m_TileLightBuffer.Init(ctx, tileCountX, tileCountY);
    }

    InitGTAOStaticResources();

    m_ShadowManager.Init(ctx);
    m_TlasBuilder.Init(ctx);
    m_MaterialTable.Init(ctx);
    m_ProbeAtlas.Init(ctx);
    m_ProbeBuffer.Init(ctx);
    m_VolumeStorage.Init(ctx);

    // Both extents start equal: the selected mode is only known once a scene has been
    // deserialized, and the first Draw resizes the render half if it needs to.
    VkExtent2D initialExtent { uint32_t(width), uint32_t(height) };
    m_ResolutionOutputExtent = initialExtent;
    m_ResolutionMode = m_EffectiveAntialiasingMode;
    m_ResolutionPath = m_EffectiveRenderPath;

    SetupRenderGraph(initialExtent, initialExtent);
    CreateTAAFramebuffers();
    m_Backend.GetSwapChain().CreateFrameBuffers(
      m_Graph.GetPassRenderPass(m_SwapchainPassIndex));
    ClearHistoryBuffers();
    ClearPathTraceOutputs();
    InitPipelines();
    CreateHiZResources();
    CreateGTAOResources();
    CreateSSGIResources();
    CreateBloomResources();

    m_Backend.InitImGui(window, m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

#ifdef YA_EDITOR
    m_ProbeBaker.Init(*this, BakeLimits::PROBE_DEFAULT_CAPTURE_RESOLUTION);
    m_RayTracedProbeBaker.Init(*this);
    CreateSceneImGuiDescriptor();
    CreatePickResources();
    m_ViewportWidth = width;
    m_ViewportHeight = height;
    m_PendingViewportWidth = width;
    m_PendingViewportHeight = height;
#endif

    m_CubicResources.Init(ctx);

    vkDeviceWaitIdle(ctx.device);
  }

  void Render::WaitIdle()
  {
    vkDeviceWaitIdle(m_Backend.GetContext().device);
  }

  void Render::UploadIrradianceVolumes(const std::vector<IrradianceVolumeFileData>& volumes,
    std::vector<uint32_t>& outSlots)
  {
    m_VolumeStorage.Upload(m_Backend.GetContext(), volumes, outSlots);
    WriteIrradianceVolumeDescriptors();
    m_VolumeUploadDirty = uint32_t(m_Backend.GetContext().maxFramesInFlight);
  }

  void Render::Destroy()
  {
    vkDeviceWaitIdle(m_Backend.GetContext().device);

    auto& ctx = m_Backend.GetContext();

#ifdef YA_EDITOR
    m_GpuProfiler.Destroy(ctx);
    m_ShaderHotReload.Destroy();
    m_ProbeBaker.Destroy();
    m_RayTracedProbeBaker.Destroy();
    DestroySceneImGuiDescriptor();
    DestroyPickResources();
    DestroySwapchainReadback();
    for (auto& set : m_DepthCopyDescriptorSets)
      set.Destroy();
    m_GizmoRenderer.Destroy(ctx);
#endif

    m_ShadowManager.Destroy(ctx);
    m_TlasBuilder.Destroy(ctx);
    m_MaterialTable.Destroy(ctx);
    m_ProbeAtlas.Destroy(ctx);
    m_ProbeBuffer.Destroy(ctx);
    m_VolumeStorage.Destroy(ctx);
    m_CubicResources.Destroy(ctx);
    m_NoneCubeMap.Destroy(ctx);
    m_NoneTexture.Destroy(ctx);

    DestroyBloomResources();
    DestroyHiZResources();
    DestroyGTAOResources();
    DestroySSGIResources();

    for (auto& fb : m_TAAFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    m_TAADepth.Destroy(ctx);

    for (auto& set : m_SwapChainDescriptorSets)
      set.Destroy();
    for (auto& set : m_TAADescriptorSets)
      set.Destroy();
    for (auto& set : m_SSRPassDescriptorSets)
      set.Destroy();
    for (auto& set : m_GTAOPrefilterDescriptorSets)
      set.Destroy();
    for (auto& set : m_SSGIPrefilterDescriptorSets)
      set.Destroy();
    for (auto& set : m_GTAOPassDescriptorSets)
      set.Destroy();
    for (auto& set : m_GTAODenoiseDescriptorSets)
      set.Destroy();
    for (auto& set : m_DeferredLightingDescriptorSets)
      set.Destroy();
    for (auto& set : m_DeferredLightingLightDescriptorSets)
      set.Destroy();
    for (auto& set : m_ForwardTransparentLightDescriptorSets)
      set.Destroy();
    for (auto& set : m_LightCullInputDescriptorSets)
      set.Destroy();
    for (auto& set : m_PathTraceDescriptorSets)
      set.Destroy();
    for (auto& set : m_PathTraceGuideDescriptorSets)
      set.Destroy();
    for (auto& set : m_HistogramPassDescriptorSets)
      set.Destroy();
    m_HistogramOutputDescriptorSet.Destroy();
    for (auto& set : m_ExposureAdaptDescriptorSets)
      set.Destroy();
    for (auto& set : m_ExposureReadDescriptorSets)
      set.Destroy();
    for (auto& set : m_IBLDescriptorSets)
      set.Destroy();
    for (auto& set : m_ParticleDescriptorSets)
      set.Destroy();
    for (auto& buf : m_ParticleInstanceBuffers)
      buf.Destroy(ctx);

    m_GTAOHilbertLUT.Destroy(ctx);
    for (auto& ubo : m_GTAOConstantsUBOs)
      ubo.Destroy(ctx);
    m_InstanceDescriptorSet.Destroy();
    m_InstanceBuffer.Destroy(ctx);
    for (auto& set : m_PrevWorldDescriptorSets)
      set.Destroy();
    for (auto& buffer : m_PrevWorldBuffers)
      buffer.Destroy(ctx);
    DestroyShadowIndirectResources();
    m_HistogramBuffer.Destroy(ctx);
    m_ExposureBuffer.Destroy(ctx);
    m_PSOCache.Destroy();
    m_DefaultMaterial.Destroy(ctx);
    m_TerrainMaterial.Destroy(ctx);
    m_TileLightBuffer.Destroy(ctx);
    m_LightBuffer.Destroy(ctx);
    m_FrameUniformBuffer.Destroy(ctx);

    m_Graph.Destroy();
    m_Backend.Destroy();
  }

  void Render::ResizeGraph(VkExtent2D renderExtent, VkExtent2D outputExtent)
  {
    auto& ctx = m_Backend.GetContext();

    vkDeviceWaitIdle(ctx.device);

    uint32_t hizMipCount = static_cast<uint32_t>(
      std::floor(std::log2(std::max(renderExtent.width, renderExtent.height)))) + 1;
    m_Graph.SetResourceMipLevels(m_HiZResource, hizMipCount);

    // A DLSS instance is built for one pair of extents and has to be rebuilt for the new
    // one. Ray reconstruction keeps an instance of its own under a second feature id, so
    // both are released here or the one that was not would keep serving the old extents.
    m_Backend.GetStreamline().ReleaseDLSSResources();
    m_Backend.GetStreamline().ReleaseRayReconstructionResources();
    b_ResetDLSSPending = true;

    DestroyBloomResources();
    DestroyHiZResources();
    DestroyGTAOResources();
    DestroySSGIResources();
#ifdef YA_EDITOR
    DestroySceneImGuiDescriptor();
#endif

    // Recreates managed resources and non-external framebuffers
    m_Graph.Resize(renderExtent, outputExtent);

    // The path tracer's two images are among them, the accumulated one included: the samples
    // it held describe a different resolution, so the mean starts over.
    b_PathTraceOutputValid = false;
    b_PathTraceResetPending = true;

#ifdef YA_EDITOR
    CreateSceneImGuiDescriptor();
#endif
    CreateHiZResources();
    CreateGTAOResources();
    CreateSSGIResources();
    CreateBloomResources();

    {
      uint32_t tileCountX = (renderExtent.width + TILE_SIZE - 1) / TILE_SIZE;
      uint32_t tileCountY = (renderExtent.height + TILE_SIZE - 1) / TILE_SIZE;
      m_TileLightBuffer.Resize(ctx, tileCountX, tileCountY);
      for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
      {
        m_DeferredLightingLightDescriptorSets[i].WriteStorageBuffer(1,
          m_TileLightBuffer.GetBuffer(uint32_t(i)), m_TileLightBuffer.GetBufferSize());
        m_ForwardTransparentLightDescriptorSets[i].WriteStorageBuffer(1,
          m_TileLightBuffer.GetTransparentBuffer(uint32_t(i)), m_TileLightBuffer.GetBufferSize());
      }
    }

    for (auto& fb : m_TAAFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    m_TAADepth.Destroy(ctx);
    CreateTAAFramebuffers();

    ClearHistoryBuffers();
    ClearPathTraceOutputs();
  }

  void Render::Resize()
  {
    b_Resized = false;

    // Recreate swapchain first to get actual surface dimensions. A minimized window has none; the
    // resize that restoring it brings does the work then.
    if (!m_Backend.GetSwapChain().Recreate(m_Graph.GetPassRenderPass(m_SwapchainPassIndex)))
      return;

#ifdef YA_EDITOR
    // In editor mode, graph extent = viewport size (independent of window size).
    // Only swapchain is recreated here; viewport resize handled by ResizeViewport().
#else
    VkExtent2D outputExtent = m_Backend.GetSwapChain().GetExt();
    ResizeGraph(ComputeRenderExtent(m_EffectiveAntialiasingMode, outputExtent), outputExtent);

    m_ResolutionMode = m_EffectiveAntialiasingMode;
    m_ResolutionPath = m_EffectiveRenderPath;
    b_ResolutionDevResolve = b_PathTraceDevResolve;
    m_ResolutionOutputExtent = outputExtent;
#endif
  }

  void Render::UpdatePathTraceAccumulation(const FrameContext& frame)
  {
    // In raster mode the reference view is the only consumer of the running mean, so
    // nothing accumulates while it is not on the screen - and it starts from sample zero
    // when it comes back. A noisy-view frame writing into the buffer would poison the mean
    // anyway: its push constant carries -1 and the shader leaves the image alone. The path
    // tracing render path always accumulates: the mean is the image it presents.
    if (!IsPathTraceAccumulating())
    {
      m_PathTraceSampleIndex = 0;
      return;
    }

    // A frame the pass will not run on contributes no sample, so the index has to stay put
    // or the next one that does run would be blended in at the wrong weight. The cached
    // state is left alone too, so a camera that moved during the gap is still detected.
    if (!IsPathTracePassEnabled())
      return;

    // Everything the samples already in the buffer depend on. The projection compared is the
    // UNJITTERED one on purpose: the per-frame jitter is what gives the reference its
    // anti-aliasing, and treating it as camera motion would reset the mean every frame and
    // the image would never converge at all.
    const bool cameraMoved =
      m_FrameUniformBuffer.uniforms.view != m_PathTraceCachedView
      || m_UnjitteredProj != m_PathTraceCachedProj;

    // The shadow atlas cache's digests cover which casters exist and where they are, and the
    // acceleration structure is rebuilt from the same snapshot. Its light digest leaves out all
    // a shadow cannot see - intensity, colour, the sun direction, lights without shadows, emitter
    // size - so the whole light buffer and the emission and transmission this frame's material
    // table holds are compared on top.
    const uint64_t materialDigest = m_MaterialTable.GetPathTraceDigest(m_Backend.GetCurrentFrameIndex());
    const bool sceneChanged =
      frame.snapshot.casterIdentityDigest != m_PathTraceCachedIdentityDigest
      || frame.snapshot.casterTransformDigest != m_PathTraceCachedTransformDigest
      || frame.snapshot.lightDigest != m_PathTraceCachedLightDigest
      || frame.snapshot.pathTraceLightDigest != m_PathTraceCachedLightBufferDigest
      || materialDigest != m_PathTraceCachedMaterialDigest;
    const uint64_t glassKey = GetPathTraceGlassKey();
    // The tracer lays the forward transparent layer into its sample on the frames it is drawn, so samples
    // taken with and without it belong to different images.
    const bool transparentLayer = IsPathTraceTransparentLayerDrawn();

    if (b_PathTraceResetPending || cameraMoved || sceneChanged || glassKey != m_PathTraceCachedGlassKey
      || transparentLayer != b_PathTraceCachedTransparentLayer)
    {
      // Zero is the reset: the shader rewrites the image at that index rather than blending
      // into it, so nothing has to be cleared and no extra pass exists to clear it.
      m_PathTraceSampleIndex = 0;
      b_PathTraceResetPending = false;
    }
    else
    {
      m_PathTraceSampleIndex++;
    }

    m_PathTraceCachedView = m_FrameUniformBuffer.uniforms.view;
    m_PathTraceCachedProj = m_UnjitteredProj;
    m_PathTraceCachedIdentityDigest = frame.snapshot.casterIdentityDigest;
    m_PathTraceCachedTransformDigest = frame.snapshot.casterTransformDigest;
    m_PathTraceCachedLightDigest = frame.snapshot.lightDigest;
    m_PathTraceCachedLightBufferDigest = frame.snapshot.pathTraceLightDigest;
    m_PathTraceCachedMaterialDigest = materialDigest;
    m_PathTraceCachedGlassKey = glassKey;
    b_PathTraceCachedTransparentLayer = transparentLayer;
  }

  void Render::Draw(FrameContext& frame)
  {
    // Runs first: the resolution the rest of the frame is set up for depends on both, and
    // the path resolve reads the mode one - see ResolveRenderPath.
    ResolveAntialiasingMode();
    ResolveRenderPath();

    // Ahead of everything that binds the resolved image or renders the shadow atlas, which both
    // depend on it.
    const bool transparency = IsPathTracingActive() && HasPathTraceRasterTransparency(frame);
    if (transparency)
      m_PathTraceTransparencyHoldFrames = PT_TRANSPARENCY_HOLD_FRAMES;
    else if (m_PathTraceTransparencyHoldFrames > 0 && IsPathTracingActive())
      m_PathTraceTransparencyHoldFrames--;
    else
      m_PathTraceTransparencyHoldFrames = 0;
    b_PathTraceTransparencyActive = transparency || m_PathTraceTransparencyHoldFrames > 0;

#ifdef YA_EDITOR
    // Handle deferred viewport resize BEFORE acquiring the frame -
    // no command buffer is recording at this point, safe to wait for GPU and recreate resources
    if (m_PendingViewportWidth != m_ViewportWidth || m_PendingViewportHeight != m_ViewportHeight)
    {
      if (m_PendingViewportWidth > 0 && m_PendingViewportHeight > 0)
      {
        YA_PROFILE_CPU("ViewportResize");
        ResizeViewport();
      }
    }

    {
      YA_PROFILE_CPU("ShaderPoll");
      if (m_ShaderHotReload.Update(frame.time))
      {
        // shadow.vert and alphatest_discard.frag feed the whole shadow pipeline
        // family, so a reload can change what the atlas should contain while
        // every cache digest stays equal. Nothing else observes shader identity.
        b_ShadowAtlasContentValid = false;
        m_ShadowCachePendingReason = ShadowInvalidation::ShaderReloaded;
      }
    }

    if (m_PendingInvalidateSlot > 0)
    {
      YA_PROFILE_CPU("ProbeInvalidate");
      vkDeviceWaitIdle(m_Backend.GetContext().device);
      m_ProbeAtlas.InvalidateSlotPreview(m_Backend.GetContext(), m_PendingInvalidateSlot);
      m_PendingInvalidateSlot = 0;
    }
#endif

    YA_PROFILE_CPU_BEGIN(setup, "FrameSetup");

#ifdef YA_EDITOR
    UpdateResolutionForMode({m_ViewportWidth, m_ViewportHeight});
#else
    UpdateResolutionForMode(m_Backend.GetSwapChain().GetExt());
#endif

    if (b_ResetAutoExposurePending)
    {
      float unity = 1.0f;
      m_ExposureBuffer.Update(0, &unity, sizeof(float));
      b_ResetAutoExposurePending = false;
    }

    YA_PROFILE_CPU_END(setup);

    // BeginFrame blocks on the fence of the frame slot it is about to reuse, so this
    // zone is time spent waiting for the GPU, not work. Kept apart from RecordCmd.
    YA_PROFILE_CPU_BEGIN(wait, "WaitFrame");
    auto imageIndex = m_Backend.BeginFrame();
    YA_PROFILE_CPU_END(wait);

    if (!imageIndex)
    {
      // The snapshot and the refit scheduler already ran, so WorldBounds::prev
      // advanced for this frame while the atlas was never patched. Dropping the
      // cached content is the only way the next frame can still trust it.
      b_ShadowAtlasContentValid = false;
      m_ShadowCachePendingReason = ShadowInvalidation::Resize;
      Resize();
      return;
    }

#ifdef YA_EDITOR
    // BeginFrame waited on this slot's fence, so a pick queued into it earlier has
    // landed and can be read without stalling.
    LatchPickResult();
    BeginPickFrame();
    LatchSwapchainReadback();
#endif

    m_Stats = {};

    auto cmd = m_Backend.GetCurrentCommandBuffer();

    YA_PROFILE_CPU_BEGIN(record, "RecordCmd");

#ifdef YA_EDITOR
    m_GpuProfiler.BeginFrame(m_Backend.GetContext(), cmd, m_Backend.GetCurrentFrameIndex(),
      ProfilerStorage::Get().GetRecordingFrame());
#endif

    SetUpCamera(frame);
    float currentTime = (float)frame.time;
    float deltaTime = currentTime - m_LastFrameTime;
    m_LastFrameTime = currentTime;
    m_DeltaTime = deltaTime;
    m_FrameUniformBuffer.uniforms.time = currentTime;
    m_FrameUniformBuffer.uniforms.gamma = m_Gamma;
    m_FrameUniformBuffer.uniforms.exposure = m_Exposure;
    m_FrameUniformBuffer.uniforms.tonemapPower = m_TonemapPower;
    m_FrameUniformBuffer.uniforms.tonemapSaturation = m_TonemapSaturation;
    m_FrameUniformBuffer.uniforms.ditherEnabled = b_DitherEnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.currentTexture = m_CurrentTexture;
    // The path tracer's two views display images only its own pass ever writes, and that pass
    // runs later in this very frame. Until it has run once - no ray tracing on this device, a
    // scene with nothing traceable, or a resize that just reallocated the images - the view
    // falls back to the final image instead of showing whatever the allocation happened to hold.
    if (IsPathTraceView() && !b_PathTraceOutputValid)
    {
      m_FrameUniformBuffer.uniforms.currentTexture = 0;
    }
    // The path tracing render path switches off the AO chain, the screen space effects,
    // the deferred lighting pass and the temporal resolve, so every view fed by one of
    // them would display a buffer nothing wrote this frame. The guide view is the mirror
    // case: its pass only exists while that path is effective.
    if (IsPathTracingActive() && IS_RASTER_ONLY_DEBUG_VIEW(m_CurrentTexture))
    {
      m_FrameUniformBuffer.uniforms.currentTexture = 0;
    }
    if ((m_CurrentTexture == DEBUG_VIEW_PT_GUIDES || m_CurrentTexture == DEBUG_VIEW_PT_SPECULAR_MOTION)
      && !IsPathTracingActive())
    {
      m_FrameUniformBuffer.uniforms.currentTexture = 0;
    }
    // screenWidth/Height is the resolution the scene is rasterized and shaded at;
    // outputWidth/Height is what reaches the screen. They only differ while a DLSS
    // upscale mode is active.
    VkExtent2D renderExtent = m_Graph.GetExtent();
    VkExtent2D outputExtent = m_Graph.GetOutputExtent();
    m_FrameUniformBuffer.uniforms.screenWidth = int(renderExtent.width);
    m_FrameUniformBuffer.uniforms.screenHeight = int(renderExtent.height);
    m_FrameUniformBuffer.uniforms.outputWidth = int(outputExtent.width);
    m_FrameUniformBuffer.uniforms.outputHeight = int(outputExtent.height);
    m_FrameUniformBuffer.uniforms.tileCountX = (m_FrameUniformBuffer.uniforms.screenWidth + TILE_SIZE - 1) / TILE_SIZE;
    m_FrameUniformBuffer.uniforms.tileCountY = (m_FrameUniformBuffer.uniforms.screenHeight + TILE_SIZE - 1) / TILE_SIZE;
    m_FrameUniformBuffer.uniforms.aoEnabled = b_AOEnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.aoStrength = m_AOStrength;
    m_FrameUniformBuffer.uniforms.aoSpecularStrength = m_AOSpecularStrength;
    m_FrameUniformBuffer.uniforms.aoMultiBounce = m_AOMultiBounce;
    m_FrameUniformBuffer.uniforms.ssrEnabled = b_SSREnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.ssrIntensity = m_SSRIntensity;
    // SSGI rides the GTAO pass, so it cannot outlive the AO toggle.
    m_FrameUniformBuffer.uniforms.ssgiEnabled = (b_SSGIEnabled && b_AOEnabled) ? 1 : 0;
    m_FrameUniformBuffer.uniforms.taaEnabled = UsesTAAPass(m_EffectiveAntialiasingMode) ? 1 : 0;
    m_FrameUniformBuffer.uniforms.taaClampSigma = m_TAAClampSigma;

    // A reset resolves one frame without history: the passthrough writes the current frame,
    // which is all the next one blends against. A history cleared to black instead would sit
    // inside the at-rest clamp box on textured surfaces and fade out over a second.
    if (b_ResetTAAPending)
    {
      m_FrameUniformBuffer.uniforms.taaEnabled = 0;
      // SSGI reprojects the previous resolved image, which belongs to the viewpoint left behind
      b_SSGIInvalidatePending = true;
      b_ResetTAAPending = false;
    }

    // Indirect lighting debug views must reach the screen untouched: SSR/TAA already pass
    // through when disabled, so zeroing the flags here needs no extra plumbing. Camera
    // jitter is handled separately in SetUpCamera, which runs before this.
    if (IS_INDIRECT_DEBUG_VIEW(m_CurrentTexture))
    {
      m_FrameUniformBuffer.uniforms.ssrEnabled = 0;
      m_FrameUniformBuffer.uniforms.taaEnabled = 0;
    }
    m_FrameUniformBuffer.uniforms.hizMipCount = static_cast<int>(m_Graph.GetResourceDesc(m_HiZResource).mipLevels);
    m_FrameUniformBuffer.uniforms.frameIndex = static_cast<int>(m_GlobalFrameIndex);
    m_FrameUniformBuffer.uniforms.tonemapMode = m_TonemapMode;
    m_FrameUniformBuffer.uniforms.bloomIntensity = b_BloomEnabled ? m_BloomIntensity : 0.0f;
    m_FrameUniformBuffer.uniforms.fogEnabled = b_FogEnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.fogDensity = m_FogDensity;
    m_FrameUniformBuffer.uniforms.fogHeightFalloff = m_FogHeightFalloff;
    m_FrameUniformBuffer.uniforms.fogMaxOpacity = m_FogMaxOpacity;
    m_FrameUniformBuffer.uniforms.fogColor = m_FogColor;
    m_FrameUniformBuffer.uniforms.fogStartDistance = m_FogStartDistance;
    m_FrameUniformBuffer.uniforms.irradianceNormalBias = m_IrradianceNormalBias;

    auto historyWrite = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
    auto historyRead = m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;
    auto resolvedColor = GetResolvedColorHandle();

    // SSGI reprojects the resolved image of the PREVIOUS frame. With DLSS that is the
    // single output image, still holding the previous frame here because the evaluate
    // that overwrites it runs much later in the graph.
    m_Graph.SetPassInput(m_SSGIRadiancePrefilterPassIndex, 0, GetPreviousResolvedColorHandle());

    // Nothing reads the histories while DLSS resolves, and nothing writes them while the
    // path tracer owns the frame, so the ping-pong stays untouched in both.
    if (!IsDLSSMode(m_EffectiveAntialiasingMode) && !IsPathTracingActive())
    {
      m_Graph.SetPassInput(m_TAAPassIndex, 1, historyRead);
      m_Graph.SetPassColorOutput(m_TAAPassIndex, 0, historyWrite);
      m_Graph.SetPassFramebuffer(m_TAAPassIndex, m_TAAFramebuffers[m_TAAIndex]);
    }

    m_Graph.SetPassInput(m_HistogramPassIndex, 0, resolvedColor);

#ifdef YA_EDITOR
    m_Graph.SetPassInput(m_SceneComposePassIndex, 0, resolvedColor);

    // SwapchainPass renders ImGui at full window size, override extent
    auto swapExtent = m_Backend.GetSwapChain().GetExt();
    m_Graph.SetPassExtent(m_SwapchainPassIndex, swapExtent);
#else
    m_Graph.SetPassInput(m_SwapchainPassIndex, 0, resolvedColor);
#endif
    m_Graph.SetPassFramebuffer(m_SwapchainPassIndex,
      m_Backend.GetSwapChain().GetFramebuffer(*imageIndex));

    auto currentFrame = m_Backend.GetCurrentFrameIndex();
    m_FrameUniformBuffer.SetUp(currentFrame);
    // Reads back the projection and viewport SetUpCamera just wrote, so it has to run after it.
    UpdateGTAOConstants(currentFrame);
    m_LightBuffer.SetUp(currentFrame, frame.lights);
    m_PathTraceSphereLights = FindSphereLightSpan(frame.lights);

    // Update IBL when skybox changes: upload to atlas slot 0 + update display cubemap
    auto skybox = frame.snapshot.skybox;
    if (!skybox && m_BoundSkybox)
    {
      // Cleared: the old sky must stop lighting and reflecting in the scene
      m_ProbeAtlas.ClearSkybox(m_Backend.GetContext());
      for (auto& set : m_IBLDescriptorSets)
        set.WriteCombinedImageSampler(3, m_NoneCubeMap.GetView(), m_NoneCubeMap.GetSampler());
      m_SkyboxView = m_NoneCubeMap.GetView();
      m_SkyboxSampler = m_NoneCubeMap.GetSampler();
      m_BoundSkybox = {};
    }
    else if (skybox && skybox != m_BoundSkybox)
    {
      auto& cubeMap = frame.assets.CubeMaps().GetVulkanCubicTexture(skybox);
      m_ProbeAtlas.UploadSkybox(m_Backend.GetContext(), cubeMap);
      for (auto& set : m_IBLDescriptorSets)
      {
        set.WriteCombinedImageSampler(2,
          frame.cubicResources.brdfLut.GetView(), frame.cubicResources.brdfLut.GetSampler());
        set.WriteCombinedImageSampler(3,
          cubeMap.GetView(), cubeMap.GetSampler());
      }
      m_SkyboxView = cubeMap.GetView();
      m_SkyboxSampler = cubeMap.GetSampler();
      m_BoundSkybox = skybox;
    }

    // Upload probe SSBO. The descriptor is written once at init and the buffers
    // are never recreated, so there is nothing to rewrite here per frame.
    m_ProbeBuffer.SetUp(currentFrame, frame.snapshot.probeBuffer);

    // Turning volumes off means the shader falls back to skybox irradiance from atlas slot 0
    // everywhere. Unlike the probe buffer this isn't rebuilt from the snapshot, so it only
    // changes on upload or toggle - one dirty frame per frame in flight.
    if (m_VolumeUploadDirty > 0 || b_IrradianceVolumesEnabled != b_VolumeUniformsEnabled)
    {
      if (b_IrradianceVolumesEnabled != b_VolumeUniformsEnabled)
      {
        b_VolumeUniformsEnabled = b_IrradianceVolumesEnabled;
        m_VolumeUploadDirty = uint32_t(m_Backend.GetContext().maxFramesInFlight);
      }

      IrradianceVolumeBuffer volumeData = m_VolumeStorage.GetBufferData();
      if (!b_IrradianceVolumesEnabled)
        volumeData.volumeCount = 0;
      m_VolumeStorage.SetUp(currentFrame, volumeData);
      m_VolumeUploadDirty--;
    }

#ifdef YA_EDITOR
    m_GizmoRenderer.Clear();
    // The gizmo passes draw at output resolution, with the projection gizmo_sprite.vert uses
    m_GizmoRenderer.SetSpriteView(m_FrameUniformBuffer.uniforms.view, m_UnjitteredProj,
      float(m_Graph.GetOutputExtent().height));
    m_VolumeNodeGizmosDrawn = 0;
    if (b_GizmosEnabled)
    {
      for (int i = 0; i < frame.lights.pointLightCount; i++)
      {
        glm::vec3 pos(frame.lights.pointLights[i].positionRadius);
        glm::vec3 col(frame.lights.pointLights[i].colorIntensity);
        m_GizmoRenderer.DrawSprite(pos, EditorIcon::WORLD_SIZE, EditorIcon::LIGHT_BULB, glm::vec4(col, 0.85f));
      }

      for (int i = 0; i < frame.lights.spotLightCount; i++)
      {
        glm::vec3 pos(frame.lights.spotLights[i].positionRadius);
        glm::vec3 dir(frame.lights.spotLights[i].directionInnerCone);
        glm::vec3 col(frame.lights.spotLights[i].colorOuterCone);
        float outerCos = std::clamp(frame.lights.spotLights[i].colorOuterCone.w, -1.0f, 1.0f);
        float angle = std::acos(outerCos);
        m_GizmoRenderer.DrawSprite(pos, EditorIcon::WORLD_SIZE, EditorIcon::LIGHT_BULB, glm::vec4(col, 0.85f));
        m_GizmoRenderer.DrawWireCone(pos, dir, 2.0f, angle, glm::vec4(col, 0.85f));
      }

      glm::vec3 dirLightDir(frame.lights.directional.directionIntensity);
      glm::vec3 dirCol(frame.lights.directional.colorPad);
      glm::vec3 dirLightPos = frame.snapshot.directionalShadow.position;
      float dirIntensity = frame.lights.directional.directionIntensity.w;
      if (dirIntensity > 0.0f)
      {
        m_GizmoRenderer.DrawSprite(dirLightPos, EditorIcon::WORLD_SIZE, EditorIcon::SUN, glm::vec4(dirCol, 0.85f));
        m_GizmoRenderer.DrawArrow(dirLightPos, dirLightDir, 3.0f, glm::vec4(dirCol, 0.85f));
      }

      if (b_ProbeVolumesVisible)
      {
        for (int i = 0; i < frame.snapshot.probeBuffer.probeCount; i++)
        {
          auto& probe = frame.snapshot.probeBuffer.probes[i];
          glm::vec3 pos(probe.positionShape);
          float shape = probe.positionShape.w;
          glm::vec3 extents(probe.extentsFade);
          glm::vec4 col(0.2f, 0.7f, 0.9f, 0.5f);
          glm::quat rot(probe.orientation.w, probe.orientation.x,
            probe.orientation.y, probe.orientation.z);

          if (shape < 0.5f)
          {
            m_GizmoRenderer.DrawWireSphereDepthTested(pos, extents.x, col);
          }
          else
          {
            m_GizmoRenderer.DrawWireBoxDepthTested(pos, extents, rot, col);
          }

          // Parallax proxy in green, so the two volumes stay tellable apart. Drawn
          // only when it actually differs - a probe that falls back to the influence
          // volume would otherwise paint a second wireframe over the first one.
          glm::vec3 proxyOffset(probe.proxyOffset);
          glm::vec3 proxyExtents(probe.proxyExtents);
          bool proxyDiffers = probe.parallaxCorrection != 0
            && (proxyOffset != glm::vec3(0.0f) || proxyExtents != extents);
          if (proxyDiffers)
          {
            glm::vec3 proxyPos = pos + rot * proxyOffset;
            glm::vec4 proxyCol(0.3f, 0.9f, 0.4f, 0.5f);

            if (shape < 0.5f)
              m_GizmoRenderer.DrawWireSphereDepthTested(proxyPos, proxyExtents.x, proxyCol);
            else
              m_GizmoRenderer.DrawWireBoxDepthTested(proxyPos, proxyExtents, rot, proxyCol);
          }
        }
      }

      // Irradiance volume bounds - warm orange to separate them from the cool
      // blue of the specular probe volumes above
      if (b_IrradianceVolumesVisible)
      {
        for (const auto& volume : frame.snapshot.irradianceVolumes)
        {
          m_GizmoRenderer.DrawWireBoxDepthTested(volume.center, volume.halfExtents,
            volume.rotation, glm::vec4(1.0f, 0.55f, 0.15f, 0.55f));
        }
      }

      if (b_HasSelectedEntity)
      {
        glm::vec3 camPos = frame.snapshot.camera.position;
        switch (m_GizmoMode)
        {
          case GizmoMode::Translate: m_GizmoRenderer.DrawTranslateGizmo(m_SelectedEntityPosition, camPos); break;
          case GizmoMode::Rotate:    m_GizmoRenderer.DrawRotateGizmo(m_SelectedEntityPosition, camPos); break;
          case GizmoMode::Scale:     m_GizmoRenderer.DrawScaleGizmo(m_SelectedEntityPosition, camPos); break;
        }
      }

      if (frame.debugDrawGizmos)
        frame.debugDrawGizmos(frame.debugDrawGizmosData);
    }
#endif

    {
#ifdef YA_EDITOR
      // Scoped here and not inside RenderShadowMaps: the probe bakers call it with
      // their own command buffer, which has no query pool of this frame to write into.
      GpuZoneScope shadowZone(&m_GpuProfiler, cmd, "Shadows");
#endif
      RenderShadowMaps(frame, cmd, currentFrame);
    }

    // Between the shadow pass, which has ended its render pass instance, and the graph,
    // which begins the next one: an acceleration structure build may not be recorded
    // inside a render pass, and this is the only gap in the frame that is outside one.
    // It also has to precede the graph because the compute passes that trace the
    // structure live inside it. The instance list comes from the snapshot rather than
    // from the collected draw commands, so it does not wait on them.
    //
    // Only a frame the path tracer serves builds it; the raster path reads neither. A skipped
    // frame leaves its slot as an older build left it, and IsPathTracePassEnabled never reads
    // that slot then, because its first two terms are this same condition.
    if (m_Backend.GetContext().raytracingSupported
      && (IsPathTracingActive() || IsPathTraceView())
      && IsPathTracerAvailable())
    {
      YA_PROFILE_CPU("BuildTlas");
#ifdef YA_EDITOR
      GpuZoneScope tlasZone(&m_GpuProfiler, cmd, "TLAS");
#endif
      // Before the build on purpose: the build's host-write barrier is a global
      // VkMemoryBarrier, so it orders these records against the same compute read it
      // already orders its own instance array and records against, and no second barrier
      // is needed. Same frame, same MaterialManager state, so a material index a TLAS
      // record carries always names a record written here.
      m_MaterialTable.Update(m_Backend.GetContext(), currentFrame,
        frame.assets.Materials(), frame.assets.Textures());

      m_TlasBuilder.Build(m_Backend.GetContext(), cmd, currentFrame, frame.snapshot,
        frame.assets.Meshes(), frame.assets.Materials(), b_PathTraceGlass);
    }

    // Decides what the path tracing pass pushes as its sample index, so it has to precede
    // the graph. It reads the same snapshot digests the shadow cache does, which are
    // already final by now.
    UpdatePathTraceAccumulation(frame);

    // Armed for this one Execute only, and only by a capture request that names a pass.
    bool captureAfterPass = b_CaptureRequested && ArmCaptureAfterPass();
    m_Graph.Execute(cmd, &frame);
    if (captureAfterPass)
      m_Graph.DisarmAfterPass();

#ifdef YA_EDITOR
    RecordSwapchainReadback(cmd, *imageIndex);
#endif

    // Particles are drawn by DrawTransparent, which empties the staging buffers. A frame
    // where neither transparent pass ran drops its submissions here, so they are not carried
    // into the next one.
    m_ParticleStage.clear();
    m_PendingParticleBatches.clear();

    YA_PROFILE_CPU_END(record);

    YA_PROFILE_CPU_BEGIN(present, "Present");
    bool presented = m_Backend.EndFrame(*imageIndex, b_Resized);
    YA_PROFILE_CPU_END(present);

    if (!presented)
    {
      Resize();
    }

    if (b_CaptureRequested)
    {
      YA_PROFILE_CPU("Capture");
      CaptureFrame();
    }

    m_TAAIndex = (m_TAAIndex + 1) % 2;
    m_GlobalFrameIndex++;
  }

#ifdef YA_EDITOR
  // Lowest atlas slot not claimed by any probe, or 0 when the atlas is full.
  // Slot 0 is reserved for the skybox.
  static uint32_t FindFreeAtlasSlot(Scene& scene)
  {
    std::set<uint32_t> usedSlots;
    auto probeView = scene.GetView<ReflectionProbeComponent>();
    for (auto e : probeView)
    {
      auto& probe = probeView.get<ReflectionProbeComponent>(e);
      if (probe.atlasSlot > 0)
        usedSlots.insert(probe.atlasSlot);
    }

    for (uint32_t s = 1; s <= MAX_REFLECTION_PROBES; s++)
    {
      if (!usedSlots.contains(s))
        return s;
    }
    return 0;
  }

  void Render::BakeProbe(entt::entity entity, Scene& scene, AssetManager& assets, bool writeToDisk)
  {
    if (!scene.HasComponent<ReflectionProbeComponent>(entity))
      return;

    vkDeviceWaitIdle(m_Backend.GetContext().device);

    auto& lp = scene.GetComponent<ReflectionProbeComponent>(entity);
    auto& wt = scene.GetWorldTransform(entity);
    glm::vec3 position = glm::vec3(wt.world[3]);
    std::string entityName = scene.GetName(entity);

    uint32_t atlasSlot = lp.atlasSlot;
    if (atlasSlot == 0)
    {
      atlasSlot = FindFreeAtlasSlot(scene);
      if (atlasSlot == 0)
      {
        YA_LOG_ERROR("Render", "No free atlas slots for probe bake");
        return;
      }
    }

    // Compute save path - intermediate bounce passes only refresh the atlas
    std::string pfPath;
    if (writeToDisk)
    {
      std::string basePath = assets.GetBasePath();
      std::string probeDir = basePath + "/Assets/Probes";
      std::filesystem::create_directories(probeDir);

      pfPath = probeDir + "/" + entityName + "_pf.yacm";
    }

    uint32_t captureResolution = std::clamp(lp.resolution,
      BakeLimits::PROBE_MIN_CAPTURE_RESOLUTION,
      BakeLimits::PROBE_MAX_CAPTURE_RESOLUTION);

    SceneSnapshot snapshot;
    LightBuffer lights {};
    BuildBakeSceneSnapshot(snapshot, lights, scene, assets.Meshes(), assets.Materials());

    FrameContext frame {
      .snapshot = snapshot,
      .lights = lights,
      .assets = assets,
      .cubicResources = m_CubicResources,
      .time = 0.0,
      .windowWidth = captureResolution,
      .windowHeight = captureResolution,
    };

    // Defer preview invalidation - can't destroy ImGui descriptor sets during
    // the SwapchainPass because ImGui's draw list may still reference them
    m_PendingInvalidateSlot = atlasSlot;

    m_LightBuffer.SetUp(0, lights);

    // Neighbouring probes stay visible so their light can bounce into this one.
    // Only the probe being rebaked is dropped, otherwise it would feed on its own
    // previous output and drift brighter with every bake.
    ReflectionProbeBuffer bakeProbes {};
    bakeProbes.probeCount = 0;
    for (int i = 0; i < snapshot.probeBuffer.probeCount; i++)
    {
      const auto& src = snapshot.probeBuffer.probes[i];
      if (src.arrayIndex == int(atlasSlot))
        continue;
      bakeProbes.probes[bakeProbes.probeCount++] = src;
    }
    m_ProbeBuffer.SetUp(0, bakeProbes);

    // Frame 0 is the slot OffscreenRenderer binds, and the frame loop rewrites the volume
    // description only when it changes, so the capture is handed the current one here.
    {
      IrradianceVolumeBuffer volumeData = m_VolumeStorage.GetBufferData();
      if (!b_IrradianceVolumesEnabled)
        volumeData.volumeCount = 0;
      m_VolumeStorage.SetUp(0, volumeData);
    }

    // One shadow atlas render per probe: the cascades are fitted around the probe
    // position instead of a camera frustum, so all six cube faces share them.
    // Frame index 0 matches the shadow UBO and atlas view OffscreenRenderer binds.
    {
      VkCommandBuffer shadowCmd = m_Backend.GetCommandBuffer().BeginSingleTimeCommands();
      RenderShadowMaps(frame, shadowCmd, 0, &position);
      m_Backend.GetCommandBuffer().EndSingleTimeCommands(shadowCmd);

      if (m_ShadowManager.IsEnabled())
        YA_LOG_INFO("Render", "Probe '%s': shadow atlas rendered once for all 6 faces",
          entityName.c_str());
    }

    m_ProbeBaker.Bake(m_CubicResources, frame, m_ProbeAtlas,
      position, captureResolution, atlasSlot, pfPath);

    lp.baked = true;
    lp.atlasSlot = atlasSlot;
    if (writeToDisk)
      lp.bakedPrefilterPath = assets.MakeRelative(pfPath);

    YA_LOG_INFO("Render", "Probe '%s' baked -> slot %u", entityName.c_str(), atlasSlot);
  }

  void Render::BakeAllProbes(Scene& scene, AssetManager& assets)
  {
    // Copied to be sorted by capture resolution below.
    std::vector<entt::entity> probes;
    {
      auto probeView = scene.GetView<ReflectionProbeComponent>();
      for (auto e : probeView)
        probes.push_back(e);
    }

    if (probes.empty())
    {
      YA_LOG_WARN("Render", "Bake all probes: scene has no reflection probes");
      return;
    }

    // Grouped by capture resolution: EnsureResolution tears down and rebuilds the
    // whole offscreen graph whenever it changes, so entity order over a scene with
    // mixed resolutions would rebuild it once per probe per bounce.
    std::sort(probes.begin(), probes.end(), [&scene](entt::entity a, entt::entity b)
    {
      return scene.GetComponent<ReflectionProbeComponent>(a).resolution
        < scene.GetComponent<ReflectionProbeComponent>(b).resolution;
    });

    // Atlas slots are handed out once, before the first pass. Reassigning them
    // between passes would move probes inside the atlas and break the neighbour
    // lookups the later bounces are built on.
    for (auto e : probes)
    {
      auto& lp = scene.GetComponent<ReflectionProbeComponent>(e);
      if (lp.atlasSlot != 0)
        continue;

      uint32_t slot = FindFreeAtlasSlot(scene);
      if (slot == 0)
      {
        YA_LOG_WARN("Render", "No free atlas slot for probe '%s' - skipped",
          scene.GetName(e).c_str());
        continue;
      }
      lp.atlasSlot = slot;
    }

    int bounces = std::clamp(m_ProbeBounceCount, MIN_PROBE_BOUNCES, MAX_PROBE_BOUNCES);
    uint32_t probeCount = uint32_t(probes.size());

    YA_LOG_INFO("Render", "Baking all probes: %u probes x %d bounce(s)",
      probeCount, bounces);

    for (int pass = 0; pass < bounces; pass++)
    {
      bool finalPass = (pass == bounces - 1);
      YA_LOG_INFO("Render", "Probe bounce %d/%d", pass + 1, bounces);

      for (auto e : probes)
      {
        auto& lp = scene.GetComponent<ReflectionProbeComponent>(e);
        if (lp.atlasSlot == 0)
          continue;
        BakeProbe(e, scene, assets, finalPass);
      }
    }

    YA_LOG_INFO("Render", "Baking all probes done (%u probes, %d bounce(s))",
      probeCount, bounces);
  }
#endif
}
