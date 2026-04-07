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
#include "Render/Render.h"
#include "Assets/AssetManager.h"
#ifdef YA_EDITOR
#include "Editor/EditorCameraLayer.h"
#endif

#define TEST

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

#ifdef TEST
    YAEngine::SceneSerializer::Load(
      APP_WORKING_DIR "/Assets/Scenes/test.scene",
      GetScene(), GetAssets(), registry, GetRender(),
      APP_WORKING_DIR);

#ifdef YA_EDITOR
    auto* editorCam = GetLayerManager().GetLayer<YAEngine::EditorCameraLayer>();
    if (editorCam)
    {
      editorCam->initialPosition = glm::vec3(0.0f, 18.0f, -8.0f);
      editorCam->initialPitch = glm::radians(-60.0f);
      editorCam->initialYaw = glm::radians(180.0f);
    }
#endif

#else
    YAEngine::SceneSerializer::Load(
      APP_WORKING_DIR "/Assets/Scenes/racing.scene",
      GetScene(), GetAssets(), registry, GetRender(),
      APP_WORKING_DIR);

    auto* controls = GetLayerManager().GetLayer<ControlsLayer>();
    if (controls)
    {
      auto view = GetScene().GetView<YAEngine::ModelSourceComponent>();
      for (auto e : view)
      {
        controls->SetTarget(e);
        break;
      }
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
