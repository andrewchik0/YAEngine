#include "Application.h"

#include "../../cmake-build-release/_deps/imgui-src/imgui.h"

#ifdef YA_EDITOR
#include "Editor/EditorLayer.h"
#endif

namespace YAEngine
{
  std::unique_ptr<Application> Application::instance;
  std::mutex Application::mtx;

  Application::Application(const ApplicationSpecs& specs)
    : m_Window(specs.windowSpecs),
      m_InputSystem(m_Window, m_EventBus)
  {
    RenderSpecs renderSpecs;

    renderSpecs.validationLayers = specs.isDebug;
    renderSpecs.applicationName = specs.windowSpecs.title;

    m_Render.Init(m_Window.Get(), renderSpecs);
    m_Render.Draw(this);
  }

  void Application::Destroy()
  {
    m_EventBus.Unsubscribe<ResizeEvent>(m_ResizeSubscription);
    m_Render.Destroy();
    m_Window.Destroy();
  }

  void Application::Run()
  {
#ifdef YA_EDITOR
    m_LayerManager.PushLayer<EditorLayer>();
#endif
    m_LayerManager.CallOnAttach();
    m_AssetManager.Init();
    m_AssetManager.SetRenderContext(m_Render.GetContext(), m_Render.GetNoneTexture(), m_Render.GetCubicResources());
    m_Render.Resize();
    m_Render.Draw(this);

    m_LayerManager.CallOnSceneReady();
    m_InputSystem.SetImGuiFiltering(true);

    auto swapExtent = m_Render.GetSwapChainExtent();
    m_Scene.GetComponent<CameraComponent>(m_Scene.GetActiveCamera()).Resize(
      (float)swapExtent.width, (float)swapExtent.height);

    // Subscribe to resize events
    m_ResizeSubscription = m_EventBus.Subscribe<ResizeEvent>(
      std::function<void(const ResizeEvent&)>([this](const ResizeEvent& e)
      {
        float width = static_cast<float>(e.width);
        float height = static_cast<float>(e.height);
        if (width != 0.0f && height != 0.0f)
        {
          m_Render.Resize();
#ifndef YA_EDITOR
          // In non-editor mode, camera aspect matches window.
          // In editor mode, camera aspect is driven by viewport panel size.
          auto extent = m_Render.GetSwapChainExtent();
          m_Scene.GetComponent<CameraComponent>(m_Scene.GetActiveCamera()).Resize(
            (float)extent.width, (float)extent.height);
#endif
        }
      }), -1000); // High priority: handle resize before layers

    while (m_Window.IsOpen())
    {
      m_InputSystem.ProcessEvents();

      if (m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0)
      {
        glfwWaitEvents();
        continue;
      }

      m_Timer.Step();
      m_Scene.Update();
      m_LayerManager.CallUpdate(m_Timer.GetDeltaTime());

      m_Render.Draw(this);
    }

    m_LayerManager.CallOnDetach();

    m_AssetManager.DestroyAll();
  }
}
