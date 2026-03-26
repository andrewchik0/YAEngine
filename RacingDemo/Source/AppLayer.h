#pragma once

#include "Application.h"
#include "ControlsLayer.h"
#include "GameComponents.h"
#include "Scene/Components.h"

#include "Vertices.h"

// #define TEST

class AppLayer : public YAEngine::Layer
{
public:
  AppLayer() = default;

  YAEngine::SubscriptionId key {};

  void OnAttach() override
  {
    App().GetWindow().Maximize();
#ifndef TEST
    App().PushLayer<ControlsLayer>();
#endif
  }

  void OnSceneReady() override
  {
    App().GetScene().SetSkybox(App().GetAssetManager().CubeMaps().Load(APP_WORKING_DIR "/Assets/Textures/sky.hdr"));

    key = App().Events().Subscribe<YAEngine::KeyEvent>([&](auto e) { OnKeyBoard(e); });

#ifndef TEST
    App().GetRender().GetGamma() = 1.9f;
    App().GetRender().GetExposure() = 1.2f;
    {
      auto carHandle = App().GetAssetManager().Models().Load(APP_WORKING_DIR "/Assets/Models/koenigsegg/scene.gltf", true);
      auto car = App().GetAssetManager().Models().Get(carHandle).rootEntity;

      // Add VehicleComponent to car
      App().GetScene().AddComponent<VehicleComponent>(car);

      auto wheels = std::array<YAEngine::Entity, 4> {
        App().GetScene().GetChildByName(car, "wheel-left-front"),
        App().GetScene().GetChildByName(car, "wheel-right-front"),
        App().GetScene().GetChildByName(car, "wheel-left-back"),
        App().GetScene().GetChildByName(car, "wheel-right-back")
      };

      // Add WheelComponent to each wheel
      for (int i = 0; i < 4; i++)
      {
        auto baseRot = App().GetScene().GetTransform(wheels[i]).rotation;
        App().GetScene().AddComponent<WheelComponent>(wheels[i],
          WheelComponent {
            .baseRot = baseRot,
            .isFront = (i < 2)
          });
      }

      App().GetScene().GetTransform(car).position.y = -0.86f;
      App().GetScene().SetDoubleSided(car);

      auto* controls = App().GetLayer<ControlsLayer>();
      if (controls)
        controls->SetTarget(car);
    }

    {
      auto albedoHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_albedo.png");
      auto normalHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_normal.png", nullptr, true);
      auto roughnessHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_roughness.png", nullptr, true);
      auto metallicHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_metallic.png", nullptr, true);

      auto meshHandle = App().GetAssetManager().Meshes().Load(vertices, indices);
      auto materialHandle = App().GetAssetManager().Materials().Create();

      App().GetAssetManager().Materials().Get(materialHandle).baseColorTexture = albedoHandle;
      App().GetAssetManager().Materials().Get(materialHandle).normalTexture = normalHandle;
      App().GetAssetManager().Materials().Get(materialHandle).roughnessTexture = roughnessHandle;
      App().GetAssetManager().Materials().Get(materialHandle).metallicTexture = metallicHandle;

      auto entity = App().GetScene().CreateEntity("Plane");
      App().GetScene().AddComponent<YAEngine::MeshComponent>(entity, meshHandle);
      App().GetScene().AddComponent<YAEngine::MaterialComponent>(entity, materialHandle);
      App().GetScene().GetTransform(entity).position.y = 0.20f;
    }
#endif

#ifdef TEST
    {
      for (int i = 0; i < 10; i++)
      {
        for (int j = 0; j < 10; j++)
        {
          auto mesh = GenerateSphere(1.0, 40, 40);
          auto meshHandle = App().GetAssetManager().Meshes().Load(mesh.vertices, mesh.indices);
          auto materialHandle = App().GetAssetManager().Materials().Create();

          App().GetAssetManager().Materials().Get(materialHandle).albedo = glm::vec3(0.5, 0.0, 0.0);
          App().GetAssetManager().Materials().Get(materialHandle).roughness = i / 9.0f;
          App().GetAssetManager().Materials().Get(materialHandle).metallic = j / 9.0f;

          auto entity = App().GetScene().CreateEntity("Sphere" + i * 10 + j);
          App().GetScene().AddComponent<YAEngine::MeshComponent>(entity, meshHandle);
          App().GetScene().AddComponent<YAEngine::MaterialComponent>(entity, materialHandle);
          App().GetScene().GetTransform(entity).position = glm::vec3(i * 3.0f, j * 3.0f, 0.0f);
          // App().GetScene().SetDoubleSided(entity);
        }
      }
    }
#endif

  }

  void OnDetach() override
  {
    App().Events().Unsubscribe<YAEngine::KeyEvent>(key);
  }

  void OnKeyBoard(const YAEngine::KeyEvent event)
  {
#ifdef YA_EDITOR
    if (event.action == GLFW_PRESS && event.key == GLFW_KEY_ESCAPE)
    {
      // Find follow camera and editor camera via ECS
      auto followView = App().GetScene().GetView<FollowCameraComponent, YAEngine::CameraComponent>();
      auto editorView = App().GetScene().GetView<YAEngine::EditorOnlyTag, YAEngine::CameraComponent>();

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

      if (App().GetScene().GetActiveCamera() == followCam)
        App().GetScene().SetActiveCamera(editorCam);
      else
        App().GetScene().SetActiveCamera(followCam);
    }
#endif
  }

};
