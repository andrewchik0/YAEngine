#include "Engine.h"
#include "Render/FrameContext.h"
#include "Render/FrustumCull.h"
#include "SceneSnapshot.h"
#include "Scene/TransformSystem.h"
#include "Scene/BoundsUpdateSystem.h"

#include <imgui.h>

#ifdef YA_EDITOR
#include "Editor/EditorLayer.h"
#endif

namespace YAEngine
{
  static void RenderUICallback(void* userData)
  {
    static_cast<Engine*>(userData)->RenderUI();
  }

  FrameContext Engine::MakeFrameContext(SceneSnapshot& snapshot)
  {
    return FrameContext {
      .snapshot = snapshot,
      .lights = m_LightData,
      .assets = m_AssetManager,
      .cubicResources = m_Render.GetCubicResources(),
      .time = m_Timer.GetTime(),
      .windowWidth = m_Window.GetWidth(),
      .windowHeight = m_Window.GetHeight(),
      .renderUI = RenderUICallback,
      .renderUIData = this
    };
  }

  Engine::Engine(const EngineSpecs& specs)
    : m_Window(specs.windowSpecs),
      m_InputSystem(m_Window)
  {
    m_Registry.Register<Window>(&m_Window);
    m_Registry.Register<Timer>(&m_Timer);
    m_Registry.Register<InputSystem>(&m_InputSystem);
    m_Registry.Register<Render>(&m_Render);
    m_Registry.Register<AssetManager>(&m_AssetManager);
    m_Registry.Register<Scene>(&m_Scene);
    m_Registry.Register<SystemScheduler>(&m_Scheduler);
    m_Registry.Register<LayerManager>(&m_LayerManager);
    m_LayerManager.SetRegistry(m_Registry);

    m_Scheduler.AddSystem<TransformSystem>();
    m_Scheduler.AddSystem<BoundsUpdateSystem>();

    RenderSpecs renderSpecs;
    renderSpecs.validationLayers = specs.isDebug;
    renderSpecs.applicationName = specs.windowSpecs.title;

    m_Render.Init(m_Window.Get(), renderSpecs);

    SceneSnapshot emptySnapshot;
    auto frame = MakeFrameContext(emptySnapshot);
    m_Render.Draw(frame);
  }

  void Engine::Destroy()
  {
    m_Render.Destroy();
    m_Window.Destroy();
  }

  void Engine::Run()
  {
#ifdef YA_EDITOR
    m_LayerManager.PushLayer<EditorLayer>();
#endif
    m_LayerManager.CallOnAttach();
    m_AssetManager.Init(m_Scene, [this](uint32_t size) { return m_Render.AllocateInstanceData(size); });
    m_AssetManager.SetRenderContext(m_Render.GetContext(), m_Render.GetNoneTexture(), m_Render.GetCubicResources());
    m_Render.Resize();
    {
      SceneSnapshot emptySnapshot;
      auto frame = MakeFrameContext(emptySnapshot);
      m_Render.Draw(frame);
    }

    m_LayerManager.CallOnSceneReady();
    m_InputSystem.SetImGuiFiltering(true);

    auto swapExtent = m_Render.GetSwapChainExtent();
    m_Scene.GetComponent<CameraComponent>(m_Scene.GetActiveCamera()).Resize(
      (float)swapExtent.width, (float)swapExtent.height);

    while (m_Window.IsOpen())
    {
      m_InputSystem.ProcessEvents();

      if (m_Window.WasResized())
      {
        m_Window.ResetResized();
        float width = static_cast<float>(m_Window.GetWidth());
        float height = static_cast<float>(m_Window.GetHeight());
        if (width != 0.0f && height != 0.0f)
        {
          m_Render.Resize();
#ifndef YA_EDITOR
          auto extent = m_Render.GetSwapChainExtent();
          m_Scene.GetComponent<CameraComponent>(m_Scene.GetActiveCamera()).Resize(
            (float)extent.width, (float)extent.height);
#endif
        }
      }

      if (m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0)
      {
        glfwWaitEvents();
        continue;
      }

      m_Timer.Step();

      double frameDt = m_Timer.GetDeltaTime();
      m_Accumulator += frameDt;
      if (m_Accumulator > FIXED_DT * MAX_FIXED_STEPS)
        m_Accumulator = FIXED_DT * MAX_FIXED_STEPS;
      while (m_Accumulator >= FIXED_DT)
      {
        m_LayerManager.CallFixedUpdate(FIXED_DT);
        m_Accumulator -= FIXED_DT;
      }

      m_Scheduler.Run(m_Scene.GetRegistry(), frameDt);
      m_LayerManager.CallUpdate(frameDt);
      m_LayerManager.CallLateUpdate(frameDt);
      m_InputSystem.EndFrame();

      BuildSceneSnapshot(m_Snapshot, m_LightData, m_Scene, m_AssetManager.Meshes());

      // Frustum cull: compute viewProj from camera data, partition visible objects to front
      auto& cam = m_Snapshot.camera;
      glm::mat4 world = glm::translate(glm::mat4(1.0f), cam.position) * glm::mat4_cast(cam.rotation);
      glm::mat4 view = glm::inverse(world);
      glm::mat4 proj = glm::perspective(cam.fov, cam.aspectRatio, cam.nearPlane, cam.farPlane);
      m_Snapshot.visibleCount = FrustumCull(m_Snapshot.objects, proj * view);

      auto frame = MakeFrameContext(m_Snapshot);
      m_Render.Draw(frame);
    }

    m_LayerManager.CallOnDetach();

    m_Render.WaitIdle();
    m_AssetManager.DestroyAll();
  }
}
