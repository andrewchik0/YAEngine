#pragma once

#include <mutex>

#include "EventBus.h"
#include "Window.h"
#include "Layer.h"
#include "LayerManager.h"
#include "InputSystem.h"
#include "Assets/AssetManager.h"
#include "Render/Render.h"
#include "Scene/Scene.h"
#include "Utils/Timer.h"

namespace YAEngine
{
  struct ApplicationSpecs
  {
    WindowSpecs windowSpecs;
    bool isDebug = false;
  };

  class Application
  {
  public:
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    static void Init(ApplicationSpecs specs)
    {
      std::lock_guard<std::mutex> lock(mtx);
      if (!instance)
      {
        instance.reset(new Application(specs));
      } else
      {
        throw std::runtime_error("Singleton already initialized!");
      }
    }

    static Application& Get()
    {
      if (!instance) throw std::runtime_error("Singleton not initialized!");
      return *instance;
    }

    void Destroy();

    void Run();

    Scene& GetScene()
    {
      return m_Scene;
    }

    EventBus& Events()
    {
      return m_EventBus;
    }

    Window& GetWindow()
    {
      return m_Window;
    }

    AssetManager& GetAssetManager()
    {
      return m_AssetManager;
    }

    template<typename T>
    requires std::is_base_of<Layer, T>::value
    void PushLayer()
    {
      m_LayerManager.PushLayer<T>();
    }

    template<typename T>
    requires std::is_base_of<Layer, T>::value
    T* GetLayer()
    {
      return m_LayerManager.GetLayer<T>();
    }

    void RenderUI()
    {
      m_LayerManager.CallRenderUI();
    }

    Timer& GetTimer()
    {
      return m_Timer;
    }

    Render& GetRender()
    {
      return m_Render;
    }

  private:

    explicit Application(const ApplicationSpecs& specs);

    static std::unique_ptr<Application> instance;
    static std::mutex mtx;

    Render m_Render {};
    Scene m_Scene;
    Window m_Window;
    AssetManager m_AssetManager;
    Timer m_Timer;
    EventBus m_EventBus;
    InputSystem m_InputSystem;
    LayerManager m_LayerManager;

    SubscriptionId m_ResizeSubscription {};

    friend class Scene;
  };
}
