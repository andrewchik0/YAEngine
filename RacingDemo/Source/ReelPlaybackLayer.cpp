#include "ReelPlaybackLayer.h"

#include "ControlsLayer.h"
#include "GameComponents.h"

#include "Engine.h"
#include "Input/InputSystem.h"
#include "LayerManager.h"
#include "Render/Render.h"
#include "Scene/Scene.h"
#include "Utils/Log.h"

void ReelPlaybackLayer::Update(double dt)
{
  auto& input = GetInput();
  if (input.IsKeyPressed(YAEngine::Key::F9))
  {
    if (m_Playing) Stop();
    else Start();
  }

  if (input.IsKeyPressed(YAEngine::Key::F10))
  {
    auto* controls = GetLayerManager().GetLayer<ControlsLayer>();
    if (controls && controls->m_Car != entt::null)
    {
      auto& tc = GetScene().GetTransform(controls->m_Car);
      YA_LOG_INFO("Render", "carStart snapshot (copy into .reel):");
      YA_LOG_INFO("Render", "  position: [%.6f, %.6f, %.6f]",
        tc.position.x, tc.position.y, tc.position.z);
      YA_LOG_INFO("Render", "  rotation: [%.6f, %.6f, %.6f, %.6f]",
        tc.rotation.x, tc.rotation.y, tc.rotation.z, tc.rotation.w);
    }
  }

  if (!m_Playing) return;

  m_Elapsed += dt;

  auto* controls = GetLayerManager().GetLayer<ControlsLayer>();
  if (controls)
  {
    auto key = SampleCarInput(m_Reel, m_Elapsed);
    controls->m_InputOverride.active = true;
    controls->m_InputOverride.up    = key.up;
    controls->m_InputOverride.down  = key.down;
    controls->m_InputOverride.left  = key.left;
    controls->m_InputOverride.right = key.right;
  }

  if (m_Elapsed >= m_Reel.duration)
    Stop();
}

void ReelPlaybackLayer::Start()
{
  m_Reel = LoadReel(m_ReelPath);
  if (!m_Reel.valid)
  {
    YA_LOG_INFO("Render", "Reel playback requested but no reel loaded at %s", m_ReelPath.c_str());
    return;
  }

  auto& engine = m_Registry->Get<YAEngine::Engine>();
  auto& render = GetRender();

  if (m_Reel.resetTAA) render.ResetTAAHistory();
  if (m_Reel.resetAutoExposure) render.ResetAutoExposure();

  auto* controls = GetLayerManager().GetLayer<ControlsLayer>();
  if (controls && controls->m_Car != entt::null)
  {
    auto car = controls->m_Car;
    auto& tc = GetScene().GetTransform(car);
    tc.position = m_Reel.carStartPos;
    tc.rotation = m_Reel.carStartRot;
    GetScene().MarkDirty(car);

    if (GetScene().HasComponent<VehicleComponent>(car))
    {
      auto& v = GetScene().GetComponent<VehicleComponent>(car);
      v.speed = 0.0;
      v.wheelsSteer = 0.0;
      v.yawInitialized = false;
    }

    controls->m_InputOverride = ControlsLayer::InputOverride{ .active = true };
  }

  engine.SetReelPlaybackMode(true);
  m_Elapsed = 0.0;
  m_Playing = true;

  YA_LOG_INFO("Render", "Reel playback started (duration=%.2fs)", m_Reel.duration);
}

void ReelPlaybackLayer::Stop()
{
  auto& engine = m_Registry->Get<YAEngine::Engine>();
  engine.SetReelPlaybackMode(false);

  auto* controls = GetLayerManager().GetLayer<ControlsLayer>();
  if (controls)
    controls->m_InputOverride = ControlsLayer::InputOverride{};

  m_Playing = false;
  YA_LOG_INFO("Render", "Reel playback stopped at %.2fs", m_Elapsed);
}
