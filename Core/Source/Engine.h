#pragma once

#include "Pch.h"
#include "Utils/ServiceRegistry.h"
#include "Window.h"
#include "Layer.h"
#include "LayerManager.h"
#include "Input/InputSystem.h"
#include "Assets/AssetManager.h"
#include "Render/Render.h"
#include "Render/RenderObject.h"
#include "Scene/Scene.h"
#include "Scene/SystemScheduler.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/CollisionQueryService.h"
#include "Utils/Timer.h"
#include "Utils/ThreadPool.h"
#include "Utils/MainThreadDispatcher.h"
#include "Utils/Log.h"

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

    void DebugDrawGizmos()
    {
      m_LayerManager.CallDebugDrawGizmos();
    }

    Scene& GetScene() { return m_Scene; }
    Render& GetRender() { return m_Render; }
    AssetManager& GetAssetManager() { return m_AssetManager; }
    Window& GetWindow() { return m_Window; }
    Timer& GetTimer() { return m_Timer; }
    InputSystem& GetInputSystem() { return m_InputSystem; }
    ServiceRegistry& GetRegistry() { return m_Registry; }
    SystemScheduler& GetScheduler() { return m_Scheduler; }
    ThreadPool& GetThreadPool() { return m_ThreadPool; }
    MainThreadDispatcher& GetDispatcher() { return m_Dispatcher; }
    ComponentRegistry& GetComponentRegistry() { return m_ComponentRegistry; }

  private:
    FrameContext MakeFrameContext(SceneSnapshot& snapshot);

    ServiceRegistry m_Registry;
    ThreadPool m_ThreadPool;
    MainThreadDispatcher m_Dispatcher;

    Window m_Window;
    Timer m_Timer;
    InputSystem m_InputSystem;
    Render m_Render {};
    AssetManager m_AssetManager;
    Scene m_Scene;
    CollisionQueryService m_CollisionQueryService { m_Scene };
    SystemScheduler m_Scheduler;
    ComponentRegistry m_ComponentRegistry;
    LayerManager m_LayerManager;

    SceneSnapshot m_Snapshot;
    LightBuffer m_LightData {};

    static constexpr double FIXED_DT = 1.0 / 60.0;
    static constexpr int MAX_FIXED_STEPS = 5;
    double m_Accumulator = 0.0;
  };
}
