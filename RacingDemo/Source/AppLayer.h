#pragma once

#include "Layer.h"
#include "LayerManager.h"
#include "Window.h"
#include "Input/InputSystem.h"
#include "ControlsLayer.h"
#include "ReelPlaybackLayer.h"
#include "GameComponents.h"
#include "GameComponentSerializers.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Utils/ThreadPool.h"
#include "Render/Render.h"
#include "Assets/AssetManager.h"
#include "SparkPool.h"
#ifdef YA_EDITOR
#include "Editor/EditorCameraLayer.h"
#endif

// #define TEST
// #define BISTRO
#define BISTRO_RACING

class AppLayer : public YAEngine::Layer
{
public:
  AppLayer() = default;

  void OnAttach() override
  {
    GetWindow().Maximize();
#if defined(BISTRO_RACING)
    GetLayerManager().PushLayer<ControlsLayer>();
#elif !defined(TEST) && !defined(BISTRO)
    GetLayerManager().PushLayer<ReelPlaybackLayer>();
    GetLayerManager().PushLayer<ControlsLayer>();
#endif
  }

  void OnSceneReady() override
  {
    auto& registry = m_Registry->Get<YAEngine::ComponentRegistry>();
    auto& threadPool = m_Registry->Get<YAEngine::ThreadPool>();

    RegisterGameComponentSerializers(registry);

#ifdef TEST
    YAEngine::SceneSerializer::Load(
      APP_WORKING_DIR "/Assets/Scenes/test.scene",
      GetScene(), GetAssets(), registry, GetRender(),
      APP_WORKING_DIR, &threadPool);

    m_TestSparkEmitter = GetScene().CreateEntity("spark_emitter_test");
    GetScene().GetTransform(m_TestSparkEmitter).position = glm::dvec3(0.0, 2.0, 0.0);
    GetScene().AddComponent<YAEngine::NoSerializeTag>(m_TestSparkEmitter);

#ifdef YA_EDITOR
    auto& camState = GetScene().GetEditorCameraState();
    camState.position = glm::vec3(0.0f, 18.0f, -8.0f);
    camState.pitch = glm::radians(-60.0f);
    camState.yaw = glm::radians(180.0f);
#endif

#elif defined(BISTRO)
    YAEngine::SceneSerializer::Load(
      APP_WORKING_DIR "/Assets/Scenes/cafe.scene",
      GetScene(), GetAssets(), registry, GetRender(),
      APP_WORKING_DIR, &threadPool);

#elif defined(BISTRO_RACING)
    YAEngine::SceneSerializer::Load(
      APP_WORKING_DIR "/Assets/Scenes/cafe.scene",
      GetScene(), GetAssets(), registry, GetRender(),
      APP_WORKING_DIR, &threadPool);

    SetupDrivableCar();

#else
    YAEngine::SceneSerializer::Load(
      APP_WORKING_DIR "/Assets/Scenes/racing.scene",
      GetScene(), GetAssets(), registry, GetRender(),
      APP_WORKING_DIR, &threadPool);

    if (auto* reel = GetLayerManager().GetLayer<ReelPlaybackLayer>())
      reel->SetReelPath(APP_WORKING_DIR "/Assets/Scenes/racing.reel");

    auto* controls = GetLayerManager().GetLayer<ControlsLayer>();
    if (controls)
    {
      auto view = GetScene().GetView<YAEngine::ModelSourceComponent>();
      for (auto e : view)
      {
        if (GetScene().GetChildByName(e, "wheel-left-front") == entt::null)
          continue;

        GetScene().AddComponent<VehicleComponent>(e);

        auto wheels = std::array<YAEngine::Entity, 4> {
          GetScene().GetChildByName(e, "wheel-left-front"),
          GetScene().GetChildByName(e, "wheel-right-front"),
          GetScene().GetChildByName(e, "wheel-left-rear"),
          GetScene().GetChildByName(e, "wheel-right-rear")
        };

        for (int i = 0; i < 4; i++)
        {
          if (wheels[i] == entt::null) continue;
          auto baseRot = GetScene().GetTransform(wheels[i]).rotation;
          GetScene().AddComponent<WheelComponent>(wheels[i],
            WheelComponent {
              .baseRot = baseRot,
              .isFront = (i < 2)
            });
        }

        controls->SetTarget(e);
        break;
      }
    }

    // Smoke-test transparent pass: mark first opaque glTF material as transparent.
    {
      auto& matMgr = GetAssets().Materials();
      bool done = false;
      matMgr.ForEach([&done](YAEngine::Material& mat)
      {
        if (done) return;
        if (mat.hasAlpha || mat.alphaTest) return;
        mat.transparent = true;
        mat.opacity = 0.35f;
        mat.MarkChanged();
        done = true;
      });
    }
#endif
  }

