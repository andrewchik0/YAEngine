#pragma once

#include "Layer.h"
#include "Utils/FrameCaptureSpec.h"

namespace YAEngine
{
  // Drives a FrameCapture session without a human in the loop: applies each shot's render
  // settings, waits for the frame to settle, asks Render for a capture and moves on. Pushed
  // only when the command line armed a capture, so nothing here runs in an ordinary session.
  class FrameCaptureLayer : public Layer
  {
  public:

    void OnAttach() override;
    void LateUpdate(double deltaTime) override;

  private:

    enum class Stage : uint8_t
    {
      ListTargets,
      ApplySettings,
      // A path or mode change resizes the graph; warming up across that boundary would
      // spend the warmup on frames from the previous configuration.
      WaitExtent,
      Warmup,
      WaitAccumulation,
      WaitCapture,
      Finished
    };

    // Everything a shot may override, taken before the first one and put back after the
    // last so an interactive session is left exactly as it was found.
    struct SettingsSnapshot
    {
      RenderPath renderPath = RenderPath::Raster;
      AntialiasingMode antialiasing = AntialiasingMode::None;
      int debugView = 0;
      int pathTraceBounces = 0;
      float pathTraceClamp = 0.0f;
      bool pathTraceDevResolve = false;
      float exposure = 0.0f;
      bool autoExposure = false;
      int tonemapMode = 0;
      bool bloom = false;
      glm::vec3 cameraPosition { 0.0f };
      glm::quat cameraRotation { 1.0f, 0.0f, 0.0f, 0.0f };
      uint32_t viewportWidth = 0;
      uint32_t viewportHeight = 0;
      bool gizmos = true;
    };

    void TruncateOutputDirectory();
    void SnapshotSettings();
    void RestoreSettings();
    void ApplyShot(const FrameCaptureShot& shot);
    void ApplyShotCamera(const FrameCaptureShot& shot);
    void ApplyPinnedResolution();
    void RequestShotCapture();
    void BeginShot();
    void FinishShot();
    void FinishSession();
    std::string GetSessionStatus() const;
    void WriteSession(const std::string& status);
    std::string GetShotDirectory(int index) const;

    const FrameCaptureSpec* m_Spec = nullptr;
    FrameCaptureSessionResult* m_SessionResult = nullptr;

    Stage m_Stage = Stage::ApplySettings;
    int m_ShotIndex = 0;
    int m_FrameInShot = 0;
    int m_WarmupLeft = 0;
    int m_StableFrames = 0;
    int m_AccumWaitFrames = 0;
    int m_LastSampleCount = 0;
    VkExtent2D m_LastRenderExtent {};
    VkExtent2D m_LastOutputExtent {};

    std::string m_ShotStatus;
    std::vector<FrameCaptureSessionShot> m_SessionShots;
    SettingsSnapshot m_Snapshot;
  };
}
