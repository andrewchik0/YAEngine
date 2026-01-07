#pragma once

#include <mutex>

#include "EventBus.h"
#include "Window.h"
#include "Layer.h"
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
      m_LayerStack.push_back(std::make_unique<T>());
    }

    template<typename T>
    requires std::is_base_of<Layer, T>::value
    T* GetLayer()
    {
      for (const auto& layer : m_LayerStack)
      {
        if (auto casted = dynamic_cast<T*>(layer.get()))
          return casted;
      }
      return nullptr;
    }

    void RenderUI()
    {
      for (auto& layer : m_LayerStack)
      {
        layer->RenderUI();
      }
    }

    Timer& GetTimer()
    {
      return m_Timer;
    }

  private:

    explicit Application(const ApplicationSpecs& specs);

    static std::unique_ptr<Application> instance;
    static std::mutex mtx;

    std::vector<std::unique_ptr<Layer>> m_LayerStack;

    Render m_Render {};
    Scene m_Scene;
    Window m_Window;
    AssetManager m_AssetManager;
    Timer m_Timer;
    EventBus m_EventBus;

    friend class Scene;
    friend class Render;

    void HandleEvents();
    void InitLayers();
  };
}
