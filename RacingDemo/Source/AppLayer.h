#pragma once

#include "Application.h"
#include "ControlsLayer.h"
#include "FreeCamLayer.h"
#include "DebugUILayer.h"

#include "Vertices.h"

// #define TEST

class AppLayer : public YAEngine::Layer
{
public:
  AppLayer() = default;


  YAEngine::Entity car, road, trees;

  YAEngine::SubscriptionId key {};

  std::array<YAEngine::Entity, 4> wheels {};

  struct WheelState
  {
    glm::dquat baseRot;
    double spinAngle;
  };
  std::array<WheelState, 4> wheelStates {};

  void OnBeforeInit() override
  {
    App().GetWindow().Maximize();
#ifndef TEST
    App().PushLayer<ControlsLayer>();
#endif
    App().PushLayer<YAEngine::FreeCamLayer>();
    App().PushLayer<YAEngine::DebugUILayer>();
  }

  void Init() override
  {
    App().GetScene().SetSkybox(App().GetAssetManager().CubeMaps().Load(APP_WORKING_DIR "/Assets/Textures/sky.hdr"));

    key = App().Events().Subscribe<YAEngine::KeyEvent>([&](auto e) { OnKeyBoard(e); });

#ifndef TEST
    App().GetRender().m_Gamma = 1.48f;
    App().GetRender().m_Exposure = 1.24f;
    {
      auto carHandle = App().GetAssetManager().Models().Load(APP_WORKING_DIR "/Assets/Models/koenigsegg/scene.gltf", true);
      car = App().GetAssetManager().Models().Get(carHandle).rootEntity;
      wheels[0] = App().GetScene().GetChildByName(car, "wheel-left-front");
      wheels[1] = App().GetScene().GetChildByName(car, "wheel-right-front");
      wheels[2] = App().GetScene().GetChildByName(car, "wheel-left-back");
      wheels[3] = App().GetScene().GetChildByName(car, "wheel-right-back");
      wheelStates[0].baseRot = App().GetScene().GetTransform(wheels[0]).rotation;
      wheelStates[1].baseRot = App().GetScene().GetTransform(wheels[1]).rotation;
      wheelStates[2].baseRot = App().GetScene().GetTransform(wheels[2]).rotation;
      wheelStates[3].baseRot = App().GetScene().GetTransform(wheels[3]).rotation;
      App().GetScene().GetTransform(car).position.y = -0.82f;
      App().GetScene().SetDoubleSided(car);
    }

    {
      auto albedoHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_albedo.png");
      auto normalHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_normal.png");
      auto roughnessHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_roughness.png");
      auto metallicHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/tile_metallic.png");

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

    App().GetRender().m_Lights.lights[0].color = glm::vec3(0.0f, 20.0f, 0.0f);
    App().GetRender().m_Lights.lights[0].position = glm::vec3(0.0f, 20.0f, 10.0f);
    App().GetRender().m_Lights.lights[0].cutOff = 60.0f;
    App().GetRender().m_Lights.lights[0].outerCutOff = 90.0f;

    App().GetRender().m_Lights.lights[1].color = glm::vec3(20.0f, 0.0f, 0.0f);
    App().GetRender().m_Lights.lights[1].position = glm::vec3(0.0f, 20.0f, -10.0f);
    App().GetRender().m_Lights.lights[1].cutOff = 60.0f;
    App().GetRender().m_Lights.lights[1].outerCutOff = 90.0f;
  }

  void Destroy() override
  {
    App().Events().Unsubscribe<YAEngine::KeyEvent>(key);
  }

  void OnKeyBoard(const YAEngine::KeyEvent event)
  {
    if (event.action == GLFW_PRESS && event.key == GLFW_KEY_ESCAPE)
    {
      auto cam = App().GetLayer<ControlsLayer>()->camera;
      if (App().GetScene().GetActiveCamera() == cam)
      {
        auto debugCamera = App().GetLayer<YAEngine::FreeCamLayer>()->freeCam;
        App().GetScene().SetActiveCamera(debugCamera);
      }
      else
      {
        App().GetScene().SetActiveCamera(cam);
      }
    }
  }

};