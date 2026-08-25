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
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
      m_Backend.GetCommandBuffer().EndSingleTimeCommands(cmd);
      m_NoneCubeMap.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
    m_ProbeAtlas.Init(ctx);
    m_ProbeBuffer.Init(ctx);
    m_VolumeStorage.Init(ctx);

    SetupRenderGraph(width, height);
    CreateTAAFramebuffers();
    m_Backend.GetSwapChain().CreateFrameBuffers(
      m_Graph.GetPassRenderPass(m_SwapchainPassIndex));
    ClearHistoryBuffers();
    InitPipelines();
    CreateHiZResources();
    CreateGTAOResources();
    CreateBloomResources();

    m_Backend.InitImGui(window, m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

#ifdef YA_EDITOR
    m_ProbeBaker.Init(*this, BakeLimits::PROBE_DEFAULT_CAPTURE_RESOLUTION);
    m_VolumeBaker.Init(*this, BakeLimits::VOLUME_DEFAULT_CAPTURE_RESOLUTION);
    CreateSceneImGuiDescriptor();
    CreatePickResources();
    m_ViewportWidth = width;
    m_ViewportHeight = height;
    m_PendingViewportWidth = width;
    m_PendingViewportHeight = height;
#endif

    m_CubicResources.Init(ctx);

    InitFrameCapture();

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

    // Frame 0 is what OffscreenRenderer binds, and it is only refreshed by Draw
    // for the frame currently in flight - prime it so a bake right after a scene
    // load does not read a stale description.
    m_VolumeStorage.SetUp(0, m_VolumeStorage.GetBufferData());
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
    m_VolumeBaker.Destroy();
    DestroySceneImGuiDescriptor();
    DestroyPickResources();
    m_GizmoRenderer.Destroy(ctx);
#endif

    m_ShadowManager.Destroy(ctx);
    m_ProbeAtlas.Destroy(ctx);
    m_ProbeBuffer.Destroy(ctx);
    m_VolumeStorage.Destroy(ctx);
    m_CubicResources.Destroy(ctx);
    m_NoneCubeMap.Destroy(ctx);
    m_NoneTexture.Destroy(ctx);

    DestroyBloomResources();
    DestroyHiZResources();
    DestroyGTAOResources();

    for (auto& fb : m_TAAFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    for (auto& fb : m_TransparentFramebuffers)
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
    for (auto& set : m_GTAOPassDescriptorSets)
      set.Destroy();
    for (auto& set : m_GTAODenoiseDescriptorSets)
      set.Destroy();
    for (auto& set : m_DeferredLightingDescriptorSets)
      set.Destroy();
    for (auto& set : m_DeferredLightingLightDescriptorSets)
      set.Destroy();
    for (auto& set : m_LightCullInputDescriptorSets)
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

  void Render::Resize()
  {
    b_Resized = false;

    // Recreate swapchain first to get actual surface dimensions
    m_Backend.GetSwapChain().Recreate(
      m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

#ifdef YA_EDITOR
    // In editor mode, graph extent = viewport size (independent of window size).
    // Only swapchain is recreated here; viewport resize handled by ResizeViewport().
#else
    auto& ctx = m_Backend.GetContext();
    auto actualExtent = m_Backend.GetSwapChain().GetExt();

    // Wait for all GPU work to complete before destroying resources
    vkDeviceWaitIdle(ctx.device);

    DestroyBloomResources();
    DestroyHiZResources();
    DestroyGTAOResources();

    // Resize graph (recreates managed resources and non-external framebuffers)
    m_Graph.Resize(actualExtent);

    // Recreate Hi-Z per-mip views and descriptor sets
    CreateHiZResources();
    CreateGTAOResources();
    CreateBloomResources();

    // Resize tile light buffer and update descriptor sets that reference it
    {
      uint32_t tileCountX = (actualExtent.width + TILE_SIZE - 1) / TILE_SIZE;
      uint32_t tileCountY = (actualExtent.height + TILE_SIZE - 1) / TILE_SIZE;
      m_TileLightBuffer.Resize(ctx, tileCountX, tileCountY);
      VkDeviceSize tileBufferSize = tileCountX * tileCountY * sizeof(TileData);
      for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
      {
        m_DeferredLightingLightDescriptorSets[i].WriteStorageBuffer(1,
          m_TileLightBuffer.GetBuffer(uint32_t(i)), tileBufferSize);
      }
    }

    // Recreate TAA external framebuffers
    for (auto& fb : m_TAAFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    for (auto& fb : m_TransparentFramebuffers)
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
#endif
  }

  void Render::Draw(FrameContext& frame)
  {
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

    if (b_ResetTAAPending)
    {
      vkDeviceWaitIdle(m_Backend.GetContext().device);
      ClearHistoryBuffers();
      b_ResetTAAPending = false;
    }
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
    m_FrameUniformBuffer.uniforms.currentTexture = m_CurrentTexture;
#ifdef YA_EDITOR
    m_FrameUniformBuffer.uniforms.screenWidth = int(m_ViewportWidth);
    m_FrameUniformBuffer.uniforms.screenHeight = int(m_ViewportHeight);
#else
    m_FrameUniformBuffer.uniforms.screenWidth = int(frame.windowWidth);
    m_FrameUniformBuffer.uniforms.screenHeight = int(frame.windowHeight);
#endif
    m_FrameUniformBuffer.uniforms.tileCountX = (m_FrameUniformBuffer.uniforms.screenWidth + TILE_SIZE - 1) / TILE_SIZE;
    m_FrameUniformBuffer.uniforms.tileCountY = (m_FrameUniformBuffer.uniforms.screenHeight + TILE_SIZE - 1) / TILE_SIZE;
    m_FrameUniformBuffer.uniforms.aoEnabled = b_AOEnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.aoStrength = m_AOStrength;
    m_FrameUniformBuffer.uniforms.aoSpecularStrength = m_AOSpecularStrength;
    m_FrameUniformBuffer.uniforms.aoMultiBounce = m_AOMultiBounce;
    m_FrameUniformBuffer.uniforms.ssrEnabled = b_SSREnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.ssrIntensity = m_SSRIntensity;
    m_FrameUniformBuffer.uniforms.taaEnabled = b_TAAEnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.taaClampSigma = m_TAAClampSigma;

    // The indirect lighting debug views must reach the screen untouched. SSR and TAA
    // both sit between the lighting pass and tone mapping, and both already pass
    // through when their flag is zero, so disabling them here needs no extra plumbing.
    // The camera jitter is handled in SetUpCamera, which runs before this.
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

    // Configure render graph for this frame (TAA ping-pong + swapchain)
    auto historyWrite = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
    auto historyRead = m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;

    m_Graph.SetPassInput(m_TAAPassIndex, 1, historyRead);
    m_Graph.SetPassColorOutput(m_TAAPassIndex, 0, historyWrite);
    m_Graph.SetPassFramebuffer(m_TAAPassIndex, m_TAAFramebuffers[m_TAAIndex]);

    // Forward transparent renders into the same TAA history target written this frame.
    m_Graph.SetPassColorOutput(m_ForwardTransparentPassIndex, 0, historyWrite);
    m_Graph.SetPassFramebuffer(m_ForwardTransparentPassIndex, m_TransparentFramebuffers[m_TAAIndex]);

    m_Graph.SetPassInput(m_HistogramPassIndex, 0, historyWrite);

#ifdef YA_EDITOR
    m_Graph.SetPassInput(m_SceneComposePassIndex, 0, historyWrite);

    // SwapchainPass renders ImGui at full window size, override extent
    auto swapExtent = m_Backend.GetSwapChain().GetExt();
    m_Graph.SetPassExtent(m_SwapchainPassIndex, swapExtent);
#else
    m_Graph.SetPassInput(m_SwapchainPassIndex, 0, historyWrite);
#endif
    m_Graph.SetPassFramebuffer(m_SwapchainPassIndex,
      m_Backend.GetSwapChain().GetFramebuffer(*imageIndex));

    // Upload per-frame data before executing passes
    auto currentFrame = m_Backend.GetCurrentFrameIndex();
    m_FrameUniformBuffer.SetUp(currentFrame);
    // Reads back the projection and viewport SetUpCamera just wrote, so it has to run after it.
    UpdateGTAOConstants(currentFrame);
    m_LightBuffer.SetUp(currentFrame, frame.lights);

    // Update IBL when skybox changes: upload to atlas slot 0 + update display cubemap
    auto skybox = frame.snapshot.skybox;
    if (skybox && skybox != m_BoundSkybox)
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
      m_BoundSkybox = skybox;
    }

    // Upload probe SSBO. The descriptor is written once at init and the buffers
    // are never recreated, so there is nothing to rewrite here per frame.
    m_ProbeBuffer.SetUp(currentFrame, frame.snapshot.probeBuffer);

    // Upload irradiance volume UBO. Turning volumes off is the same as having
    // none - the shader then takes skybox irradiance from atlas slot 0 everywhere.
    // Unlike the probe buffer this is not rebuilt from the snapshot, so it only
    // changes on upload or on the toggle: one dirty frame per frame in flight.
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

      // Reflection probe influence volumes
      if (b_ProbeVolumesVisible)
      {
        for (int i = 0; i < frame.snapshot.probeBuffer.probeCount; i++)
        {
          auto& probe = frame.snapshot.probeBuffer.probes[i];
          glm::vec3 pos(probe.positionShape);
          float shape = probe.positionShape.w;
          glm::vec3 extents(probe.extentsFade);
          glm::vec4 col(0.2f, 0.7f, 0.9f, 0.5f);

          if (shape < 0.5f)
          {
            m_GizmoRenderer.DrawWireSphereDepthTested(pos, extents.x, col);
          }
          else
          {
            glm::quat rot(probe.orientation.w, probe.orientation.x,
              probe.orientation.y, probe.orientation.z);
            m_GizmoRenderer.DrawWireBoxDepthTested(pos, extents, rot, col);
          }
        }
      }

      // Irradiance volume bounds - warm orange to separate them from the cool
      // blue of the specular probe volumes above
      if (b_IrradianceVolumesVisible)
      {
        for (const auto& volume : frame.snapshot.irradianceVolumes)
        {
          m_GizmoRenderer.DrawWireBoxDepthTested(volume.center, volume.grid.halfExtents,
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

    // Render shadow maps before main passes
    {
#ifdef YA_EDITOR
      // Scoped here and not inside RenderShadowMaps: the probe bakers call it with
      // their own command buffer, which has no query pool of this frame to write into.
      GpuZoneScope shadowZone(&m_GpuProfiler, cmd, "Shadows");
#endif
      RenderShadowMaps(frame, cmd, currentFrame);
    }

    // Execute all passes
    m_Graph.Execute(cmd, &frame);

    YA_PROFILE_CPU_END(record);

    YA_PROFILE_CPU_BEGIN(present, "Present");
    bool presented = m_Backend.EndFrame(*imageIndex, b_Resized);
    YA_PROFILE_CPU_END(present);

    if (!presented)
    {
      Resize();
    }

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

    // Find next free atlas slot (1..MAX_REFLECTION_PROBES, slot 0 = skybox)
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

    // Build temporary scene snapshot + light buffer
    SceneSnapshot snapshot;
    LightBuffer lights {};
    BuildSceneSnapshot(snapshot, lights, scene, assets.Meshes(), assets.Materials());

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

    // Upload light data to LightStorageBuffer so OffscreenRenderer can use it
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

    // Irradiance volumes are switched off for the capture. They are fed by the
    // very probes being rebaked, so leaving them on would close a feedback loop:
    // every bake pass would read the previous one back in and drift brighter.
    // Frame 0 is the slot OffscreenRenderer binds.
    {
      IrradianceVolumeBuffer noVolumes {};
      noVolumes.atlasInvSize = m_VolumeStorage.GetBufferData().atlasInvSize;
      noVolumes.volumeCount = 0;
      m_VolumeStorage.SetUp(0, noVolumes);
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

    // Update component
    lp.baked = true;
    lp.atlasSlot = atlasSlot;
    if (writeToDisk)
      lp.bakedPrefilterPath = assets.MakeRelative(pfPath);

    // Restore the real volume description for the next frame
    m_VolumeStorage.SetUp(0, m_VolumeStorage.GetBufferData());

    YA_LOG_INFO("Render", "Probe '%s' baked -> slot %u", entityName.c_str(), atlasSlot);
  }

  void Render::BakeAllProbes(Scene& scene, AssetManager& assets)
  {
    // Snapshot the entity list first - BuildSceneSnapshot inside BakeProbe can add
    // components and invalidate a live view
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
