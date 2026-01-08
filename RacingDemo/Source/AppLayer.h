#pragma once

#include "Application.h"
#include "ControlsLayer.h"
#include "FreeCamLayer.h"
#include "DebugUILayer.h"

class AppLayer : public YAEngine::Layer
{
public:
  AppLayer() = default;

  std::vector<YAEngine::Vertex> vertices = {
    {
      { -50, -1.0, -50 },
      { 0, 0 },
      { 0, 1, 0 },
      { 0, 0, 1, 1.0 },
    },
    {
      { -50, -1.0, 50 },
      { 0, 50 },
      { 0, 1, 0 },
      { 0, 0, 1, 1.0 },
    },
    {
      { 50, -1.0, -50 },
      { 50, 0 },
      { 0, 1, 0 },
      { 0, 0, 1, 1.0 },
    },
    {
      { 50, -1.0, 50 },
      { 50, 50 },
      { 0, 1, 0 },
      { 0, 0, 1, 1.0 },
    }
  };

  std::vector<uint32_t> indices = {
    0, 1, 2, 1, 3, 2
  };

  YAEngine::Entity car;

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

    auto carHandle = App().GetAssetManager().Models().Load(APP_WORKING_DIR "/Assets/Models/koenigsegg/scene.gltf");
    car = App().GetAssetManager().Models().Get(carHandle).rootEntity;

    auto rubberAlbedoHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/Rubber/synth-rubber-albedo.png");
    auto rubberMetallicHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/Rubber/synth-rubber-metalness.png");
    auto rubberNormalHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/Rubber/synth-rubber-normal.png");
    auto rubberRoughnessHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Materials/Rubber/synth-rubber-roughness.png");

    auto meshHandle = App().GetAssetManager().Meshes().Load(vertices, indices);
    auto materialHandle = App().GetAssetManager().Materials().Create();

    App().GetAssetManager().Materials().Get(materialHandle).baseColorTexture = rubberAlbedoHandle;
    App().GetAssetManager().Materials().Get(materialHandle).normalTexture = rubberNormalHandle;
    App().GetAssetManager().Materials().Get(materialHandle).roughnessTexture = rubberRoughnessHandle;
    App().GetAssetManager().Materials().Get(materialHandle).metallicTexture = rubberMetallicHandle;

    auto entity = App().GetScene().CreateEntity("Plane");
    App().GetScene().AddComponent<YAEngine::MeshComponent>(entity, meshHandle);
    App().GetScene().AddComponent<YAEngine::MaterialComponent>(entity, materialHandle);

    App().GetScene().GetTransform(car).position.y = -1.07f;
    App().GetScene().SetDoubleSided(car);

    key = App().Events().Subscribe<YAEngine::KeyEvent>([&](auto e) { OnKeyBoard(e); });

    wheels[0] = App().GetScene().GetChildByName(car, "wheel-left-front");
    wheels[1] = App().GetScene().GetChildByName(car, "wheel-right-front");
    wheels[2] = App().GetScene().GetChildByName(car, "wheel-left-back");
    wheels[3] = App().GetScene().GetChildByName(car, "wheel-right-back");
    wheelStates[0].baseRot = App().GetScene().GetTransform(wheels[0]).rotation;
    wheelStates[1].baseRot = App().GetScene().GetTransform(wheels[1]).rotation;
    wheelStates[2].baseRot = App().GetScene().GetTransform(wheels[2]).rotation;
    wheelStates[3].baseRot = App().GetScene().GetTransform(wheels[3]).rotation;
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