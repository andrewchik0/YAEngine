#pragma once

#include "Layer.h"
#include "LayerManager.h"
#include "Window.h"
#include "Input/InputSystem.h"
#include "ControlsLayer.h"
#include "GameComponents.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Utils/ThreadPool.h"
#include "Render/Render.h"
#include "Assets/AssetManager.h"
#ifdef YA_EDITOR
#include "Editor/EditorCameraLayer.h"
#endif

// #define TEST

class AppLayer : public YAEngine::Layer
{
public:
  AppLayer() = default;

  void OnAttach() override
  {
    GetWindow().Maximize();
#ifndef TEST
    GetLayerManager().PushLayer<ControlsLayer>();
#endif
  }

  void OnSceneReady() override
  {
    auto& registry = m_Registry->Get<YAEngine::ComponentRegistry>();
    auto& threadPool = m_Registry->Get<YAEngine::ThreadPool>();

#ifdef TEST
    YAEngine::SceneSerializer::Load(
      APP_WORKING_DIR "/Assets/Scenes/test.scene",
      GetScene(), GetAssets(), registry, GetRender(),
      APP_WORKING_DIR, &threadPool);

#ifdef YA_EDITOR
    auto& camState = GetScene().GetEditorCameraState();
    camState.position = glm::vec3(0.0f, 18.0f, -8.0f);
    camState.pitch = glm::radians(-60.0f);
    camState.yaw = glm::radians(180.0f);
#endif

#else
    YAEngine::SceneSerializer::Load(
      APP_WORKING_DIR "/Assets/Scenes/racing.scene",
      GetScene(), GetAssets(), registry, GetRender(),
      APP_WORKING_DIR, &threadPool);

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
          GetScene().GetChildByName(e, "wheel-left-back"),
          GetScene().GetChildByName(e, "wheel-right-back")
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

};
