#pragma once

#include "Application.h"
#include "ControlsLayer.h"
#include "FreeCamLayer.h"
#include "DebugUILayer.h"
#include "GameComponents.h"

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
    App().PushLayer<YAEngine::FreeCamLayer>();
    App().PushLayer<YAEngine::DebugUILayer>();
  }

  void OnSceneReady() override
  {
    App().GetScene().SetSkybox(App().GetAssetManager().CubeMaps().Load(APP_WORKING_DIR "/Assets/Textures/sky.hdr"));

    key = App().Events().Subscribe<YAEngine::KeyEvent>([&](auto e) { OnKeyBoard(e); });

#ifndef TEST
    App().GetRender().GetGamma() = 1.48f;
    App().GetRender().GetExposure() = 1.24f;
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

      App().GetScene().GetTransform(car).position.y = -0.82f;
      App().GetScene().SetDoubleSided(car);
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

    App().GetRender().GetLight(0).color = glm::vec3(0.0f, 20.0f, 0.0f);
    App().GetRender().GetLight(0).position = glm::vec3(0.0f, 20.0f, 10.0f);
    App().GetRender().GetLight(0).cutOff = 60.0f;
    App().GetRender().GetLight(0).outerCutOff = 90.0f;

    App().GetRender().GetLight(1).color = glm::vec3(20.0f, 0.0f, 0.0f);
    App().GetRender().GetLight(1).position = glm::vec3(0.0f, 20.0f, -10.0f);
    App().GetRender().GetLight(1).cutOff = 60.0f;
    App().GetRender().GetLight(1).outerCutOff = 90.0f;
  }

  void OnDetach() override
  {
    App().Events().Unsubscribe<YAEngine::KeyEvent>(key);
  }

  void OnKeyBoard(const YAEngine::KeyEvent event)
  {
    if (event.action == GLFW_PRESS && event.key == GLFW_KEY_ESCAPE)
    {
      // Find follow camera (ControlsLayer camera) and free camera via ECS
      auto followView = App().GetScene().GetView<FollowCameraComponent, YAEngine::CameraComponent>();
      auto freeView = App().GetScene().GetView<YAEngine::CameraComponent, YAEngine::TransformComponent>();

      YAEngine::Entity followCam = entt::null;
      YAEngine::Entity freeCam = entt::null;

      for (auto e : followView)
      {
        followCam = e;
        break;
      }

      // FreeCam is a camera entity without FollowCameraComponent
      for (auto e : freeView)
      {
        if (!App().GetScene().HasComponent<FollowCameraComponent>(e))
        {
          freeCam = e;
          break;
        }
      }

      if (followCam == entt::null || freeCam == entt::null) return;

      if (App().GetScene().GetActiveCamera() == followCam)
        App().GetScene().SetActiveCamera(freeCam);
      else
        App().GetScene().SetActiveCamera(followCam);
    }
  }

};
