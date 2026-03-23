#include "Application.h"

#include "../../cmake-build-release/_deps/imgui-src/imgui.h"

namespace YAEngine
{
  std::unique_ptr<Application> Application::instance;
  std::mutex Application::mtx;

  Application::Application(const ApplicationSpecs& specs)
    : m_Window(specs.windowSpecs)
  {
    RenderSpecs renderSpecs;

    renderSpecs.validationLayers = specs.isDebug;
    renderSpecs.applicationName = specs.windowSpecs.title;

    m_Render.Init(m_Window.Get(), renderSpecs);
    m_Render.Draw(this);
  }

  void Application::Destroy()
  {
    m_Render.Destroy();
    m_Window.Destroy();
  }

  void Application::Run()
  {
    for (auto& layer : m_LayerStack)
      layer->OnBeforeInit();
    m_AssetManager.Init();
    m_AssetManager.SetRenderContext(m_Render.GetContext(), m_Render.GetNoneTexture(), m_Render.GetCubicResources());
    m_Render.Resize();
    m_Render.Draw(this);

    for (auto& layer : m_LayerStack)
      layer->Init();

    auto swapExtent = m_Render.GetSwapChainExtent();
    m_Scene.GetComponent<CameraComponent>(m_Scene.GetActiveCamera()).Resize(
      (float)swapExtent.width, (float)swapExtent.height);

    while (m_Window.IsOpen())
    {
      HandleEvents();

      if (m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0)
      {
        glfwWaitEvents();
        continue;
      }

      m_Timer.Step();
      m_Scene.Update();
      for (auto& layer : m_LayerStack)
        layer->Update(m_Timer.GetDeltaTime());

      m_Render.Draw(this);
    }

    for (auto& layer : m_LayerStack)
      layer->Destroy();

    m_AssetManager.DestroyAll();
  }

  void Application::HandleEvents()
  {
    const auto& windowEventStack = m_Window.PollEvents();

    ImGuiIO& io = ImGui::GetIO();

    for (auto& windowEvent : windowEventStack)
    {
      switch (windowEvent->type)
      {
      case EventType::Key:
        if (!io.WantCaptureKeyboard)
          m_EventBus.Emit<KeyEvent>(*dynamic_cast<KeyEvent*>(windowEvent.get()));
        break;
      case EventType::MouseButton:
        if (!io.WantCaptureMouse)
          m_EventBus.Emit<MouseButtonEvent>(*dynamic_cast<MouseButtonEvent*>(windowEvent.get()));
        break;
      case EventType::MouseScroll:
        if (!io.WantCaptureMouse)
            m_EventBus.Emit<MouseWheelEvent>(*dynamic_cast<MouseWheelEvent*>(windowEvent.get()));
        break;
      case EventType::MouseMoved:
        m_EventBus.Emit<MouseMovedEvent>(*dynamic_cast<MouseMovedEvent*>(windowEvent.get()));
        break;
      case EventType::Resize:
        {
          float width = static_cast<float>(dynamic_cast<ResizeEvent*>(windowEvent.get())->width);
          float height = static_cast<float>(dynamic_cast<ResizeEvent*>(windowEvent.get())->height);
          if (width != 0.0f && height != 0.0f)
          {
            m_Render.Resize();
            auto extent = m_Render.GetSwapChainExtent();
            m_Scene.GetComponent<CameraComponent>(m_Scene.GetActiveCamera()).Resize(
              (float)extent.width, (float)extent.height);
            m_EventBus.Emit<ResizeEvent>(*dynamic_cast<ResizeEvent*>(windowEvent.get()));
          }
          break;
        }
      default: break;
      }
    }
  }

  void Application::InitLayers()
  {
    for (auto& layer : m_LayerStack)
    {
      layer->Init();
    }
  }


}
