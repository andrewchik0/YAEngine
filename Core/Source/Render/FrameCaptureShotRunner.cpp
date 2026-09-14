#include "Render/FrameCaptureShotRunner.h"

#include "Render/Render.h"
#include "Scene/Components.h"
#include "Utils/CameraOrientation.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    // The path tracer's sample counter can legitimately sit still for a while, but a wait
    // that never advances would hang an unattended run. Give up, capture, and say so.
    constexpr int ACCUMULATION_STALL_FRAMES = 300;
    // The extent has to hold for two consecutive frames before a warmup means anything.
    constexpr int EXTENT_STABLE_FRAMES = 2;
  }

  FrameCaptureSettingsSnapshot SnapshotFrameCaptureSettings(Scene& scene, Render& render)
  {
    FrameCaptureSettingsSnapshot snapshot;
    snapshot.renderPath = render.GetRenderPath();
    snapshot.antialiasing = render.GetAntialiasingMode();
    snapshot.debugView = render.GetDebugView();
    snapshot.pathTraceBounces = render.GetPathTraceMaxBounces();
    snapshot.pathTraceClamp = render.GetPathTraceFireflyClamp();
    snapshot.pathTraceDevResolve = render.IsPathTraceDevResolveEnabled();
    snapshot.exposure = render.GetExposure();
    snapshot.autoExposure = render.GetAutoExposureEnabled();
    snapshot.tonemapMode = render.GetTonemapMode();
    snapshot.bloom = render.GetBloomEnabled();
#ifdef YA_EDITOR
    snapshot.viewportWidth = render.GetViewportWidth();
    snapshot.viewportHeight = render.GetViewportHeight();
    snapshot.gizmos = render.GetGizmosEnabled();
    // The gizmo pass draws into sceneColor, so 'final' would carry light and probe icons
    // over the scene and an agent could not tell one from geometry.
    render.GetGizmosEnabled() = false;
#endif

    Entity camera = scene.GetActiveCamera();
    if (camera != entt::null && scene.HasComponent<LocalTransform>(camera))
    {
      const auto& transform = scene.GetTransform(camera);
      snapshot.camera = camera;
      snapshot.cameraPosition = transform.position;
      snapshot.cameraRotation = transform.rotation;
    }

    return snapshot;
  }

  void RestoreFrameCaptureSettings(const FrameCaptureSettingsSnapshot& snapshot, Scene& scene, Render& render,
    bool restoreViewport)
  {
    render.GetRenderPath() = snapshot.renderPath;
    render.GetAntialiasingMode() = snapshot.antialiasing;
    render.SetDebugView(snapshot.debugView);
    render.GetPathTraceMaxBounces() = snapshot.pathTraceBounces;
    render.GetPathTraceFireflyClamp() = snapshot.pathTraceClamp;
    render.SetPathTraceDevResolve(snapshot.pathTraceDevResolve);
    render.GetExposure() = snapshot.exposure;
    render.GetAutoExposureEnabled() = snapshot.autoExposure;
    render.GetTonemapMode() = snapshot.tonemapMode;
    render.GetBloomEnabled() = snapshot.bloom;
#ifdef YA_EDITOR
    render.GetGizmosEnabled() = snapshot.gizmos;
    if (restoreViewport && snapshot.viewportWidth > 0)
      render.RequestViewportResize(snapshot.viewportWidth, snapshot.viewportHeight);
#else
    (void)restoreViewport;
#endif

    // A command line session is armed before the scene has a camera, so its pose can only go
    // to whichever camera is active by the end. An interactive snapshot knows its camera, and
    // a camera preview started meanwhile must not receive the editor camera's pose.
    Entity camera = snapshot.camera != entt::null ? snapshot.camera : scene.GetActiveCamera();
    if (camera == entt::null || !scene.GetRegistry().valid(camera) || !scene.HasComponent<LocalTransform>(camera))
      return;

    auto& transform = scene.GetTransform(camera);
    transform.position = snapshot.cameraPosition;
    transform.rotation = snapshot.cameraRotation;
  }

  void FrameCaptureShotRunner::Start(FrameCaptureShot shot, int shotIndex, std::string directory,
    FinishedCallback onFinished)
  {
    m_Shot = std::move(shot);
    m_ShotIndex = shotIndex;
    m_Directory = std::move(directory);
    m_OnFinished = std::move(onFinished);
    m_Warnings.clear();
    m_Stage = Stage::ApplySettings;
  }

  void FrameCaptureShotRunner::Cancel()
  {
    m_Stage = Stage::Idle;
    m_OnFinished = nullptr;
    m_Warnings.clear();
  }

  void FrameCaptureShotRunner::BeginShot(Scene& scene, Render& render)
  {
    // Render would only notice on the capture frame, after the whole warmup
    if (!m_Shot.afterPass.empty() && !render.HasCapturePass(m_Shot.afterPass))
    {
      FailShot("unknown pass '" + m_Shot.afterPass + "'; --capture-list-targets and capture.targets list the passes");
      return;
    }

    if (m_Shot.renderPath)          render.GetRenderPath() = *m_Shot.renderPath;
    if (m_Shot.antialiasing)        render.GetAntialiasingMode() = *m_Shot.antialiasing;
    if (m_Shot.debugView)           render.SetDebugView(*m_Shot.debugView);
    if (m_Shot.pathTraceBounces)    render.GetPathTraceMaxBounces() = *m_Shot.pathTraceBounces;
    if (m_Shot.pathTraceClamp)      render.GetPathTraceFireflyClamp() = *m_Shot.pathTraceClamp;
    if (m_Shot.pathTraceDevResolve) render.SetPathTraceDevResolve(*m_Shot.pathTraceDevResolve);
    if (m_Shot.exposure)            render.GetExposure() = *m_Shot.exposure;
    if (m_Shot.autoExposure)        render.GetAutoExposureEnabled() = *m_Shot.autoExposure;
    if (m_Shot.tonemapMode)         render.GetTonemapMode() = *m_Shot.tonemapMode;
    if (m_Shot.bloom)               render.GetBloomEnabled() = *m_Shot.bloom;

    ApplyShotCamera(scene);

    m_FrameInShot = 0;
    m_Status = "ok";
    m_WarmupLeft = m_Shot.warmupFrames;
    m_StableFrames = 0;
    m_AccumWaitFrames = 0;
    m_LastSampleCount = -1;
    m_LastRenderExtent = {};
    m_LastOutputExtent = {};

    // The sample counter has to start from this shot's settings, or an accum wait inherited
    // from the previous shot would pass on the first frame.
    if (m_Shot.accumSamples)
      render.ResetPathTraceAccumulation();

    YA_LOG_INFO("Render", "Capture: shot %03d '%s' begins (%s)",
      m_ShotIndex, m_Shot.name.c_str(),
      m_Shot.requestedBy.empty() ? "current settings" : m_Shot.requestedBy.c_str());

    m_Stage = Stage::WaitExtent;
  }

  void FrameCaptureShotRunner::ApplyShotCamera(Scene& scene) const
  {
    Entity camera = scene.GetActiveCamera();
    if (camera == entt::null || !scene.HasComponent<LocalTransform>(camera))
      return;

    auto& transform = scene.GetTransform(camera);
    if (m_Shot.cameraPosition)
      transform.position = *m_Shot.cameraPosition;

    if (m_Shot.lookAt)
    {
      glm::vec3 forward = *m_Shot.lookAt - transform.position;
      if (glm::length(forward) > 1e-6f)
      {
        float yaw = 0.0f;
        float pitch = 0.0f;
        YawPitchFromForward(glm::normalize(forward), yaw, pitch);
        transform.rotation = MakeYawPitchRotation(yaw, pitch);
      }
    }
    else if (m_Shot.yawDegrees || m_Shot.pitchDegrees)
    {
      float yaw = glm::radians(m_Shot.yawDegrees.value_or(0.0f));
      float pitch = glm::radians(m_Shot.pitchDegrees.value_or(0.0f));
      transform.rotation = MakeYawPitchRotation(yaw, pitch);
    }
  }

  void FrameCaptureShotRunner::FailShot(const std::string& reason)
  {
    YA_LOG_ERROR("Render", "Capture: shot %03d '%s' failed: %s", m_ShotIndex, m_Shot.name.c_str(), reason.c_str());
    m_Status = "failed";
    AddWarning(reason);
    Finish();
  }

  void FrameCaptureShotRunner::RequestShotCapture(Scene& scene, Render& render)
  {
    bool accepted = render.RequestCapture(FrameCaptureRequest {
      .directory = m_Directory,
      .targets = m_Shot.targets,
      .afterPass = m_Shot.afterPass,
      .shotName = m_Shot.name,
      .requestedBy = m_Shot.requestedBy,
      .scenePath = scene.GetScenePath(),
      .shotIndex = m_ShotIndex,
      .frameInShot = m_FrameInShot,
      .frameCount = m_Shot.frames,
      .warmupFrames = m_Shot.warmupFrames,
      .accumSamples = m_Shot.accumSamples.value_or(0)
    });

    // Waiting on would hand this shot the other request's result
    if (!accepted)
      FailShot("another capture request was still waiting for its frame");
  }

  void FrameCaptureShotRunner::AddWarning(const std::string& warning)
  {
    // Every frame of a multi-frame shot skips the same targets for the same reason.
    if (std::find(m_Warnings.begin(), m_Warnings.end(), warning) == m_Warnings.end())
      m_Warnings.push_back(warning);
  }

  void FrameCaptureShotRunner::Finish()
  {
    FrameCaptureShotOutcome outcome {
      .directory = m_Directory,
      .status = m_Status,
      .warnings = std::move(m_Warnings)
    };

    FinishedCallback onFinished = std::move(m_OnFinished);
    m_OnFinished = nullptr;
    m_Warnings.clear();
    m_Stage = Stage::Idle;

    if (onFinished)
      onFinished(outcome);
  }

  void FrameCaptureShotRunner::Tick(Scene& scene, Render& render)
  {
    if (m_Stage == Stage::Idle)
      return;

    // Re-applied every frame while the shot is live, so nothing - editor camera input, a
    // running system, a camera track - can drift the pose between apply and capture.
    if (m_Stage != Stage::ApplySettings)
      ApplyShotCamera(scene);

    switch (m_Stage)
    {
    case Stage::ApplySettings:
      BeginShot(scene, render);
      break;

    case Stage::WaitExtent:
    {
      VkExtent2D renderExtent = render.GetRenderExtent();
      VkExtent2D outputExtent = render.GetOutputExtent();
      bool unchanged = renderExtent.width == m_LastRenderExtent.width
        && renderExtent.height == m_LastRenderExtent.height
        && outputExtent.width == m_LastOutputExtent.width
        && outputExtent.height == m_LastOutputExtent.height;

      m_StableFrames = unchanged ? m_StableFrames + 1 : 0;
      m_LastRenderExtent = renderExtent;
      m_LastOutputExtent = outputExtent;

      if (m_StableFrames >= EXTENT_STABLE_FRAMES)
        m_Stage = Stage::Warmup;
      break;
    }

    case Stage::Warmup:
      if (m_WarmupLeft > 0)
      {
        m_WarmupLeft--;
        break;
      }
      m_Stage = m_Shot.accumSamples ? Stage::WaitAccumulation : Stage::WaitCapture;
      if (m_Stage == Stage::WaitCapture)
        RequestShotCapture(scene, render);
      break;

    case Stage::WaitAccumulation:
    {
      int samples = render.GetPathTraceSampleCount();
      bool rising = samples > m_LastSampleCount;
      m_LastSampleCount = samples;
      m_AccumWaitFrames = rising ? 0 : m_AccumWaitFrames + 1;

      bool reached = samples >= *m_Shot.accumSamples;
      bool stalled = m_AccumWaitFrames >= ACCUMULATION_STALL_FRAMES;
      if (stalled && !reached)
      {
        YA_LOG_WARN("Render", "Capture: accumulation stalled at %d of %d samples, capturing anyway",
          samples, *m_Shot.accumSamples);

        char warning[96];
        std::snprintf(warning, sizeof(warning), "accumulation stalled at %d of %d samples, captured anyway",
          samples, *m_Shot.accumSamples);
        AddWarning(warning);
      }

      if (!reached && !stalled)
        break;

      // Before the request, which finishes the shot when it is refused
      m_Stage = Stage::WaitCapture;
      RequestShotCapture(scene, render);
      break;
    }

    case Stage::WaitCapture:
    {
      FrameCaptureResult result;
      if (!render.ConsumeCaptureResult(result))
        break;

      for (const auto& warning : result.warnings)
        AddWarning(warning);

      if (result.failed)
      {
        m_Status = "failed";
        Finish();
        break;
      }
      if (!result.complete && m_Status == "ok")
        m_Status = "partial";

      m_FrameInShot++;
      if (m_FrameInShot >= m_Shot.frames)
      {
        Finish();
        break;
      }

      // Consecutive frames of a multi-frame shot need no second warmup: the point is the
      // frame-to-frame difference the temporal analysis measures.
      RequestShotCapture(scene, render);
      break;
    }

    case Stage::Idle:
      break;
    }
  }
}
