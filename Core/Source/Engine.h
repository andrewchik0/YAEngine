#pragma once

#include "Pch.h"
#include "ServiceRegistry.h"
#include "Window.h"
#include "Layer.h"
#include "LayerManager.h"
#include "InputSystem.h"
#include "Assets/AssetManager.h"
#include "Render/Render.h"
#include "Render/RenderObject.h"
#include "Scene/Scene.h"
#include "Scene/SystemScheduler.h"
#include "Utils/Timer.h"
#include "Log.h"

namespace YAEngine
{
  struct EngineSpecs
  {
    WindowSpecs windowSpecs;
    bool isDebug = false;
  };

  class Engine
  {
  public:
    explicit Engine(const EngineSpecs& specs);

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void Run();
    void Destroy();

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

    Scene& GetScene() { return m_Scene; }
    Render& GetRender() { return m_Render; }
    AssetManager& GetAssetManager() { return m_AssetManager; }
    Window& GetWindow() { return m_Window; }
    Timer& GetTimer() { return m_Timer; }
    InputSystem& GetInputSystem() { return m_InputSystem; }
    ServiceRegistry& GetRegistry() { return m_Registry; }
    SystemScheduler& GetScheduler() { return m_Scheduler; }

  private:
    FrameContext MakeFrameContext(SceneSnapshot& snapshot);

    ServiceRegistry m_Registry;

    Window m_Window;
    Timer m_Timer;
    InputSystem m_InputSystem;
    Render m_Render {};
    AssetManager m_AssetManager;
    Scene m_Scene;
    SystemScheduler m_Scheduler;
    LayerManager m_LayerManager;

    SceneSnapshot m_Snapshot;
    LightBuffer m_LightData {};

    static constexpr double FIXED_DT = 1.0 / 60.0;
    static constexpr int MAX_FIXED_STEPS = 5;
    double m_Accumulator = 0.0;
  };
}
