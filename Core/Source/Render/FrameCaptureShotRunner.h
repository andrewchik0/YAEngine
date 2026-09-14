#pragma once

#include "Pch.h"
#include "Scene/Scene.h"
#include "Utils/FrameCaptureSpec.h"

namespace YAEngine
{
  class Render;

  // Everything a shot may override, taken before a capture and put back after it so an
  // interactive session is left exactly as it was found.
  struct FrameCaptureSettingsSnapshot
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
    // The camera the pose was read from; null when no camera was active yet.
    Entity camera = entt::null;
    glm::vec3 cameraPosition { 0.0f };
    glm::quat cameraRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    bool gizmos = true;
  };

  // Also switches the editor gizmos off until the restore.
  FrameCaptureSettingsSnapshot SnapshotFrameCaptureSettings(Scene& scene, Render& render);
  // restoreViewport puts back the viewport size a pinned capture resolution replaced.
  void RestoreFrameCaptureSettings(const FrameCaptureSettingsSnapshot& snapshot, Scene& scene, Render& render,
    bool restoreViewport);

  struct FrameCaptureShotOutcome
  {
    std::string directory;
    // "ok", "partial" or "failed", the words the session manifest uses.
    std::string status;
    // Each distinct reason a target was skipped or the shot failed, in the order they came up.
    std::vector<std::string> warnings;
  };

  // Runs one shot into one directory: applies its overrides, waits for the extent to settle,
  // warms up, optionally waits for path trace accumulation, then has Render dump each frame of
  // the shot. It changes settings but never restores them - that is up to whoever took the
  // snapshot, since a session of several shots restores only once at its end.
  class FrameCaptureShotRunner
  {
  public:
    using FinishedCallback = std::function<void(const FrameCaptureShotOutcome& outcome)>;

    // Nothing is applied until the next Tick.
    void Start(FrameCaptureShot shot, int shotIndex, std::string directory, FinishedCallback onFinished);
    // Once per frame from a LateUpdate: the pose re-applied here then wins over the camera
    // input of the same frame. The callback runs from inside Tick, with the runner idle again.
    void Tick(Scene& scene, Render& render);
    // Drops the shot without calling back. A capture already requested is still written.
    void Cancel();
    bool IsRunning() const { return m_Stage != Stage::Idle; }

  private:
    enum class Stage : uint8_t
    {
      Idle,
      ApplySettings,
      // A path or mode change resizes the graph; warming up across that boundary would
      // spend the warmup on frames from the previous configuration.
      WaitExtent,
      Warmup,
      WaitAccumulation,
      WaitCapture
    };

    void BeginShot(Scene& scene, Render& render);
    void ApplyShotCamera(Scene& scene) const;
    // A refused request fails the shot and finishes it.
    void RequestShotCapture(Scene& scene, Render& render);
    void FailShot(const std::string& reason);
    void AddWarning(const std::string& warning);
    void Finish();

    FrameCaptureShot m_Shot;
    int m_ShotIndex = 0;
    std::string m_Directory;
    FinishedCallback m_OnFinished;

    Stage m_Stage = Stage::Idle;
    int m_FrameInShot = 0;
    int m_WarmupLeft = 0;
    int m_StableFrames = 0;
    int m_AccumWaitFrames = 0;
    int m_LastSampleCount = 0;
    VkExtent2D m_LastRenderExtent {};
    VkExtent2D m_LastOutputExtent {};

    std::string m_Status;
    std::vector<std::string> m_Warnings;
  };
}
