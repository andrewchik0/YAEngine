#include "CameraDirectorLayer.h"

#include "ControlsLayer.h"
#include "GameComponents.h"

#include "Input/InputSystem.h"
#include "LayerManager.h"
#include "Render/Render.h"
#include "Scene/Components.h"
#include "Scene/SequencePlayer.h"
#include "Utils/CameraOrientation.h"
#include "Utils/Log.h"

namespace
{
  constexpr std::array<YAEngine::Key, 9> kCameraKeys = {
    YAEngine::Key::D1, YAEngine::Key::D2, YAEngine::Key::D3,
    YAEngine::Key::D4, YAEngine::Key::D5, YAEngine::Key::D6,
    YAEngine::Key::D7, YAEngine::Key::D8, YAEngine::Key::D9
  };

  // Hardcoded F5 spot, read from the editor on 2026-09-17: the car transform and the editor
  // camera pose (yaw and pitch in degrees, the editor convention)
  constexpr glm::vec3 kSpotCarPosition { 41.149372f, 0.31532f, 28.027332f };
  constexpr glm::quat kSpotCarRotation { 0.8179283f, 0.0f, 0.5753201f, 0.0f };
  constexpr glm::vec3 kSpotCameraPosition { 36.315254f, 2.096710f, 29.087421f };
  constexpr float kSpotCameraYaw = -65.807625f;
  constexpr float kSpotCameraPitch = -15.156607f;
}

void CameraDirectorLayer::Update(double deltaTime)
{
  auto& scene = GetScene();
  auto& input = GetInput();
  auto& player = m_Registry->Get<YAEngine::SequencePlayer>();

  if (!scene.GetRegistry().valid(m_Selected))
    m_Selected = scene.GetActiveCamera();

  // A take that ran to its end has already handed the view back; put the shot's first frame
  // up again
  if (b_Playing && !player.IsAdvancing())
  {
    b_Playing = false;
    ShowSelected();
  }

  int32_t pick = -1;
  for (size_t i = 0; i < kCameraKeys.size(); i++)
  {
    if (input.IsKeyPressed(kCameraKeys[i]))
      pick = int32_t(i);
  }
  bool tab = input.IsKeyPressed(YAEngine::Key::Tab);

  if (pick >= 0 || tab)
  {
    auto cameras = CollectCameras();
    if (pick >= 0)
    {
      if (size_t(pick) < cameras.size())
        Select(cameras, size_t(pick));
    }
    else if (!cameras.empty())
    {
      bool backwards = input.IsKeyDown(YAEngine::Key::LeftShift) || input.IsKeyDown(YAEngine::Key::RightShift);
      size_t count = cameras.size();
      auto current = std::find(cameras.begin(), cameras.end(), m_Selected);
      size_t next = 0;
      if (current != cameras.end())
        next = (size_t(current - cameras.begin()) + (backwards ? count - 1 : 1)) % count;
      Select(cameras, next);
    }
  }

  if (input.IsKeyPressed(YAEngine::Key::F9))
    TogglePlayback();

  if (input.IsKeyPressed(YAEngine::Key::F5))
    TeleportToSpot();
}

std::vector<YAEngine::Entity> CameraDirectorLayer::CollectCameras()
{
  auto& scene = GetScene();

  std::vector<YAEngine::Entity> cameras;
  std::vector<YAEngine::Entity> placed;
  for (auto e : scene.GetView<YAEngine::CameraComponent>())
  {
    if (scene.HasComponent<FollowCameraComponent>(e))
      cameras.push_back(e);
    else
      placed.push_back(e);
  }

  std::sort(placed.begin(), placed.end(), [&scene](YAEngine::Entity a, YAEngine::Entity b) {
    return scene.GetName(a) < scene.GetName(b);
  });

  cameras.insert(cameras.end(), placed.begin(), placed.end());
  return cameras;
}

void CameraDirectorLayer::Select(const std::vector<YAEngine::Entity>& cameras, size_t index)
{
  m_Selected = cameras[index];
  b_Playing = false;
  ShowSelected();
  // A cut: history from the previous viewpoint would only ghost into the new one
  GetRender().ResetTAAHistory();

  YA_LOG_INFO("Render", "Camera %zu/%zu '%s'%s", index + 1, cameras.size(),
    GetScene().GetName(m_Selected).c_str(), HasShot(m_Selected) ? ", F9 plays its shot" : "");
}

