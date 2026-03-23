#include "Render.h"

#include <imgui_impl_glfw.h>
#include <ImGui/imgui_impl_vulkan.h>

#include "Application.h"
#include "ImageBarrier.h"
#include "VulkanVertexBuffer.h"

#include "Utils/Utils.h"

namespace YAEngine
{
  void Render::SetupRenderGraph(uint32_t width, uint32_t height)
  {
    auto& ctx = m_Backend.GetContext();
    auto swapFormat = m_Backend.GetSwapChain().GetFormat();

    m_Graph.Init(ctx, {width, height});

    // --- Resources ---
    m_MainColor = m_Graph.CreateResource({
      .name = "mainColor",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    m_MainNormals = m_Graph.CreateResource({
      .name = "mainNormals",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    m_MainDepth = m_Graph.CreateResource({
      .name = "mainDepth",
      .format = VK_FORMAT_D32_SFLOAT,
      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT
    });
    m_MainMaterial = m_Graph.CreateResource({
      .name = "mainMaterial",
      .format = VK_FORMAT_R8G8_UNORM
    });
    m_MainAlbedo = m_Graph.CreateResource({
      .name = "mainAlbedo",
      .format = VK_FORMAT_R8G8B8A8_UNORM
    });
    m_MainVelocity = m_Graph.CreateResource({
      .name = "mainVelocity",
      .format = VK_FORMAT_R16G16_SFLOAT
    });
    m_SSRColor = m_Graph.CreateResource({
      .name = "ssrColor",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    m_TAAHistory0 = m_Graph.CreateResource({
      .name = "taaHistory0",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    });
    m_TAAHistory1 = m_Graph.CreateResource({
      .name = "taaHistory1",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    });

    // --- Passes ---
    m_MainPassIndex = m_Graph.AddPass({
      .name = "MainPass",
      .inputs = {},
      .colorOutputs = {m_MainColor, m_MainNormals, m_MainMaterial, m_MainAlbedo, m_MainVelocity},
      .depthOutput = m_MainDepth,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* app = static_cast<Application*>(ctx.userData);
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        m_PerFrameData.SetUp(currentFrame);

        m_ForwardPipeline.Bind(ctx.cmd);
        m_ForwardPipeline.BindDescriptorSets(ctx.cmd, {m_PerFrameData.GetDescriptorSet(currentFrame)}, 0);
        m_ForwardPipelineDoubleSided.Bind(ctx.cmd);
        m_ForwardPipelineDoubleSided.BindDescriptorSets(ctx.cmd, {m_PerFrameData.GetDescriptorSet(currentFrame)}, 0);
        SetViewportAndScissor();

        DrawMeshes(app);
      }
    });

    m_SSRPassIndex = m_Graph.AddPass({
      .name = "SSRPass",
      .inputs = {m_MainColor, m_MainDepth, m_MainNormals, m_MainMaterial, m_MainAlbedo, m_MainVelocity},
      .colorOutputs = {m_SSRColor},
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        auto& mainColor = m_Graph.GetResource(m_MainColor);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);
        auto& mainNormals = m_Graph.GetResource(m_MainNormals);
        auto& mainMaterial = m_Graph.GetResource(m_MainMaterial);
        auto& mainAlbedo = m_Graph.GetResource(m_MainAlbedo);

        m_SSRPipeline.Bind(ctx.cmd);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          mainColor.GetView(), mainColor.GetSampler(), mainColor.GetLayout());
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          mainDepth.GetView(), mainDepth.GetSampler(), mainDepth.GetLayout());
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          mainNormals.GetView(), mainNormals.GetSampler(), mainNormals.GetLayout());
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          mainMaterial.GetView(), mainMaterial.GetSampler(), mainMaterial.GetLayout());
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          mainAlbedo.GetView(), mainAlbedo.GetSampler(), mainAlbedo.GetLayout());

        m_SSRPipeline.BindDescriptorSets(ctx.cmd, {m_PerFrameData.GetDescriptorSet(currentFrame)}, 0);
        m_SSRPipeline.BindDescriptorSets(ctx.cmd, {m_SSRPassDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad();
      }
    });

    m_TAAPassIndex = m_Graph.AddPass({
      .name = "TAAPass",
      .inputs = {m_SSRColor, m_TAAHistory1, m_MainVelocity},
      .colorOutputs = {m_TAAHistory0},
      .externalFramebuffer = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto& ssrColor = m_Graph.GetResource(m_SSRColor);
        auto historyReadHandle = m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;
        auto& historyPrev = m_Graph.GetResource(historyReadHandle);
        auto& velocity = m_Graph.GetResource(m_MainVelocity);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        m_TAAPipeline.Bind(ctx.cmd);
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          ssrColor.GetView(), ssrColor.GetSampler(), ssrColor.GetLayout());
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          historyPrev.GetView(), historyPrev.GetSampler(), historyPrev.GetLayout());
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          velocity.GetView(), velocity.GetSampler(), velocity.GetLayout());
        m_TAAPipeline.BindDescriptorSets(ctx.cmd, {m_PerFrameData.GetDescriptorSet(currentFrame)}, 0);
        m_TAAPipeline.BindDescriptorSets(ctx.cmd, {m_TAADescriptorSets[currentFrame].Get()}, 1);
        DrawQuad();
      }
    });

    m_SwapchainPassIndex = m_Graph.AddPass({
      .name = "SwapchainPass",
      .inputs = {m_TAAHistory0},
      .colorOutputs = {},
      .externalFramebuffer = true,
      .externalFormat = swapFormat,
      .finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* app = static_cast<Application*>(ctx.userData);
        SetViewportAndScissor();

        auto historyWriteHandle = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
        auto& historyCurrent = m_Graph.GetResource(historyWriteHandle);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        m_QuadPipeline.Bind(ctx.cmd);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          historyCurrent.GetView(), historyCurrent.GetSampler(), historyCurrent.GetLayout());
        m_QuadPipeline.BindDescriptorSets(ctx.cmd, {m_PerFrameData.GetDescriptorSet(currentFrame)}, 0);
        m_QuadPipeline.BindDescriptorSets(ctx.cmd, {m_SwapChainDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app->RenderUI();
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx.cmd);
      }
    });

    m_Graph.Compile();
  }

  void Render::CreateTAAFramebuffers()
  {
    auto& ctx = m_Backend.GetContext();

    // Create depth image shared by both TAA framebuffers
    ImageDesc depthDesc;
    depthDesc.width = m_Graph.GetExtent().width;
    depthDesc.height = m_Graph.GetExtent().height;
    depthDesc.format = VK_FORMAT_D32_SFLOAT;
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthDesc.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    m_TAADepth.Init(ctx, depthDesc);

    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);

    for (uint32_t i = 0; i < 2; i++)
    {
      RGHandle historyHandle = (i == 0) ? m_TAAHistory0 : m_TAAHistory1;

      VkImageView views[2] = {
        m_Graph.GetResource(historyHandle).GetView(),
        m_TAADepth.GetView()
      };

      VkFramebufferCreateInfo fbInfo{};
      fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbInfo.renderPass = taaRP;
      fbInfo.attachmentCount = 2;
      fbInfo.pAttachments = views;
      fbInfo.width = m_Graph.GetExtent().width;
      fbInfo.height = m_Graph.GetExtent().height;
      fbInfo.layers = 1;

      if (vkCreateFramebuffer(ctx.device, &fbInfo, nullptr, &m_TAAFramebuffers[i]) != VK_SUCCESS)
      {
        throw std::runtime_error("Failed to create TAA framebuffer!");
      }
    }
  }

  void Render::ClearHistoryBuffers()
  {
    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);
    auto extent = m_Backend.GetSwapChain().GetExt();

    auto cmd = m_Backend.GetCommandBuffer().BeginSingleTimeCommands();

    // Transition depth once
    TransitionImageLayout(cmd, m_TAADepth.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_DEPTH_BIT);

    for (uint32_t i = 0; i < 2; i++)
    {
      auto handle = (i == 0) ? m_TAAHistory0 : m_TAAHistory1;
      auto& image = m_Graph.GetResource(handle);

      // Render pass with loadOp=CLEAR will clear the contents
      // initialLayout=UNDEFINED is fine here since we don't need previous contents
      VkClearValue clearValues[2] = {};
      clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
      clearValues[1].depthStencil = {1.0f, 0};

      VkRenderPassBeginInfo rpBegin{};
      rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpBegin.renderPass = taaRP;
      rpBegin.framebuffer = m_TAAFramebuffers[i];
      rpBegin.renderArea.extent = extent;
      rpBegin.clearValueCount = 2;
      rpBegin.pClearValues = clearValues;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdEndRenderPass(cmd);

      // After render pass with finalLayout=COLOR_ATTACHMENT_OPTIMAL,
      // transition to SHADER_READ_ONLY for first frame's use
      TransitionImageLayout(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      image.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      m_Graph.SetResourceLayout(handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    m_Backend.GetCommandBuffer().EndSingleTimeCommands(cmd);
  }

  void Render::Init(GLFWwindow* window, const RenderSpecs &specs)
  {
    m_Backend.Init(window, specs);
    auto& ctx = m_Backend.GetContext();

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    uint32_t whitePixel = 0xFFFFFFFF;
    m_NoneTexture.Load(ctx, &whitePixel, 1, 1, 4, VK_FORMAT_R8G8B8A8_SRGB);

    m_DefaultMaterial.Init(ctx, m_NoneTexture);
    m_PerFrameData.Init(ctx);

    // Setup and compile render graph
    SetupRenderGraph(width, height);

    // Create TAA external framebuffers (ping-pong)
    CreateTAAFramebuffers();

    // Create swapchain framebuffers using graph's render pass
    m_Backend.GetSwapChain().CreateFrameBuffers(
      m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

    // Clear TAA history buffers
    ClearHistoryBuffers();

    // Init pipelines using graph's render passes
    InitPipelines();

    m_Backend.InitImGui(window, m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

    m_CubicResources.Init(ctx);
    m_SkyBox.Init(ctx, m_Graph.GetPassRenderPass(m_MainPassIndex));

    vkDeviceWaitIdle(ctx.device);
  }

  void Render::Destroy()
  {
    vkDeviceWaitIdle(m_Backend.GetContext().device);

    auto& ctx = m_Backend.GetContext();

    m_SkyBox.Destroy();
    m_CubicResources.Destroy(ctx);
    m_NoneTexture.Destroy(ctx);

    // Destroy TAA external framebuffers
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
    {
      set.Destroy();
    }
    for (auto& set : m_TAADescriptorSets)
    {
      set.Destroy();
    }
    for (auto& set : m_SSRPassDescriptorSets)
    {
      set.Destroy();
    }
    m_SSRPipeline.Destroy();
    m_ForwardPipeline.Destroy();
    m_ForwardPipelineDoubleSided.Destroy();
    m_ForwardPipelineInstanced.Destroy();
    m_ForwardPipelineDoubleSidedInstanced.Destroy();
    m_ForwardPipelineNoShading.Destroy();
    m_InstanceDescriptorSet.Destroy();
    m_LightsDescriptorSet.Destroy();
    m_LightsBuffer.Destroy(ctx);
    m_InstanceBuffer.Destroy(ctx);
    m_QuadPipeline.Destroy();
    m_TAAPipeline.Destroy();
    m_DefaultMaterial.Destroy(ctx);
    m_PerFrameData.Destroy(ctx);

    m_Graph.Destroy();
    m_Backend.Destroy();
  }

  void Render::Resize()
  {
    b_Resized = false;
    auto& ctx = m_Backend.GetContext();

    // Recreate swapchain first to get actual surface dimensions
    m_Backend.GetSwapChain().Recreate(
      m_Graph.GetPassRenderPass(m_SwapchainPassIndex));

    // Use actual swapchain extent for everything
    auto actualExtent = m_Backend.GetSwapChain().GetExt();

    // Resize graph (recreates managed resources and non-external framebuffers)
    m_Graph.Resize(actualExtent);

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

    // Clear history buffers
    ClearHistoryBuffers();
  }

  void Render::Draw(Application *app)
  {
    auto imageIndex = m_Backend.BeginFrame();
    if (!imageIndex)
    {
      Resize();
      return;
    }

    auto cmd = m_Backend.GetCurrentCommandBuffer();

    SetUpCamera(app);
    m_PerFrameData.ubo.time = (float)app->GetTimer().GetTime();
    m_PerFrameData.ubo.gamma = m_Gamma;
    m_PerFrameData.ubo.exposure = m_Exposure;
    m_PerFrameData.ubo.currentTexture = m_CurrentTexture;
    m_PerFrameData.ubo.screenWidth = int(app->GetWindow().GetWidth());
    m_PerFrameData.ubo.screenHeight = int(app->GetWindow().GetHeight());
    m_LightsBuffer.Update(0, &m_Lights, sizeof(Light) * MAX_LIGHTS + sizeof(int));

    // Configure render graph for this frame (TAA ping-pong + swapchain)
    auto historyWrite = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;
    auto historyRead = m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;

    m_Graph.SetPassInput(m_TAAPassIndex, 1, historyRead);
    m_Graph.SetPassColorOutput(m_TAAPassIndex, 0, historyWrite);
    m_Graph.SetPassFramebuffer(m_TAAPassIndex, m_TAAFramebuffers[m_TAAIndex]);

    m_Graph.SetPassInput(m_SwapchainPassIndex, 0, historyWrite);
    m_Graph.SetPassFramebuffer(m_SwapchainPassIndex,
      m_Backend.GetSwapChain().GetFramebuffer(*imageIndex));

    // Execute all passes
    m_Graph.Execute(cmd, app);

    if (!m_Backend.EndFrame(*imageIndex, b_Resized))
    {
      Resize();
    }
    m_TAAIndex = (m_TAAIndex + 1) % 2;
    m_GlobalFrameIndex++;
  }

  void Render::SetViewportAndScissor()
  {
    auto cmd = m_Backend.GetCurrentCommandBuffer();

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) m_Backend.GetSwapChain().GetExt().width;
    viewport.height = (float) m_Backend.GetSwapChain().GetExt().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_Backend.GetSwapChain().GetExt();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

  }

  void Render::DrawMeshes(Application* app)
  {
    auto currentFrame = m_Backend.GetCurrentFrameIndex();
    auto cmd = m_Backend.GetCurrentCommandBuffer();

    auto view = app->GetScene().GetView<MeshComponent, TransformComponent, MaterialComponent>();

    view.each([&](MeshComponent mesh, TransformComponent transform, MaterialComponent material)
    {
      if (!mesh.shouldRender) return;

      auto& currentPipeline = mesh.noShading ? m_ForwardPipelineNoShading :
        app->GetAssetManager().Meshes().Get(mesh.asset).instanceData == nullptr
          ? mesh.doubleSided ? m_ForwardPipelineDoubleSided : m_ForwardPipeline
          : mesh.doubleSided ? m_ForwardPipelineDoubleSidedInstanced : m_ForwardPipelineInstanced;

      currentPipeline.Bind(cmd);

      struct _
      {
        glm::mat4 model;
        int offset = 0;
      } data;
      data.model = transform.world;
      data.offset = app->GetAssetManager().Meshes().Get(mesh.asset).offset / sizeof(glm::mat4);
      currentPipeline.PushConstants(cmd, &data);

      currentPipeline.BindDescriptorSets(cmd, {app->GetAssetManager().Materials().Get(material.asset).m_VulkanMaterial.GetDescriptorSet(currentFrame)}, 1);
      currentPipeline.BindDescriptorSets(cmd, { m_LightsDescriptorSet.Get() }, 2);
      uint32_t instanceCount = 1;
      if (app->GetAssetManager().Meshes().Get(mesh.asset).instanceData != nullptr)
      {
        instanceCount = uint32_t(app->GetAssetManager().Meshes().Get(mesh.asset).instanceData->size());
        currentPipeline.BindDescriptorSets(cmd, { m_InstanceDescriptorSet.Get() }, 3);
        m_InstanceBuffer.Update(app->GetAssetManager().Meshes().Get(mesh.asset).offset, app->GetAssetManager().Meshes().Get(mesh.asset).instanceData->data(), uint32_t(instanceCount * sizeof(glm::mat4)));
      }

      auto& mat = app->GetAssetManager().Materials().Get(material.asset);
      mat.cubemap = app->GetScene().GetSkybox();
      app->GetAssetManager().Materials().Get(material.asset).m_VulkanMaterial.Bind(app, mat, currentFrame, m_NoneTexture);

      app->GetAssetManager().Meshes().Get(mesh.asset).vertexBuffer.Draw(cmd, instanceCount);
    });

    if (!app->GetScene().HasComponent<CameraComponent>(app->GetScene().GetActiveCamera())) return;
    auto& camCamera = app->GetScene().GetComponent<CameraComponent>(app->GetScene().GetActiveCamera());
    auto& camTransform = app->GetScene().GetComponent<TransformComponent>(app->GetScene().GetActiveCamera());
    glm::mat4 camDir = glm::mat4_cast(camTransform.rotation);
    if (app->GetScene().GetSkybox())
      m_SkyBox.Draw(currentFrame, &app->GetAssetManager().CubeMaps().Get(app->GetScene().GetSkybox()).m_CubeTexture, cmd, camDir, m_PerFrameData.ubo.proj, m_CubicResources);
  }

  void Render::SetUpCamera(Application* app)
  {
    if (!app->GetScene().HasComponent<CameraComponent>(app->GetScene().GetActiveCamera()))
      return;

    auto& transform = app->GetScene().GetComponent<TransformComponent>(app->GetScene().GetActiveCamera());
    auto& camera    = app->GetScene().GetComponent<CameraComponent>(app->GetScene().GetActiveCamera());

    glm::mat4 world =
      glm::translate(glm::mat4(1.0f), transform.position) *
      glm::mat4_cast(transform.rotation);

    glm::mat4 view = glm::inverse(world);

    glm::mat4 proj = glm::perspective(
      camera.fov,
      camera.aspectRatio,
      camera.nearPlane,
      camera.farPlane
    );

    proj[1][1] *= -1.0f;

    // Store previous frame matrices (unjittered) for reprojection
    m_PerFrameData.ubo.prevView = m_PrevView;
    m_PerFrameData.ubo.prevProj = m_PrevProj;
    m_PrevView = view;
    m_PrevProj = proj;

    glm::vec2 jitter = GetTAAJitter(m_GlobalFrameIndex);

    jitter.x /= float(app->GetWindow().GetWidth());
    jitter.y /= float(app->GetWindow().GetHeight());

    proj[2][0] += jitter.x;
    proj[2][1] += jitter.y;

    m_PerFrameData.ubo.view = view;
    m_PerFrameData.ubo.proj = proj;
    m_PerFrameData.ubo.invProj = glm::inverse(proj);
    m_PerFrameData.ubo.nearPlane = camera.nearPlane;
    m_PerFrameData.ubo.farPlane = camera.farPlane;
    m_PerFrameData.ubo.cameraPosition = transform.position;
    m_PerFrameData.ubo.cameraDirection = glm::normalize(-glm::vec3(world[2]));
    m_PerFrameData.ubo.fov = camera.fov;
  }

  void Render::InitPipelines()
  {
    auto& ctx = m_Backend.GetContext();
    auto pipelineCache = ctx.pipelineCache;

    SetDescription instanceDesc = {
      .set = 3,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
      }
    };
    m_InstanceDescriptorSet.Init(ctx, instanceDesc);
    constexpr auto MAX_INSTANCES = 10000;
    m_InstanceBuffer.Create(ctx, MAX_INSTANCES * sizeof(glm::mat4));
    m_InstanceDescriptorSet.WriteStorageBuffer(0, m_InstanceBuffer.Get(), MAX_INSTANCES * sizeof(glm::mat4));

    SetDescription lightsDesc = {
      .set = 2,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT }
      }
    };
    m_LightsDescriptorSet.Init(ctx, lightsDesc);
    m_LightsBuffer.Create(ctx, MAX_LIGHTS * sizeof(Light) + sizeof(int));
    m_LightsDescriptorSet.WriteStorageBuffer(0, m_LightsBuffer.Get(), MAX_LIGHTS * sizeof(Light) + sizeof(int));

    // Forward pipelines — use graph's main pass render pass
    VkRenderPass mainRP = m_Graph.GetPassRenderPass(m_MainPassIndex);

    PipelineCreateInfo forwardInfo = {
      .fragmentShaderFile = "shader.frag",
      .vertexShaderFile = "shader.vert",
      .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
      .colorAttachmentCount = 5,
      .vertexInputFormat = "f3f2f3f4",
      .sets = std::vector({
        m_PerFrameData.GetLayout(),
        m_DefaultMaterial.GetLayout(),
        m_LightsDescriptorSet.GetLayout(),
      })
    };
    m_ForwardPipeline.Init(ctx.device, mainRP, forwardInfo, pipelineCache);
    forwardInfo.doubleSided = true;
    m_ForwardPipelineDoubleSided.Init(ctx.device, mainRP, forwardInfo, pipelineCache);

    forwardInfo.sets.push_back(m_InstanceDescriptorSet.GetLayout());
    forwardInfo.vertexShaderFile = "instanced.vert",
    m_ForwardPipelineDoubleSidedInstanced.Init(ctx.device, mainRP, forwardInfo, pipelineCache);
    forwardInfo.doubleSided = false;
    m_ForwardPipelineInstanced.Init(ctx.device, mainRP, forwardInfo, pipelineCache);

    forwardInfo.sets.pop_back();
    forwardInfo.doubleSided = true;
    forwardInfo.fragmentShaderFile = "no_shading.frag";
    forwardInfo.vertexShaderFile = "shader.vert";
    m_ForwardPipelineNoShading.Init(ctx.device, mainRP, forwardInfo, pipelineCache);

    // Swapchain descriptor sets (set 1 — set 0 is PerFrameData)
    SetDescription desc = {
      .set = 1,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };
    m_SwapChainDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_SwapChainDescriptorSets[i].Init(ctx, desc);
    }

    // TAA descriptor sets (set 1 — set 0 is PerFrameData)
    SetDescription taaDesc = {
      .set = 1,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };
    m_TAADescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      m_TAADescriptorSets[i].Init(ctx, taaDesc);
    }

    // Quad pipeline — use swapchain render pass
    VkRenderPass swapRP = m_Graph.GetPassRenderPass(m_SwapchainPassIndex);

    PipelineCreateInfo quadInfo = {
      .fragmentShaderFile = "quad.frag",
      .vertexShaderFile = "quad.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_PerFrameData.GetLayout(),
        m_SwapChainDescriptorSets[0].GetLayout(),
      })
    };

    m_QuadPipeline.Init(ctx.device, swapRP, quadInfo, pipelineCache);

    // TAA pipeline — use TAA render pass (compatible format)
    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);
    quadInfo.fragmentShaderFile = "taa.frag";
    quadInfo.sets = std::vector({ m_PerFrameData.GetLayout(), m_TAADescriptorSets[0].GetLayout() });
    m_TAAPipeline.Init(ctx.device, taaRP, quadInfo, pipelineCache);

    // SSR descriptor sets and pipeline
    VkRenderPass ssrRP = m_Graph.GetPassRenderPass(m_SSRPassIndex);

    m_SSRPassDescriptorSets.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
    {
      SetDescription ssrDesc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      m_SSRPassDescriptorSets[i].Init(ctx, ssrDesc);
    }
    PipelineCreateInfo ssrDesc = {
      .fragmentShaderFile = "ssr.frag",
      .vertexShaderFile = "quad.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_PerFrameData.GetLayout(),
        m_SSRPassDescriptorSets[0].GetLayout(),
      })
    };
    m_SSRPipeline.Init(ctx.device, ssrRP, ssrDesc, pipelineCache);
  }

  void Render::DrawQuad()
  {
    vkCmdDraw(m_Backend.GetCurrentCommandBuffer(), 3, 1, 0, 0);
  }
}
