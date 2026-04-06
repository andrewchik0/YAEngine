#include "Render.h"

#include <imgui_impl_glfw.h>
#include <ImGui/imgui_impl_vulkan.h>

#include "Assets/AssetManager.h"
#include "Assets/CubeMapManager.h"
#include "ImageBarrier.h"
#include "Utils/Log.h"
#include "SSAOKernel.h"

#include "Utils/Utils.h"
#include "TileCullData.h"

#include <random>

namespace YAEngine
{
  void Render::Init(GLFWwindow* window, const RenderSpecs &specs)
  {
    m_Backend.Init(window, specs);
    auto& ctx = m_Backend.GetContext();

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    uint32_t whitePixel = 0xFFFFFFFF;
    m_NoneTexture.Load(ctx, &whitePixel, 1, 1, 4, VK_FORMAT_R8G8B8A8_SRGB);

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
    m_FrameUniformBuffer.Init(ctx);
    m_LightBuffer.Init(ctx);

    {
      uint32_t tileCountX = (uint32_t(width) + TILE_SIZE - 1) / TILE_SIZE;
      uint32_t tileCountY = (uint32_t(height) + TILE_SIZE - 1) / TILE_SIZE;
      m_TileLightBuffer.Init(ctx, tileCountX, tileCountY);
    }

    // Generate SSAO noise texture (4x4 random tangent-space rotations)
    {
      std::mt19937 rng(42);
      std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

      // 16 pixels, R32G32B32A32_SFLOAT (16 bytes per pixel)
      std::array<glm::vec4, 16> noiseData;
      for (auto& v : noiseData)
      {
        v = glm::vec4(dist(rng), dist(rng), 0.0f, 0.0f);
        v = glm::vec4(glm::normalize(glm::vec2(v.x, v.y)), 0.0f, 0.0f);
      }

      // Convert to half-float manually via uint16 - use R16G16B16A16_SFLOAT
      // VulkanTexture::Load expects raw data with pixelSize per pixel
      // Use R32G32B32A32_SFLOAT for simplicity (16 bytes per pixel)
      m_SSAONoise.Load(ctx, noiseData.data(), 4, 4, 16, VK_FORMAT_R32G32B32A32_SFLOAT);
    }

    // Generate SSAO hemisphere kernel
    {
      std::mt19937 rng(0);
      std::uniform_real_distribution<float> dist(0.0f, 1.0f);

      SSAOKernel kernelData;

      for (int i = 0; i < SSAO_KERNEL_SIZE; i++)
      {
        glm::vec3 sample(
          dist(rng) * 2.0f - 1.0f,
          dist(rng) * 2.0f - 1.0f,
          dist(rng)
        );
        sample = glm::normalize(sample);
        sample *= dist(rng);

        // Accelerating interpolation - bias samples closer to origin
        float scale = float(i) / float(SSAO_KERNEL_SIZE);
        scale = 0.1f + scale * scale * 0.9f;
        sample *= scale;

        kernelData.samples[i] = glm::vec4(sample, 0.0f);
      }

      m_SSAOKernelUBO.Create(ctx, sizeof(SSAOKernel));
      m_SSAOKernelUBO.Update(kernelData);
    }

    m_ShadowManager.Init(ctx);

    SetupRenderGraph(width, height);
    CreateTAAFramebuffers();
    m_Backend.GetSwapChain().CreateFrameBuffers(
      m_Graph.GetPassRenderPass(m_SwapchainPassIndex));
    ClearHistoryBuffers();
    InitPipelines();
    CreateHiZResources();

    m_Backend.InitImGui(window, m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

#ifdef YA_EDITOR
    CreateSceneImGuiDescriptor();
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

  void Render::Destroy()
  {
    vkDeviceWaitIdle(m_Backend.GetContext().device);

    auto& ctx = m_Backend.GetContext();

#ifdef YA_EDITOR
    m_ShaderHotReload.Destroy();
    DestroySceneImGuiDescriptor();
    m_GizmoRenderer.Destroy(ctx);
#endif

    m_ShadowManager.Destroy(ctx);
    m_CubicResources.Destroy(ctx);
    m_NoneCubeMap.Destroy(ctx);
    m_NoneTexture.Destroy(ctx);

    DestroyHiZResources();

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
    for (auto& set : m_SSAOPassDescriptorSets)
      set.Destroy();
    for (auto& set : m_SSAOBlurHPassDescriptorSets)
      set.Destroy();
    for (auto& set : m_SSAOBlurVPassDescriptorSets)
      set.Destroy();
    for (auto& set : m_DeferredLightingDescriptorSets)
      set.Destroy();
    for (auto& set : m_DeferredLightingLightDescriptorSets)
      set.Destroy();
    for (auto& set : m_LightCullInputDescriptorSets)
      set.Destroy();
    m_IBLDescriptorSet.Destroy();

    m_SSAONoise.Destroy(ctx);
    m_SSAOKernelUBO.Destroy(ctx);
    m_InstanceDescriptorSet.Destroy();
    m_InstanceBuffer.Destroy(ctx);
    m_PSOCache.Destroy();
    m_DefaultMaterial.Destroy(ctx);
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

    DestroyHiZResources();

    // Resize graph (recreates managed resources and non-external framebuffers)
    m_Graph.Resize(actualExtent);

    // Recreate Hi-Z per-mip views and descriptor sets
    CreateHiZResources();

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
        ResizeViewport();
      }
    }

    m_ShaderHotReload.Update(frame.time);
#endif

    auto imageIndex = m_Backend.BeginFrame();
    if (!imageIndex)
    {
      Resize();
      return;
    }

    m_Stats = {};

    auto cmd = m_Backend.GetCurrentCommandBuffer();

    SetUpCamera(frame);
    m_FrameUniformBuffer.uniforms.time = (float)frame.time;
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
    m_FrameUniformBuffer.uniforms.ssaoEnabled = b_SSAOEnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.ssrEnabled = b_SSREnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.taaEnabled = b_TAAEnabled ? 1 : 0;
    m_FrameUniformBuffer.uniforms.hizMipCount = static_cast<int>(m_Graph.GetResourceDesc(m_HiZResource).mipLevels);

    // Configure render graph for this frame (TAA ping-pong + swapchain)
    auto historyWrite = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
    auto historyRead = m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;

    m_Graph.SetPassInput(m_TAAPassIndex, 1, historyRead);
    m_Graph.SetPassColorOutput(m_TAAPassIndex, 0, historyWrite);
    m_Graph.SetPassFramebuffer(m_TAAPassIndex, m_TAAFramebuffers[m_TAAIndex]);

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
    m_LightBuffer.SetUp(currentFrame, frame.lights);

    // Update IBL descriptor set when skybox changes
    auto skybox = frame.snapshot.skybox;
    if (skybox && skybox != m_BoundSkybox)
    {
      auto& cubeMap = frame.assets.CubeMaps().GetVulkanCubicTexture(skybox);
      m_IBLDescriptorSet.WriteCombinedImageSampler(0,
        cubeMap.GetIrradianceView(), cubeMap.GetIrradianceSampler());
      m_IBLDescriptorSet.WriteCombinedImageSampler(1,
        cubeMap.GetPrefilterView(), cubeMap.GetPrefilterSampler());
      m_IBLDescriptorSet.WriteCombinedImageSampler(2,
        frame.cubicResources.brdfLut.GetView(), frame.cubicResources.brdfLut.GetSampler());
      m_IBLDescriptorSet.WriteCombinedImageSampler(3,
        cubeMap.GetView(), cubeMap.GetSampler());
      m_BoundSkybox = skybox;
    }

#ifdef YA_EDITOR
    m_GizmoRenderer.Clear();
    if (b_GizmosEnabled)
    {
      for (int i = 0; i < frame.lights.pointLightCount; i++)
      {
        glm::vec3 pos(frame.lights.pointLights[i].positionRadius);
        glm::vec3 col(frame.lights.pointLights[i].colorIntensity);
        m_GizmoRenderer.DrawSprite(pos, 0.5f, 0xf0eb, glm::vec4(col, 0.85f));
      }

      for (int i = 0; i < frame.lights.spotLightCount; i++)
      {
        glm::vec3 pos(frame.lights.spotLights[i].positionRadius);
        glm::vec3 dir(frame.lights.spotLights[i].directionInnerCone);
        glm::vec3 col(frame.lights.spotLights[i].colorOuterCone);
        float outerCos = std::clamp(frame.lights.spotLights[i].colorOuterCone.w, -1.0f, 1.0f);
        float angle = std::acos(outerCos);
        m_GizmoRenderer.DrawSprite(pos, 0.5f, 0xf0eb, glm::vec4(col, 0.85f));
        m_GizmoRenderer.DrawWireCone(pos, dir, 2.0f, angle, glm::vec4(col, 0.85f));
      }

      glm::vec3 dirLightDir(frame.lights.directional.directionIntensity);
      glm::vec3 dirCol(frame.lights.directional.colorPad);
      glm::vec3 dirLightPos = frame.snapshot.directionalShadow.position;
      float dirIntensity = frame.lights.directional.directionIntensity.w;
      if (dirIntensity > 0.0f)
      {
        m_GizmoRenderer.DrawSprite(dirLightPos, 0.5f, 0xf185, glm::vec4(dirCol, 0.85f));
        m_GizmoRenderer.DrawArrow(dirLightPos, dirLightDir, 3.0f, glm::vec4(dirCol, 0.85f));
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
    }
#endif

    // Render shadow maps before main passes
    RenderShadowMaps(frame);

    // Execute all passes
    m_Graph.Execute(cmd, &frame);

    if (!m_Backend.EndFrame(*imageIndex, b_Resized))
    {
      Resize();
    }
    m_TAAIndex = (m_TAAIndex + 1) % 2;
    m_GlobalFrameIndex++;
  }
}
