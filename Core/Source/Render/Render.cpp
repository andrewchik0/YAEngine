#include "Render.h"

#include <imgui_impl_glfw.h>
#include <ImGui/imgui_impl_vulkan.h>

#include "Application.h"
#include "VulkanVertexBuffer.h"

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
    m_MainRenderPass.Init(m_Device.Get(), m_SwapChain.GetFormat(), m_Allocator.Get(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true);

    m_SwapchainRenderPass.Init(m_Device.Get(), m_SwapChain.GetFormat(), m_Allocator.Get(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, false);
    m_SwapChain.CreateFrameBuffers(m_SwapchainRenderPass.Get(), width, height);

    m_CommandBuffer.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), s_MaxFramesInFlight);

    m_Sync.Init(m_Device.Get(), m_PhysicalDevice.Get(), m_Surface.Get(), m_SwapChain.GetImageCount());
    m_CommandBuffer.SetGraphicsQueue(m_Sync.GetQueue());

    VulkanTexture::InitTextures(m_Device.Get(), m_Sync.GetQueue(), m_CommandBuffer, m_Allocator.Get());
    VulkanVertexBuffer::InitVertexBuffers(m_Device.Get(), m_Sync.GetQueue(), m_CommandBuffer, m_Allocator.Get());

    m_DescriptorPool.Init(m_Device.Get());

    VulkanMaterial::InitMaterials(m_Device.Get(), m_DescriptorPool.Get(), m_Allocator.Get(), s_MaxFramesInFlight);
    m_DefaultMaterial.Init();

    m_PerFrameData.Init(m_Device.Get(), m_Allocator.Get(), m_DescriptorPool.Get(), s_MaxFramesInFlight);

    PipelineCreateInfo forwardInfo = {
      .fragmentShaderFile = "shader.frag",
      .vertexShaderFile = "shader.vert",
      .vertexInputFormat = "f3f2f3f4",
      .sets = std::vector({
        m_PerFrameData.GetLayout(),
        m_DefaultMaterial.GetLayout(),
      })
    };
    m_ForwardPipeline.Init(m_Device.Get(), m_MainRenderPass.Get(), forwardInfo);
    forwardInfo.doubleSided = true;
    m_ForwardPipelineDoubleSided.Init(m_Device.Get(), m_MainRenderPass.Get(), forwardInfo);

    SetDescription desc = {
      .set = 0,
      .bindings = {
        {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
        }
      }
    };
    m_SwapChainDescriptorSet.Init(m_Device.Get(), m_DescriptorPool.Get(), desc);

    PipelineCreateInfo quadInfo = {
      .fragmentShaderFile = "quad.frag",
      .vertexShaderFile = "quad.vert",
      .depthTesting = false,
      .multisample = false,
      .vertexInputFormat = "",
      .sets = std::vector({
        m_SwapChainDescriptorSet.GetLayout(),
      })
    };

    m_QuadPipeline.Init(m_Device.Get(), m_SwapchainRenderPass.Get(), quadInfo);

    m_MainPassFrameBuffer.Init(m_Device.Get(), m_Allocator.Get(), m_MainRenderPass.Get(), width, height, m_SwapChain.GetFormat());


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
      m_SwapchainRenderPass.Get()
    );

    VulkanCubicTexture::InitCubicTextures(m_Device.Get(), m_Allocator.Get(), m_CommandBuffer, m_DescriptorPool.Get());

    m_SkyBox.Init(m_Device.Get(), m_DescriptorPool.Get(), m_MainRenderPass.Get(), s_MaxFramesInFlight);
  }

  void Render::Destroy()
  {
    m_Sync.Destroy();

    m_SkyBox.Destroy();
    VulkanCubicTexture::DestroyCubicTextures();
    m_ImGUI.Destroy();
    m_MainPassFrameBuffer.Destroy();
    m_SwapChainDescriptorSet.Destroy();
    m_CommandBuffer.Destroy();
    m_ForwardPipeline.Destroy();
    m_ForwardPipelineDoubleSided.Destroy();
    m_QuadPipeline.Destroy();
    m_DefaultMaterial.Destroy();
    m_PerFrameData.Destroy();
    m_DescriptorPool.Destroy();
    m_SwapChain.Destroy();
    m_MainRenderPass.Destroy();
    m_SwapchainRenderPass.Destroy();
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

    m_SwapchainRenderPass.Recreate();
    m_SwapChain.Recreate(m_SwapchainRenderPass.Get(), width, height);
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

    m_SwapchainRenderPass.Begin(m_CommandBuffer.GetCurrentBuffer(), m_SwapChain.GetFramebuffer(imageIndex), m_SwapChain.GetExt());
    SetViewportAndScissor();

    m_QuadPipeline.Bind(m_CommandBuffer.GetCurrentBuffer());
    m_QuadPipeline.BindDescriptorSets(m_CommandBuffer.GetCurrentBuffer(), { m_SwapChainDescriptorSet.Get() }, 0);
    m_SwapChainDescriptorSet.BindCombinedImageSampler(0, m_MainPassFrameBuffer.GetImageView(), m_MainPassFrameBuffer.GetSampler(), m_MainPassFrameBuffer.GetLayout());
    DrawQuad();

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    app->RenderUI();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_CommandBuffer.GetCurrentBuffer());

    m_SwapchainRenderPass.End(m_CommandBuffer.GetCurrentBuffer());

    m_CommandBuffer.End(m_CurrentFrameIndex);
    result = m_Sync.Submit(m_CommandBuffer.GetCurrentBuffer(), m_SwapChain.Get(), imageIndex, b_Resized);
    if (!result)
    {
      Resize(app->m_Window.GetWidth(), app->m_Window.GetHeight());
    }
    m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % s_MaxFramesInFlight;
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

      auto& currentPipeline = mesh.doubleSided ? m_ForwardPipelineDoubleSided : m_ForwardPipeline;

      currentPipeline.Bind(m_CommandBuffer.GetCurrentBuffer());
      currentPipeline.PushConstants(m_CommandBuffer.GetCurrentBuffer(), &transform.world);
      currentPipeline.BindDescriptorSets(m_CommandBuffer.GetCurrentBuffer(), {app->GetAssetManager().Materials().Get(material.asset).m_VulkanMaterial.GetDescriptorSet(m_CurrentFrameIndex)}, 1);
      auto& mat = app->GetAssetManager().Materials().Get(material.asset);
      mat.cubemap = app->GetScene().m_Skybox;
      app->GetAssetManager().Materials().Get(material.asset).m_VulkanMaterial.Bind(app, mat, m_CurrentFrameIndex);
      app->m_AssetManager.Meshes().Get(mesh.asset).vertexBuffer.Draw(m_CommandBuffer.GetCurrentBuffer());
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
    if (!app->m_Scene.HasComponent<CameraComponent>(app->m_Scene.GetActiveCamera())) return;

    auto cameraEntity = (app->m_Scene.m_Registry.get<CameraComponent, TransformComponent>(app->m_Scene.GetActiveCamera()));
    auto transform = std::get<TransformComponent &>(cameraEntity);
    auto camera = std::get<CameraComponent &>(cameraEntity);

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

    m_PerFrameData.ubo.view = view;
    m_PerFrameData.ubo.proj = proj;
    m_PerFrameData.ubo.cameraPosition = transform.position;
  }

  void Render::DrawQuad()
  {
    vkCmdDraw(m_CommandBuffer.GetCurrentBuffer(), 3, 1, 0, 0);
  }
}
