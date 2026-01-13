#include "Render.h"

#include <imgui_impl_glfw.h>
#include <ImGui/imgui_impl_vulkan.h>

#include "Application.h"
#include "VulkanVertexBuffer.h"

#include "Utils/Utils.h"

namespace YAEngine
{
  uint32_t Render::s_MaxFramesInFlight = 2;

  void Render::Init(GLFWwindow* window, const RenderSpecs &specs)
  {
    s_MaxFramesInFlight = specs.maxFramesInFlight;

    m_VulkanInstance.Init(specs);

    m_Surface.Init(m_VulkanInstance.Get(), window);

    m_PhysicalDevice.Init(m_VulkanInstance.Get(), m_Surface.Get());
    m_Device.Init(m_VulkanInstance, m_PhysicalDevice, m_Surface.Get());

    m_Allocator.Init(m_VulkanInstance.Get(), m_Device.Get(), m_PhysicalDevice.Get());

    m_SwapChain.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), window, m_Allocator.Get());
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    m_MainRenderPass.Init(m_Device.Get(), m_SwapChain.GetFormat(), m_Allocator.Get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    m_TAARenderPass.Init(m_Device.Get(), m_SwapChain.GetFormat(), m_Allocator.Get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    m_SwapChainRenderPass.Init(m_Device.Get(), m_SwapChain.GetFormat(), m_Allocator.Get(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    m_SwapChain.CreateFrameBuffers(m_SwapChainRenderPass.Get(), width, height);

    m_CommandBuffer.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), s_MaxFramesInFlight);

    m_Sync.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), m_SwapChain.GetImageCount());
    m_CommandBuffer.SetGraphicsQueue(m_Sync.GetQueue());

    VulkanTexture::InitTextures(m_Device.Get(), m_Sync.GetQueue(), m_CommandBuffer, m_Allocator.Get());
    VulkanVertexBuffer::InitVertexBuffers(m_Device.Get(), m_Sync.GetQueue(), m_CommandBuffer, m_Allocator.Get());

    m_DescriptorPool.Init(m_Device.Get());

    VulkanMaterial::InitMaterials(m_Device.Get(), m_DescriptorPool.Get(), m_Allocator.Get(), s_MaxFramesInFlight);
    m_DefaultMaterial.Init();

    m_PerFrameData.Init(m_Device.Get(), m_Allocator.Get(), m_DescriptorPool.Get(), s_MaxFramesInFlight);

    VulkanStorageBuffer::InitBuffers(m_Device.Get(), m_Allocator.Get());

    InitPipelines();

    m_MainPassFrameBuffer.Init(m_Device.Get(), m_Allocator.Get(), m_MainRenderPass.Get(), width, height, m_SwapChain.GetFormat());
    for (auto& buffer : m_HistoryFrameBuffers)
    {
      buffer.Init(m_Device.Get(), m_Allocator.Get(), m_TAARenderPass.Get(), width, height, m_SwapChain.GetFormat());
      auto cmd = m_CommandBuffer.BeginSingleTimeCommands();
      buffer.Begin(cmd);
      VkClearValue clearValues[2];
      clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
      clearValues[1].depthStencil = {1.0f, 0};
      VkRenderPassBeginInfo rpBegin{};
      rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpBegin.renderPass = m_TAARenderPass.Get();
      rpBegin.framebuffer = buffer.Get();
      rpBegin.renderArea.extent.width = width;
      rpBegin.renderArea.extent.height = height;
      rpBegin.clearValueCount = 2;
      rpBegin.pClearValues = clearValues;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdEndRenderPass(cmd);
      buffer.End(cmd);
      m_CommandBuffer.EndSingleTimeCommands(cmd);
    }

    m_ImGUI.Init(
      window,
      m_VulkanInstance.Get(),
      m_PhysicalDevice.Get(),
      m_Device.Get(),
      m_Sync.GetQueue(),
      m_SwapChain.GetImageCount(),
      VulkanPhysicalDevice::FindQueueFamilies(
        m_PhysicalDevice.Get(),
        m_Surface.Get()
      ).graphicsFamily.value(),
      m_SwapChainRenderPass.Get()
    );

    VulkanCubicTexture::InitCubicTextures(m_Device.Get(), m_Allocator.Get(), m_CommandBuffer, m_DescriptorPool.Get());

    m_SkyBox.Init(m_Device.Get(), m_DescriptorPool.Get(), m_MainRenderPass.Get(), s_MaxFramesInFlight);

    vkDeviceWaitIdle(m_Device.Get());
  }

  void Render::Destroy()
  {
    m_Sync.Destroy();

    m_SkyBox.Destroy();
    VulkanCubicTexture::DestroyCubicTextures();
    m_ImGUI.Destroy();
    m_MainPassFrameBuffer.Destroy();
    for (auto& buffer : m_HistoryFrameBuffers)
    {
      buffer.Destroy();
    }
    m_SwapChainDescriptorSet.Destroy();
    m_TAADescriptorSet.Destroy();
    m_CommandBuffer.Destroy();
    m_ForwardPipeline.Destroy();
    m_ForwardPipelineDoubleSided.Destroy();
    m_ForwardPipelineInstanced.Destroy();
    m_ForwardPipelineDoubleSidedInstanced.Destroy();
    m_InstanceDescriptorSet.Destroy();
    m_ForwardPipelineNoShading.Destroy();
    m_InstanceBuffer.Destroy();
    m_QuadPipeline.Destroy();
    m_TAAPipeline.Destroy();
    m_DefaultMaterial.Destroy();
    m_PerFrameData.Destroy();
    m_DescriptorPool.Destroy();
    m_SwapChain.Destroy();
    m_MainRenderPass.Destroy();
    m_SwapChainRenderPass.Destroy();
    m_TAARenderPass.Destroy();
    m_Allocator.Destroy();
    m_Device.Destroy();
    m_Surface.Destroy();
    m_VulkanInstance.Destroy();
  }

  void Render::Resize(uint32_t width, uint32_t height)
  {
    b_Resized = false;
    m_MainRenderPass.Recreate();
    m_MainPassFrameBuffer.Recreate(m_MainRenderPass.Get(), width, height);

    m_TAARenderPass.Recreate();
    for (auto& buffer : m_HistoryFrameBuffers)
    {
      buffer.Recreate(m_TAARenderPass.Get(), width, height);
      auto cmd = m_CommandBuffer.BeginSingleTimeCommands();
      buffer.Begin(cmd);
      VkClearValue clearValues[2];
      clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
      clearValues[1].depthStencil = {1.0f, 0};
      VkRenderPassBeginInfo rpBegin{};
      rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpBegin.renderPass = m_TAARenderPass.Get();
      rpBegin.framebuffer = buffer.Get();
      rpBegin.renderArea.extent.width = width;
      rpBegin.renderArea.extent.height = height;
      rpBegin.clearValueCount = 2;
      rpBegin.pClearValues = clearValues;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdEndRenderPass(cmd);
      buffer.End(cmd);
      m_CommandBuffer.EndSingleTimeCommands(cmd);
    }

    m_SwapChainRenderPass.Recreate();
    m_SwapChain.Recreate(m_SwapChainRenderPass.Get(), width, height);
  }

  void Render::Draw(Application *app)
  {
    uint32_t imageIndex;
    auto result = m_Sync.WaitIdle(m_SwapChain.Get(), &imageIndex);

    if (!result)
    {
      Resize(app->m_Window.GetWidth(), app->m_Window.GetHeight());
      return;
    }

    SetUpCamera(app);
    m_PerFrameData.ubo.time = (float)app->m_Timer.GetTime();
    m_PerFrameData.ubo.gamma = m_Gamma;
    m_PerFrameData.ubo.exposure = m_Exposure;
    m_PerFrameData.ubo.currentTexture = m_CurrentTexture;

    m_CommandBuffer.Set(m_CurrentFrameIndex);

    m_MainPassFrameBuffer.Begin(m_CommandBuffer.GetCurrentBuffer());
    m_MainRenderPass.Begin(m_CommandBuffer.GetCurrentBuffer(), m_MainPassFrameBuffer.Get(), m_SwapChain.GetExt());
    m_PerFrameData.SetUp(m_CurrentFrameIndex);

    m_ForwardPipeline.Bind(m_CommandBuffer.GetCurrentBuffer());
    m_ForwardPipeline.BindDescriptorSets(m_CommandBuffer.GetCurrentBuffer(), { m_PerFrameData.GetDescriptorSet(m_CurrentFrameIndex) }, 0);
    m_ForwardPipelineDoubleSided.Bind(m_CommandBuffer.GetCurrentBuffer());
    m_ForwardPipelineDoubleSided.BindDescriptorSets(m_CommandBuffer.GetCurrentBuffer(), { m_PerFrameData.GetDescriptorSet(m_CurrentFrameIndex) }, 0);
    SetViewportAndScissor();

    DrawMeshes(app);

    m_MainRenderPass.End(m_CommandBuffer.GetCurrentBuffer());
    m_MainPassFrameBuffer.End(m_CommandBuffer.GetCurrentBuffer());

    auto prevIndex = m_TAAIndex == 1 ? 0 : m_TAAIndex + 1;
    m_HistoryFrameBuffers[m_TAAIndex].Begin(m_CommandBuffer.GetCurrentBuffer());
    m_TAARenderPass.Begin(m_CommandBuffer.GetCurrentBuffer(), m_HistoryFrameBuffers[m_TAAIndex].Get(), m_SwapChain.GetExt());
    m_TAAPipeline.Bind(m_CommandBuffer.GetCurrentBuffer());
    m_TAADescriptorSet.WriteCombinedImageSampler(0, m_MainPassFrameBuffer.GetImageView(), m_MainPassFrameBuffer.GetSampler(), m_MainPassFrameBuffer.GetLayout());
    m_TAADescriptorSet.WriteCombinedImageSampler(1, m_HistoryFrameBuffers[prevIndex].GetImageView(), m_HistoryFrameBuffers[prevIndex].GetSampler(), m_HistoryFrameBuffers[prevIndex].GetLayout());
    m_TAAPipeline.BindDescriptorSets(m_CommandBuffer.GetCurrentBuffer(), { m_TAADescriptorSet.Get() }, 0);
    DrawQuad();
    m_TAARenderPass.End(m_CommandBuffer.GetCurrentBuffer());
    m_HistoryFrameBuffers[m_TAAIndex].End(m_CommandBuffer.GetCurrentBuffer());

    m_SwapChainRenderPass.Begin(m_CommandBuffer.GetCurrentBuffer(), m_SwapChain.GetFramebuffer(imageIndex), m_SwapChain.GetExt());
    SetViewportAndScissor();

    m_QuadPipeline.Bind(m_CommandBuffer.GetCurrentBuffer());
    m_QuadPipeline.BindDescriptorSets(m_CommandBuffer.GetCurrentBuffer(), { m_SwapChainDescriptorSet.Get() }, 0);
    m_SwapChainDescriptorSet.WriteCombinedImageSampler(0, m_HistoryFrameBuffers[m_TAAIndex].GetImageView(), m_HistoryFrameBuffers[m_TAAIndex].GetSampler(), m_HistoryFrameBuffers[m_TAAIndex].GetLayout());
    DrawQuad();

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    app->RenderUI();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_CommandBuffer.GetCurrentBuffer());

    m_SwapChainRenderPass.End(m_CommandBuffer.GetCurrentBuffer());

    m_CommandBuffer.End(m_CurrentFrameIndex);
    result = m_Sync.Submit(m_CommandBuffer.GetCurrentBuffer(), m_SwapChain.Get(), imageIndex, b_Resized);
    if (!result)
    {
      Resize(app->m_Window.GetWidth(), app->m_Window.GetHeight());
    }
    m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % s_MaxFramesInFlight;
    m_TAAIndex = (m_TAAIndex + 1) % 2;
    m_GlobalFrameIndex++;
  }

  void Render::SetViewportAndScissor()
  {
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) m_SwapChain.GetExt().width;
    viewport.height = (float) m_SwapChain.GetExt().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_CommandBuffer.GetCurrentBuffer(), 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_SwapChain.GetExt();
    vkCmdSetScissor(m_CommandBuffer.GetCurrentBuffer(), 0, 1, &scissor);

  }

  void Render::DrawMeshes(Application* app)
  {
    auto view = app->m_Scene.m_Registry.view<MeshComponent, TransformComponent, MaterialComponent>();

    view.each([&](MeshComponent mesh, TransformComponent transform, MaterialComponent material)
    {
      if (!mesh.shouldRender) return;

      auto& currentPipeline = mesh.noShading ? m_ForwardPipelineNoShading :
        app->m_AssetManager.Meshes().Get(mesh.asset).instanceData == nullptr
          ? mesh.doubleSided ? m_ForwardPipelineDoubleSided : m_ForwardPipeline
          : mesh.doubleSided ? m_ForwardPipelineDoubleSidedInstanced : m_ForwardPipelineInstanced;

      currentPipeline.Bind(m_CommandBuffer.GetCurrentBuffer());

      struct _
      {
        glm::mat4 model;
        int offset = 0;
      } data;
      data.model = transform.world;
      data.offset = app->m_AssetManager.Meshes().Get(mesh.asset).offset / sizeof(glm::mat4);
      currentPipeline.PushConstants(m_CommandBuffer.GetCurrentBuffer(), &data);

      currentPipeline.BindDescriptorSets(m_CommandBuffer.GetCurrentBuffer(), {app->GetAssetManager().Materials().Get(material.asset).m_VulkanMaterial.GetDescriptorSet(m_CurrentFrameIndex)}, 1);
      uint32_t instanceCount = 1;
      if (app->m_AssetManager.Meshes().Get(mesh.asset).instanceData != nullptr)
      {
        instanceCount = uint32_t(app->m_AssetManager.Meshes().Get(mesh.asset).instanceData->size());
        currentPipeline.BindDescriptorSets(m_CommandBuffer.GetCurrentBuffer(), { m_InstanceDescriptorSet.Get() }, 2);
        m_InstanceBuffer.Update(app->m_AssetManager.Meshes().Get(mesh.asset).offset, app->m_AssetManager.Meshes().Get(mesh.asset).instanceData->data(), uint32_t(instanceCount * sizeof(glm::mat4)));
      }

      auto& mat = app->GetAssetManager().Materials().Get(material.asset);
      mat.cubemap = app->GetScene().m_Skybox;
      app->GetAssetManager().Materials().Get(material.asset).m_VulkanMaterial.Bind(app, mat, m_CurrentFrameIndex);

      app->m_AssetManager.Meshes().Get(mesh.asset).vertexBuffer.Draw(m_CommandBuffer.GetCurrentBuffer(), instanceCount);
    });

    if (!app->m_Scene.HasComponent<CameraComponent>(app->m_Scene.GetActiveCamera())) return;
    auto cameraEntity = (app->m_Scene.m_Registry.get<CameraComponent, TransformComponent>(app->m_Scene.GetActiveCamera()));
    auto transform = std::get<TransformComponent &>(cameraEntity);
    glm::mat4 camDir = glm::mat4_cast(transform.rotation);
    if (app->GetScene().m_Skybox)
      m_SkyBox.Draw(m_CurrentFrameIndex, &app->GetAssetManager().CubeMaps().Get(app->GetScene().m_Skybox).m_CubeTexture, m_CommandBuffer.GetCurrentBuffer(), camDir, m_PerFrameData.ubo.proj);
  }

  void Render::SetUpCamera(Application* app)
  {
    if (!app->m_Scene.HasComponent<CameraComponent>(app->m_Scene.GetActiveCamera()))
      return;

    auto cameraEntity =
      app->m_Scene.m_Registry.get<CameraComponent, TransformComponent>(
        app->m_Scene.GetActiveCamera());

    auto& transform = std::get<TransformComponent&>(cameraEntity);
    auto& camera    = std::get<CameraComponent&>(cameraEntity);

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

    glm::vec2 jitter = GetTAAJitter(m_GlobalFrameIndex);

    jitter.x /= float(app->GetWindow().GetWidth());
    jitter.y /= float(app->GetWindow().GetHeight());

    proj[2][0] += jitter.x;
    proj[2][1] += jitter.y;

    m_PerFrameData.ubo.view = view;
    m_PerFrameData.ubo.proj = proj;
    m_PerFrameData.ubo.cameraPosition = transform.position;
  }

  void Render::InitPipelines()
  {
    SetDescription instanceDesc = {
      .set = 2,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT }
      }
    };
    m_InstanceDescriptorSet.Init(m_Device.Get(), m_DescriptorPool.Get(), instanceDesc);
    constexpr auto MAX_INSTANCES = 10000;
    m_InstanceBuffer.Create(MAX_INSTANCES * sizeof(glm::mat4));
    m_InstanceDescriptorSet.WriteStorageBuffer(0, m_InstanceBuffer.Get(), MAX_INSTANCES * sizeof(glm::mat4));

    PipelineCreateInfo forwardInfo = {
      .fragmentShaderFile = "shader.frag",
      .vertexShaderFile = "shader.vert",
      .pushConstantSize = sizeof(glm::mat4) + sizeof(int),
      .vertexInputFormat = "f3f2f3f4",
      .sets = std::vector({
        m_PerFrameData.GetLayout(),
        m_DefaultMaterial.GetLayout(),
      })
    };
    m_ForwardPipeline.Init(m_Device.Get(), m_MainRenderPass.Get(), forwardInfo);
    forwardInfo.doubleSided = true;
    m_ForwardPipelineDoubleSided.Init(m_Device.Get(), m_MainRenderPass.Get(), forwardInfo);

    forwardInfo.sets.push_back(m_InstanceDescriptorSet.GetLayout());
    forwardInfo.vertexShaderFile = "instanced.vert",
    m_ForwardPipelineDoubleSidedInstanced.Init(m_Device.Get(), m_MainRenderPass.Get(), forwardInfo);
    forwardInfo.doubleSided = false;
    m_ForwardPipelineInstanced.Init(m_Device.Get(), m_MainRenderPass.Get(), forwardInfo);

    forwardInfo.doubleSided = true;
    forwardInfo.fragmentShaderFile = "no_shading.frag";
    forwardInfo.vertexShaderFile = "shader.vert";
    m_ForwardPipelineNoShading.Init(m_Device.Get(), m_MainRenderPass.Get(), forwardInfo);

    SetDescription desc = {
      .set = 0,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };
    m_SwapChainDescriptorSet.Init(m_Device.Get(), m_DescriptorPool.Get(), desc);
    SetDescription taaDesc = {
      .set = 0,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };
    m_TAADescriptorSet.Init(m_Device.Get(), m_DescriptorPool.Get(), taaDesc);

    PipelineCreateInfo quadInfo = {
      .fragmentShaderFile = "quad.frag",
      .vertexShaderFile = "quad.vert",
      .depthTesting = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_SwapChainDescriptorSet.GetLayout(),
      })
    };

    m_QuadPipeline.Init(m_Device.Get(), m_SwapChainRenderPass.Get(), quadInfo);
    quadInfo.fragmentShaderFile = "taa.frag";
    quadInfo.sets = std::vector({ m_TAADescriptorSet.GetLayout() });
    m_TAAPipeline.Init(m_Device.Get(), m_SwapChainRenderPass.Get(), quadInfo);
  }

  void Render::DrawQuad()
  {
    vkCmdDraw(m_CommandBuffer.GetCurrentBuffer(), 3, 1, 0, 0);
  }
}
