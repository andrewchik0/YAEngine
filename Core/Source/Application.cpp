#include "Application.h"

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
  }

  Application::~Application()
  {
    m_Render.Destroy();
  }

  void Application::Run()
  {
    for (auto& layer : m_LayerStack)
      layer->OnBeforeLoad();
    m_AssetManager.Init();
    for (auto& layer : m_LayerStack)
      layer->Init();

    m_Scene.GetComponent<CameraComponent>(m_Scene.GetActiveCamera()).Resize(static_cast<float>(m_Window.GetWidth()), static_cast<float>(m_Window.GetHeight()));

    while (m_Window.IsOpen())
    {
      HandleEvents();
      m_Timer.Step();
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

    for (auto& windowEvent : windowEventStack)
    {
      for (auto& layer : m_LayerStack)
      {
        switch (windowEvent->type)
        {
        case EventType::Key:
          layer->OnKeyboard(*dynamic_cast<KeyEvent*>(windowEvent.get()));
          break;
        case EventType::MouseButton:
          layer->OnMouseButton(*dynamic_cast<MouseButtonEvent*>(windowEvent.get()));
          break;
        case EventType::MouseMoved:
          layer->OnMouseMoved(*dynamic_cast<MouseMovedEvent*>(windowEvent.get()));
          break;
        case EventType::Resize:
          {
            float width = static_cast<float>(dynamic_cast<ResizeEvent*>(windowEvent.get())->width);
            float height = static_cast<float>(dynamic_cast<ResizeEvent*>(windowEvent.get())->height);
            m_Render.Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            m_Scene.GetComponent<CameraComponent>(m_Scene.GetActiveCamera()).Resize(width, height);
            layer->OnResize(*dynamic_cast<ResizeEvent*>(windowEvent.get()));
            break;
          }
        default: break;
        }
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
