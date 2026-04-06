#pragma once

#include "Layer.h"
#include "LayerManager.h"
#include "Window.h"
#include "Input/InputSystem.h"
#include "ControlsLayer.h"
#include "GameComponents.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Render/Render.h"
#include "Assets/AssetManager.h"
#ifdef YA_EDITOR
#include "Editor/EditorCameraLayer.h"
#endif

#include "Vertices.h"

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
    GetScene().SetSkybox(GetAssets().CubeMaps().Load(APP_WORKING_DIR "/Assets/Textures/sky.hdr"));

#ifndef TEST
    GetRender().GetGamma() = 1.9f;
    GetRender().GetExposure() = 1.2f;
    {
      auto carHandle = GetAssets().Models().Load(APP_WORKING_DIR "/Assets/Models/koenigsegg/scene.gltf", true);
      auto car = GetAssets().Models().Get(carHandle).rootEntity;

      GetScene().AddComponent<VehicleComponent>(car);

      auto wheels = std::array<YAEngine::Entity, 4> {
        GetScene().GetChildByName(car, "wheel-left-front"),
        GetScene().GetChildByName(car, "wheel-right-front"),
        GetScene().GetChildByName(car, "wheel-left-back"),
        GetScene().GetChildByName(car, "wheel-right-back")
      };

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
#ifdef YA_EDITOR
      auto* editorCam = GetLayerManager().GetLayer<YAEngine::EditorCameraLayer>();
      if (editorCam)
      {
        editorCam->initialPosition = glm::vec3(0.0f, 18.0f, -8.0f);
        editorCam->initialPitch = glm::radians(-60.0f);
        editorCam->initialYaw = glm::radians(180.0f);
      }
#endif

      auto sphere = GenerateSphere(1.0f, 40, 40);
      auto sphereMesh = GetAssets().Meshes().Load(sphere.vertices, sphere.indices);
      auto box = GenerateBox(2.0f, 2.0f, 2.0f);
      auto boxMesh = GetAssets().Meshes().Load(box.vertices, box.indices);
      auto tallBox = GenerateBox(1.5f, 4.0f, 1.5f);
      auto tallBoxMesh = GetAssets().Meshes().Load(tallBox.vertices, tallBox.indices);
      auto floor = GeneratePlane(40.0f, 10.0f);
      auto floorMesh = GetAssets().Meshes().Load(floor.vertices, floor.indices);

      // Floor - light grey, slightly rough
      {
        auto mat = GetAssets().Materials().Create();
        GetAssets().Materials().Get(mat).albedo = glm::vec3(0.7f);
        GetAssets().Materials().Get(mat).roughness = 0.6f;
        GetAssets().Materials().Get(mat).metallic = 0.0f;
        auto e = GetScene().CreateEntity("Floor");
        GetScene().AddComponent<YAEngine::MeshComponent>(e, floorMesh);
        GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
      }

      // Roughness/metallic gradient - 2 rows of spheres
      for (int i = 0; i < 7; i++)
      {
        float t = i / 6.0f;

        // Top row: dielectric, varying roughness
        {
          auto mat = GetAssets().Materials().Create();
          GetAssets().Materials().Get(mat).albedo = glm::vec3(0.9f, 0.2f, 0.2f);
          GetAssets().Materials().Get(mat).roughness = 0.05f + t * 0.9f;
          GetAssets().Materials().Get(mat).metallic = 0.0f;
          auto e = GetScene().CreateEntity("DielectricSphere_" + std::to_string(i));
          GetScene().AddComponent<YAEngine::MeshComponent>(e, sphereMesh);
          GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
          GetScene().GetTransform(e).position = glm::vec3(-9.0f + i * 3.0f, 1.0f, -6.0f);
        }

        // Bottom row: metallic, varying roughness
        {
          auto mat = GetAssets().Materials().Create();
          GetAssets().Materials().Get(mat).albedo = glm::vec3(0.95f, 0.64f, 0.54f); // copper
          GetAssets().Materials().Get(mat).roughness = 0.05f + t * 0.9f;
          GetAssets().Materials().Get(mat).metallic = 1.0f;
          auto e = GetScene().CreateEntity("MetalSphere_" + std::to_string(i));
          GetScene().AddComponent<YAEngine::MeshComponent>(e, sphereMesh);
          GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
          GetScene().GetTransform(e).position = glm::vec3(-9.0f + i * 3.0f, 1.0f, -2.0f);
        }
      }

      // Emissive sphere - bright orange glow
      {
        auto mat = GetAssets().Materials().Create();
        GetAssets().Materials().Get(mat).albedo = glm::vec3(1.0f, 0.5f, 0.1f);
        GetAssets().Materials().Get(mat).emissivity = glm::vec3(5.0f, 2.5f, 0.5f);
        GetAssets().Materials().Get(mat).roughness = 0.3f;
        GetAssets().Materials().Get(mat).metallic = 0.0f;
        auto e = GetScene().CreateEntity("EmissiveOrange");
        GetScene().AddComponent<YAEngine::MeshComponent>(e, sphereMesh);
        GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
        GetScene().GetTransform(e).position = glm::vec3(-6.0f, 1.5f, 4.0f);
        GetScene().GetTransform(e).scale = glm::vec3(1.5f);
      }

      // Emissive sphere - blue
      {
        auto mat = GetAssets().Materials().Create();
        GetAssets().Materials().Get(mat).albedo = glm::vec3(0.1f, 0.3f, 1.0f);
        GetAssets().Materials().Get(mat).emissivity = glm::vec3(0.5f, 1.5f, 8.0f);
        GetAssets().Materials().Get(mat).roughness = 0.1f;
        GetAssets().Materials().Get(mat).metallic = 0.0f;
        auto e = GetScene().CreateEntity("EmissiveBlue");
        GetScene().AddComponent<YAEngine::MeshComponent>(e, sphereMesh);
        GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
        GetScene().GetTransform(e).position = glm::vec3(6.0f, 1.0f, 4.0f);
      }

      // Mirror box - perfect reflector
      {
        auto mat = GetAssets().Materials().Create();
        GetAssets().Materials().Get(mat).albedo = glm::vec3(0.95f);
        GetAssets().Materials().Get(mat).roughness = 0.02f;
        GetAssets().Materials().Get(mat).metallic = 1.0f;
        auto e = GetScene().CreateEntity("MirrorBox");
        GetScene().AddComponent<YAEngine::MeshComponent>(e, boxMesh);
        GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
        GetScene().GetTransform(e).position = glm::vec3(0.0f, 1.0f, 5.0f);
      }

      // Gold tall box
      {
        auto mat = GetAssets().Materials().Create();
        GetAssets().Materials().Get(mat).albedo = glm::vec3(1.0f, 0.76f, 0.33f);
        GetAssets().Materials().Get(mat).roughness = 0.2f;
        GetAssets().Materials().Get(mat).metallic = 1.0f;
        auto e = GetScene().CreateEntity("GoldTallBox");
        GetScene().AddComponent<YAEngine::MeshComponent>(e, tallBoxMesh);
        GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
        GetScene().GetTransform(e).position = glm::vec3(-3.0f, 2.0f, 8.0f);
      }

      // White diffuse tall box (Cornell box style)
      {
        auto mat = GetAssets().Materials().Create();
        GetAssets().Materials().Get(mat).albedo = glm::vec3(0.9f);
        GetAssets().Materials().Get(mat).roughness = 0.9f;
        GetAssets().Materials().Get(mat).metallic = 0.0f;
        auto e = GetScene().CreateEntity("DiffuseTallBox");
        GetScene().AddComponent<YAEngine::MeshComponent>(e, tallBoxMesh);
        GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
        GetScene().GetTransform(e).position = glm::vec3(3.0f, 2.0f, 8.0f);
      }

      // Point light - warm white, above center (shadow-casting)
      {
        auto e = GetScene().CreateEntity("PointLight_Center");
        GetScene().GetTransform(e).position = glm::vec3(0.0f, 6.0f, 2.0f);
        GetScene().AddComponent<YAEngine::LightComponent>(e, YAEngine::LightComponent {
          .type = YAEngine::LightType::Point,
          .color = glm::vec3(1.0f, 0.95f, 0.8f),
          .intensity = 5.0f,
          .radius = 25.0f,
          .castShadow = true,
        });
      }

      // Point light - red, left side
      {
        auto e = GetScene().CreateEntity("PointLight_Red");
        GetScene().GetTransform(e).position = glm::vec3(-8.0f, 3.0f, 8.0f);
        GetScene().AddComponent<YAEngine::LightComponent>(e, YAEngine::LightComponent {
          .type = YAEngine::LightType::Point,
          .color = glm::vec3(1.0f, 0.2f, 0.1f),
          .intensity = 3.0f,
          .radius = 15.0f,
        });
      }

      // Point light - blue, right side
      {
        auto e = GetScene().CreateEntity("PointLight_Blue");
        GetScene().GetTransform(e).position = glm::vec3(8.0f, 3.0f, 8.0f);
        GetScene().AddComponent<YAEngine::LightComponent>(e, YAEngine::LightComponent {
          .type = YAEngine::LightType::Point,
          .color = glm::vec3(0.1f, 0.3f, 1.0f),
          .intensity = 3.0f,
          .radius = 15.0f,
        });
      }

      // Directional light - sun
      {
        auto e = GetScene().CreateEntity("DirectionalLight_Sun");
        GetScene().GetTransform(e).rotation = glm::quat(glm::vec3(
          glm::radians(-45.0f), glm::radians(30.0f), 0.0f));
        GetScene().AddComponent<YAEngine::LightComponent>(e, YAEngine::LightComponent {
          .type = YAEngine::LightType::Directional,
          .color = glm::vec3(1.0f, 0.98f, 0.9f),
          .intensity = 1.5f,
          .castShadow = true,
        });
      }

      // Spot light - aimed at the boxes (shadow-casting)
      {
        auto e = GetScene().CreateEntity("SpotLight_Boxes");
        GetScene().GetTransform(e).position = glm::vec3(0.0f, 10.0f, 8.0f);
        GetScene().GetTransform(e).rotation = glm::quat(glm::vec3(
          glm::radians(-80.0f), 0.0f, 0.0f));
        GetScene().AddComponent<YAEngine::LightComponent>(e, YAEngine::LightComponent {
          .type = YAEngine::LightType::Spot,
          .color = glm::vec3(1.0f, 0.9f, 0.7f),
          .intensity = 8.0f,
          .radius = 20.0f,
          .innerCone = glm::radians(20.0f),
          .outerCone = glm::radians(30.0f),
          .castShadow = true,
        });
      }

      // Glass-like sphere (dielectric, very smooth)
      {
        auto mat = GetAssets().Materials().Create();
        GetAssets().Materials().Get(mat).albedo = glm::vec3(0.95f, 0.95f, 1.0f);
        GetAssets().Materials().Get(mat).roughness = 0.0f;
        GetAssets().Materials().Get(mat).metallic = 0.0f;
        GetAssets().Materials().Get(mat).specular = 1.0f;
        auto e = GetScene().CreateEntity("GlassSphere");
        GetScene().AddComponent<YAEngine::MeshComponent>(e, sphereMesh);
        GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
        GetScene().GetTransform(e).position = glm::vec3(0.0f, 1.5f, 10.0f);
        GetScene().GetTransform(e).scale = glm::vec3(1.5f);
      }

      // Chrome sphere
      {
        auto mat = GetAssets().Materials().Create();
        GetAssets().Materials().Get(mat).albedo = glm::vec3(0.77f, 0.78f, 0.78f);
        GetAssets().Materials().Get(mat).roughness = 0.05f;
        GetAssets().Materials().Get(mat).metallic = 1.0f;
        auto e = GetScene().CreateEntity("ChromeSphere");
        GetScene().AddComponent<YAEngine::MeshComponent>(e, sphereMesh);
        GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
        GetScene().GetTransform(e).position = glm::vec3(-4.0f, 2.0f, 12.0f);
        GetScene().GetTransform(e).scale = glm::vec3(2.0f);
      }

      // Plastic spheres - saturated colors
      glm::vec3 plasticColors[] = {
        {0.1f, 0.8f, 0.2f}, // green
        {0.8f, 0.8f, 0.05f}, // yellow
        {0.6f, 0.1f, 0.8f}, // purple
      };
      for (int i = 0; i < 3; i++)
      {
        auto mat = GetAssets().Materials().Create();
        GetAssets().Materials().Get(mat).albedo = plasticColors[i];
        GetAssets().Materials().Get(mat).roughness = 0.35f;
        GetAssets().Materials().Get(mat).metallic = 0.0f;
        auto e = GetScene().CreateEntity("PlasticSphere_" + std::to_string(i));
        GetScene().AddComponent<YAEngine::MeshComponent>(e, sphereMesh);
        GetScene().AddComponent<YAEngine::MaterialComponent>(e, mat);
        GetScene().GetTransform(e).position = glm::vec3(4.0f + i * 2.5f, 1.0f, 12.0f);
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