void CameraDirectorLayer::ShowSelected()
{
  auto& scene = GetScene();
  auto& player = m_Registry->Get<YAEngine::SequencePlayer>();

  player.Stop(scene);
  if (!scene.GetRegistry().valid(m_Selected))
    return;

  scene.SetActiveCamera(m_Selected);
  // The engine sizes only the camera active at startup or on a window resize
  VkExtent2D extent = GetRender().GetSwapChainExtent();
  scene.GetComponent<YAEngine::CameraComponent>(m_Selected).Resize(float(extent.width), float(extent.height));

  if (HasShot(m_Selected))
  {
    const auto& track = scene.GetComponent<YAEngine::CameraTrackComponent>(m_Selected);
    player.Scrub(scene, m_Selected, YAEngine::SequencePlayer::ShotStart(track));
  }
}

void CameraDirectorLayer::TogglePlayback()
{
  auto& scene = GetScene();
  auto& player = m_Registry->Get<YAEngine::SequencePlayer>();

  if (player.IsAdvancing())
  {
    b_Playing = false;
    ShowSelected();
    YA_LOG_INFO("Render", "Sequence playback stopped");
    return;
  }

  if (!scene.GetRegistry().valid(m_Selected))
    return;

  if (HasShot(m_Selected))
  {
    player.Play(scene, m_Selected);
    YA_LOG_INFO("Render", "Shot '%s' started, %.2f - %.2f s", scene.GetName(m_Selected).c_str(),
      player.GetStartTime(), player.GetEndTime());
  }
  else
  {
    if (scene.GetView<YAEngine::MotionPathComponent>().empty())
    {
      YA_LOG_INFO("Render", "'%s' has no shot and the scene has no motion path",
        scene.GetName(m_Selected).c_str());
      return;
    }

    player.Play(scene, entt::null);
    YA_LOG_INFO("Render", "Timeline playback started through '%s', %.2f s",
      scene.GetName(m_Selected).c_str(), player.GetEndTime());
  }

  b_Playing = player.IsAdvancing();
}

void CameraDirectorLayer::TeleportToSpot()
{
  auto* controls = GetLayerManager().GetLayer<ControlsLayer>();
  if (controls == nullptr || controls->m_Car == entt::null)
    return;

  auto& scene = GetScene();
  if (!scene.GetRegistry().valid(m_SpotCamera))
  {
    m_SpotCamera = scene.CreateEntity("spot_camera");
    scene.AddComponent<YAEngine::CameraComponent>(m_SpotCamera);
    scene.AddComponent<YAEngine::NoSerializeTag>(m_SpotCamera);

    auto& camera = scene.GetTransform(m_SpotCamera);
    camera.position = kSpotCameraPosition;
    camera.rotation = YAEngine::MakeYawPitchRotation(glm::radians(kSpotCameraYaw), glm::radians(kSpotCameraPitch));
    scene.MarkDirty(m_SpotCamera);
  }

  auto cameras = CollectCameras();
  auto spot = std::find(cameras.begin(), cameras.end(), m_SpotCamera);
  // First: stopping a session puts the car back where it was, over the teleport
  Select(cameras, size_t(spot - cameras.begin()));

  auto& car = scene.GetTransform(controls->m_Car);
  car.position = kSpotCarPosition;
  car.rotation = kSpotCarRotation;
  scene.MarkDirty(controls->m_Car);

  auto& vehicle = scene.GetComponent<VehicleComponent>(controls->m_Car);
  // The drive reads its heading back from the new rotation on its next frame
  vehicle.yawInitialized = false;
  vehicle.speed = 0.0;
  vehicle.wheelsSteer = 0.0;
  vehicle.tilt = glm::dquat(1, 0, 0, 0);
  vehicle.wasInContact = false;

  YA_LOG_INFO("Render", "Car moved to the F5 spot");
}

bool CameraDirectorLayer::HasShot(YAEngine::Entity camera)
{
  auto* track = GetScene().GetRegistry().try_get<YAEngine::CameraTrackComponent>(camera);
  return track != nullptr && !track->keys.empty();
}
