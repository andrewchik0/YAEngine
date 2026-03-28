#pragma once

#include "Layer.h"
#include "LayerManager.h"
#include "Window.h"
#include "InputSystem.h"
#include "ControlsLayer.h"
#include "GameComponents.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Render/Render.h"
#include "Assets/AssetManager.h"

#include "Vertices.h"

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
    GetScene().SetSkybox(GetAssets().CubeMaps().Load(APP_WORKING_DIR "/Assets/Textures/sky.hdr"));

#ifndef TEST
    GetRender().GetGamma() = 1.9f;
    GetRender().GetExposure() = 1.2f;
    {
      auto carHandle = GetAssets().Models().Load(APP_WORKING_DIR "/Assets/Models/koenigsegg/scene.gltf", true);
      auto car = GetAssets().Models().Get(carHandle).rootEntity;

      // Add VehicleComponent to car
      GetScene().AddComponent<VehicleComponent>(car);

      auto wheels = std::array<YAEngine::Entity, 4> {
        GetScene().GetChildByName(car, "wheel-left-front"),
        GetScene().GetChildByName(car, "wheel-right-front"),
        GetScene().GetChildByName(car, "wheel-left-back"),
        GetScene().GetChildByName(car, "wheel-right-back")
      };

      // Add WheelComponent to each wheel
      for (int i = 0; i < 4; i++)
      {
        auto baseRot = GetScene().GetTransform(wheels[i]).rotation;
        GetScene().AddComponent<WheelComponent>(wheels[i],
          WheelComponent {
            .baseRot = baseRot,
            .isFront = (i < 2)
          });
      }

      GetScene().GetTransform(car).position.y = -0.86f;
      GetScene().SetDoubleSided(car);

      auto* controls = GetLayerManager().GetLayer<ControlsLayer>();
      if (controls)
        controls->SetTarget(car);
    }

    {
      auto albedoHandle = GetAssets().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_albedo.png");
      auto normalHandle = GetAssets().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_normal.png", nullptr, true);
      auto roughnessHandle = GetAssets().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_roughness.png", nullptr, true);
      auto metallicHandle = GetAssets().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_metallic.png", nullptr, true);

      auto meshHandle = GetAssets().Meshes().Load(vertices, indices);
      auto materialHandle = GetAssets().Materials().Create();

      GetAssets().Materials().Get(materialHandle).baseColorTexture = albedoHandle;
      GetAssets().Materials().Get(materialHandle).normalTexture = normalHandle;
      GetAssets().Materials().Get(materialHandle).roughnessTexture = roughnessHandle;
      GetAssets().Materials().Get(materialHandle).metallicTexture = metallicHandle;

      auto entity = GetScene().CreateEntity("Plane");
      GetScene().AddComponent<YAEngine::MeshComponent>(entity, meshHandle);
      GetScene().AddComponent<YAEngine::MaterialComponent>(entity, materialHandle);
      GetScene().GetTransform(entity).position.y = 0.20f;
    }
#endif

#ifdef TEST
    {
      for (int i = 0; i < 10; i++)
      {
        for (int j = 0; j < 10; j++)
        {
          auto mesh = GenerateSphere(1.0, 40, 40);
          auto meshHandle = GetAssets().Meshes().Load(mesh.vertices, mesh.indices);
          auto materialHandle = GetAssets().Materials().Create();

          GetAssets().Materials().Get(materialHandle).albedo = glm::vec3(0.5, 0.0, 0.0);
          GetAssets().Materials().Get(materialHandle).roughness = i / 9.0f;
          GetAssets().Materials().Get(materialHandle).metallic = j / 9.0f;

          auto entity = GetScene().CreateEntity(std::string("Sphere") + std::to_string(i * 10 + j));
          GetScene().AddComponent<YAEngine::MeshComponent>(entity, meshHandle);
          GetScene().AddComponent<YAEngine::MaterialComponent>(entity, materialHandle);
          GetScene().GetTransform(entity).position = glm::vec3(i * 3.0f, j * 3.0f, 0.0f);
          // GetScene().SetDoubleSided(entity);
        }
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
