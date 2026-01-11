#pragma once

#include "Application.h"
#include "ControlsLayer.h"
#include "FreeCamLayer.h"
#include "DebugUILayer.h"

#include "Vertices.h"

class AppLayer : public YAEngine::Layer
{
public:
  AppLayer() = default;


  YAEngine::Entity car, road, trees;

  YAEngine::SubscriptionId key;

  std::array<YAEngine::Entity, 4> wheels;

  struct WheelState
  {
    glm::dquat baseRot;
    double spinAngle;
  };
  std::array<WheelState, 4> wheelStates;

  void OnBeforeInit() override
  {
    App().GetWindow().Maximize();
    App().PushLayer<ControlsLayer>();
    App().PushLayer<YAEngine::FreeCamLayer>();
    App().PushLayer<YAEngine::DebugUILayer>();
  }

  void Init() override
  {
    App().GetScene().SetSkybox(App().GetAssetManager().CubeMaps().Load(APP_WORKING_DIR "/Assets/Textures/sky.hdr"));

    key = App().Events().Subscribe<YAEngine::KeyEvent>([&](auto e) { OnKeyBoard(e); });

    {
      std::vector<glm::mat4> instances;
      for (int i = -50; i < 50; i++)
      {
        instances.push_back(glm::translate(glm::mat4(1.0), glm::vec3(i * 1617.3, 0.0f, 0.0f)));
      }
      auto roadHandle = App().GetAssetManager().Models().LoadInstanced(APP_WORKING_DIR "/Assets/Models/road/scene.gltf", instances);
      road = App().GetAssetManager().Models().Get(roadHandle).rootEntity;
      App().GetScene().GetTransform(road).position.y = 0;
      App().GetScene().GetTransform(road).scale = glm::vec3(0.5f);
      App().GetScene().SetDoubleSided(road);
    }

    {
      auto carHandle = App().GetAssetManager().Models().Load(APP_WORKING_DIR "/Assets/Models/koenigsegg/scene.gltf");
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
      auto albedoHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/grass_albedo.png");
      auto normalHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/grass_normal.png");
      auto roughnessHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/grass_roughness.png");

      auto meshHandle = App().GetAssetManager().Meshes().Load(vertices, indices);
      auto materialHandle = App().GetAssetManager().Materials().Create();

      App().GetAssetManager().Materials().Get(materialHandle).baseColorTexture = albedoHandle;
      App().GetAssetManager().Materials().Get(materialHandle).normalTexture = normalHandle;
      App().GetAssetManager().Materials().Get(materialHandle).roughnessTexture = roughnessHandle;

      auto entity = App().GetScene().CreateEntity("Plane");
      App().GetScene().AddComponent<YAEngine::MeshComponent>(entity, meshHandle);
      App().GetScene().AddComponent<YAEngine::MaterialComponent>(entity, materialHandle);
      App().GetScene().GetTransform(entity).position.y = 0.20f;
    }

    {
      std::vector<glm::mat4> instances;
      for (int i = -150; i < 150; i++)
        for (int j = -3; j <= 3; j++)
        {
          if (i > -45 && i < 45) continue;
          if (j >= -1 && j <= 1) continue;

          auto randomScale = (float)std::rand() / RAND_MAX / 2.0f;
          auto scale = glm::scale(glm::mat4(1.0), glm::vec3(0.3f + randomScale));

          auto randomOffset = glm::vec2((float)std::rand() / RAND_MAX * 10.0f, (float)std::rand() / RAND_MAX * 10.0f);
          auto translate = glm::translate(glm::mat4(1.0), glm::vec3(i * 10 + randomOffset.x, randomOffset.y + j * 10.0f, -.8f));

          auto randomRotation = (float)std::rand() / RAND_MAX * 360.0f;
          auto rotate = glm::rotate(glm::mat4(1.0), glm::radians(randomRotation), glm::vec3(0.0f, 0.0f, 1.0f));

          instances.push_back(translate * scale * rotate);
        }
      for (int i = -55; i < 55; i++)
        for (int j = -25; j < 25; j++)
        {
          if (i > -45 && i < 45) continue;
          if (j >= -1 && j <= 1) continue;

          auto randomScale = (float)std::rand() / RAND_MAX / 2.0f;
          auto scale = glm::scale(glm::mat4(1.0), glm::vec3(0.3f + randomScale));

          auto randomOffset = glm::vec2((float)std::rand() / RAND_MAX * 10.0f, (float)std::rand() / RAND_MAX * 10.0f);
          auto translate = glm::translate(glm::mat4(1.0), glm::vec3(i * 10 + randomOffset.x, randomOffset.y + j * 10.0f, -.8f));

          auto randomRotation = (float)std::rand() / RAND_MAX * 360.0f;
          auto rotate = glm::rotate(glm::mat4(1.0), glm::radians(randomRotation), glm::vec3(0.0f, 0.0f, 1.0f));

          instances.push_back(translate * scale * rotate);
        }
      for (int j = -35; j < 35; j++)
        for (int i = -50; i < 50; i++)
        {
          if (j > -25 && j < 25) continue;

          auto randomScale = (float)std::rand() / RAND_MAX / 2.0f;
          auto scale = glm::scale(glm::mat4(1.0), glm::vec3(0.3f + randomScale));

          auto randomOffset = glm::vec2((float)std::rand() / RAND_MAX * 10.0f, (float)std::rand() / RAND_MAX * 10.0f);
          auto translate = glm::translate(glm::mat4(1.0), glm::vec3(i * 10 + randomOffset.x, randomOffset.y + j * 10.0f, -.8f));

          auto randomRotation = (float)std::rand() / RAND_MAX * 360.0f;
          auto rotate = glm::rotate(glm::mat4(1.0), glm::radians(randomRotation), glm::vec3(0.0f, 0.0f, 1.0f));

          instances.push_back(translate * scale * rotate);
        }
      auto treeHandle = App().GetAssetManager().Models().LoadInstanced(APP_WORKING_DIR "/Assets/Models/tree/scene.gltf", instances);
      trees = App().GetAssetManager().Models().Get(treeHandle).rootEntity;
      App().GetScene().SetDoubleSided(trees);
    }
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