  void Update(double deltaTime) override
  {
#ifdef TEST
    if (m_TestSparkEmitter != entt::null)
    {
      if (!m_TestSparkTexture)
        m_TestSparkTexture = GetAssets().Textures().Load(
          GetAssets().ResolvePath("Assets/Textures/spark.png"));

      glm::vec3 pos = glm::vec3(GetScene().GetTransform(m_TestSparkEmitter).position);
      m_TestSparkPool.Emit(pos, glm::vec3(0.0f, 1.0f, 0.0f), 1);
      m_TestSparkPool.Update(deltaTime);

      m_TestSparkInstances.clear();
      m_TestSparkPool.FillInstances(m_TestSparkInstances);
      if (!m_TestSparkInstances.empty() && m_TestSparkTexture)
        GetRender().SubmitParticles(m_TestSparkInstances, m_TestSparkTexture);
    }
#endif

#ifdef YA_EDITOR
    if (GetInput().IsKeyPressed(YAEngine::Key::Escape))
    {
      auto followView = GetScene().GetView<FollowCameraComponent, YAEngine::CameraComponent>();
      auto editorView = GetScene().GetView<YAEngine::EditorOnlyTag, YAEngine::CameraComponent>();

      YAEngine::Entity followCam = entt::null;
      YAEngine::Entity editorCam = entt::null;

      for (auto e : followView)
      {
        followCam = e;
        break;
      }

      for (auto e : editorView)
      {
        editorCam = e;
        break;
      }

      if (followCam == entt::null || editorCam == entt::null) return;

      if (GetScene().GetActiveCamera() == followCam)
        GetScene().SetActiveCamera(editorCam);
      else
        GetScene().SetActiveCamera(followCam);
    }
#endif
  }

private:
#ifdef BISTRO_RACING
  // BistroExterior has no terrain, so the car rides a constant plane. kAsphaltY is the wet
  // cobblestone roadway level in front of the bistro (the pedestrian pavement sits at 0.37);
  // the wheel offset drops the model so the tires - lowest point at 0.0039 in model space -
  // land exactly on it.
  static constexpr double kAsphaltY = 0.32;
  static constexpr double kCarScale = 1.2;
  static constexpr double kWheelBottomLocalY = 0.0039;

  void SetupDrivableCar()
  {
    auto* controls = GetLayerManager().GetLayer<ControlsLayer>();
    if (controls == nullptr)
      return;

    YAEngine::Entity car = entt::null;
    for (auto e : GetScene().GetView<VehicleComponent>())
    {
      car = e;
      break;
    }

    if (car == entt::null)
      car = BootstrapDrivableCar();

    if (car == entt::null)
      return;

    AttachWheels(car);

    controls->b_FixedGround = true;
    controls->m_GroundY = GetScene().GetTransform(car).position.y;
    controls->SetTarget(car);
  }

  // Fallback for a scene that predates the car entry. The car is spawned without
  // NoSerializeTag, so the next editor save moves it into cafe.scene for good.
  YAEngine::Entity BootstrapDrivableCar()
  {
    auto modelHandle = GetAssets().Models().Load(
      GetAssets().ResolvePath("Assets/Models/porsche/rose_porsche.glb"), true);
    if (!modelHandle)
      return entt::null;

    auto car = GetAssets().Models().Get(modelHandle).rootEntity;
    double carY = kAsphaltY - kWheelBottomLocalY * kCarScale;

    auto& transform = GetScene().GetTransform(car);
    transform.position = glm::dvec3(-7.0, carY, 10.0);
    transform.rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    transform.scale = glm::vec3(float(kCarScale));
    GetScene().MarkDirty(car);

    GetScene().AddComponent<YAEngine::ColliderComponent>(car,
      YAEngine::ColliderComponent {
        .localOffset = { 0.0f, 0.4f, -0.02f },
        .halfExtents = { 0.55f, 0.4f, 1.39f },
        .isStatic = false,
        .layer = 2u
      });

    GetScene().AddComponent<VehicleComponent>(car);

    return car;
  }

  // Wheels loaded from the scene already carry the component; only a bootstrapped car
  // needs them attached here.
  void AttachWheels(YAEngine::Entity car)
  {
    auto wheels = std::array<YAEngine::Entity, 4> {
      GetScene().GetChildByName(car, "wheel-left-front"),
      GetScene().GetChildByName(car, "wheel-right-front"),
      GetScene().GetChildByName(car, "wheel-left-rear"),
      GetScene().GetChildByName(car, "wheel-right-rear")
    };

    for (int i = 0; i < 4; i++)
    {
      if (wheels[i] == entt::null) continue;
      if (GetScene().HasComponent<WheelComponent>(wheels[i])) continue;

      auto baseRot = GetScene().GetTransform(wheels[i]).rotation;
      GetScene().AddComponent<WheelComponent>(wheels[i],
        WheelComponent {
          .baseRot = baseRot,
          .isFront = (i < 2)
        });
    }
  }
#endif

#ifdef TEST
  YAEngine::Entity m_TestSparkEmitter = entt::null;
  SparkPool m_TestSparkPool;
  YAEngine::TextureHandle m_TestSparkTexture;
  std::vector<YAEngine::ParticleInstance> m_TestSparkInstances;
#endif
};
