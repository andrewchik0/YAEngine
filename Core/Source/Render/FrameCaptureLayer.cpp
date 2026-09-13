#include "Render/FrameCaptureLayer.h"

#include "Render/Render.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Window.h"
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

    glm::quat MakeCameraRotation(float yawRadians, float pitchRadians)
    {
      // Same composition order as EditorCameraLayer, so a yaw the editor wrote and a yaw a
      // shot asks for point the same way.
      glm::quat pitch = glm::angleAxis(pitchRadians, glm::vec3(1.0f, 0.0f, 0.0f));
      glm::quat yaw = glm::angleAxis(yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
      return glm::normalize(yaw * pitch);
    }
  }

  void FrameCaptureLayer::OnAttach()
  {
    m_Spec = &m_Registry->Get<FrameCaptureSpec>();
    m_SessionResult = &m_Registry->Get<FrameCaptureSessionResult>();

    if (m_Spec->listTargets)
      m_Stage = Stage::ListTargets;

    if (!m_Spec->armed)
      return;

    TruncateOutputDirectory();
    SnapshotSettings();

    if (m_Spec->pinnedWidth > 0)
    {
#ifndef YA_EDITOR
      YA_LOG_WARN("Render", "Capture: --capture-res needs an editor build; "
        "the render extent follows the swapchain here");
#endif
    }

    YA_LOG_INFO("Render", "Capture: session armed, %zu shot(s) into '%s'",
      m_Spec->shots.size(), m_Spec->outputDir.c_str());
  }

  void FrameCaptureLayer::TruncateOutputDirectory()
  {
    std::error_code ec;
    std::filesystem::create_directories(m_Spec->outputDir, ec);
    if (ec)
    {
      YA_LOG_ERROR("Render", "Capture: cannot create '%s': %s",
        m_Spec->outputDir.c_str(), ec.message().c_str());
      return;
    }

    // A rerun into an existing directory must not leave stale shots next to fresh ones -
    // an agent reading the listing cannot tell which run a directory came from.
    std::filesystem::remove(std::filesystem::path(m_Spec->outputDir) / "session.json", ec);

    for (const auto& entry : std::filesystem::directory_iterator(m_Spec->outputDir, ec))
    {
      if (!entry.is_directory())
        continue;

      const std::string name = entry.path().filename().string();
      auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
      bool numbered = name.size() > 3 && isDigit(name[0]) && isDigit(name[1])
        && isDigit(name[2]) && name[3] == '_';
      if (numbered)
        std::filesystem::remove_all(entry.path(), ec);
    }
  }

  void FrameCaptureLayer::SnapshotSettings()
  {
    auto& render = GetRender();
    m_Snapshot.renderPath = render.GetRenderPath();
    m_Snapshot.antialiasing = render.GetAntialiasingMode();
    m_Snapshot.debugView = render.GetDebugView();
    m_Snapshot.pathTraceBounces = render.GetPathTraceMaxBounces();
    m_Snapshot.pathTraceClamp = render.GetPathTraceFireflyClamp();
    m_Snapshot.pathTraceDevResolve = render.IsPathTraceDevResolveEnabled();
    m_Snapshot.exposure = render.GetExposure();
    m_Snapshot.autoExposure = render.GetAutoExposureEnabled();
    m_Snapshot.tonemapMode = render.GetTonemapMode();
    m_Snapshot.bloom = render.GetBloomEnabled();
#ifdef YA_EDITOR
    m_Snapshot.viewportWidth = render.GetViewportWidth();
    m_Snapshot.viewportHeight = render.GetViewportHeight();
    m_Snapshot.gizmos = render.GetGizmosEnabled();
    // The gizmo pass draws into sceneColor, so 'final' would carry light and probe icons
    // over the scene and an agent could not tell one from geometry.
    render.GetGizmosEnabled() = false;
#endif

    Entity camera = GetScene().GetActiveCamera();
    if (camera != entt::null && GetScene().HasComponent<LocalTransform>(camera))
    {
      const auto& transform = GetScene().GetTransform(camera);
      m_Snapshot.cameraPosition = transform.position;
      m_Snapshot.cameraRotation = transform.rotation;
    }
  }

  void FrameCaptureLayer::RestoreSettings()
  {
    auto& render = GetRender();
    render.GetRenderPath() = m_Snapshot.renderPath;
    render.GetAntialiasingMode() = m_Snapshot.antialiasing;
    render.SetDebugView(m_Snapshot.debugView);
    render.GetPathTraceMaxBounces() = m_Snapshot.pathTraceBounces;
    render.GetPathTraceFireflyClamp() = m_Snapshot.pathTraceClamp;
    render.SetPathTraceDevResolve(m_Snapshot.pathTraceDevResolve);
    render.GetExposure() = m_Snapshot.exposure;
    render.GetAutoExposureEnabled() = m_Snapshot.autoExposure;
    render.GetTonemapMode() = m_Snapshot.tonemapMode;
    render.GetBloomEnabled() = m_Snapshot.bloom;
#ifdef YA_EDITOR
    render.GetGizmosEnabled() = m_Snapshot.gizmos;
    if (m_Spec->pinnedWidth > 0 && m_Snapshot.viewportWidth > 0)
      render.RequestViewportResize(m_Snapshot.viewportWidth, m_Snapshot.viewportHeight);
#endif

    Entity camera = GetScene().GetActiveCamera();
    if (camera != entt::null && GetScene().HasComponent<LocalTransform>(camera))
    {
      auto& transform = GetScene().GetTransform(camera);
      transform.position = m_Snapshot.cameraPosition;
      transform.rotation = m_Snapshot.cameraRotation;
    }
  }

  void FrameCaptureLayer::ApplyShot(const FrameCaptureShot& shot)
  {
    auto& render = GetRender();

    if (shot.renderPath)          render.GetRenderPath() = *shot.renderPath;
    if (shot.antialiasing)        render.GetAntialiasingMode() = *shot.antialiasing;
    if (shot.debugView)           render.SetDebugView(*shot.debugView);
    if (shot.pathTraceBounces)    render.GetPathTraceMaxBounces() = *shot.pathTraceBounces;
    if (shot.pathTraceClamp)      render.GetPathTraceFireflyClamp() = *shot.pathTraceClamp;
    if (shot.pathTraceDevResolve) render.SetPathTraceDevResolve(*shot.pathTraceDevResolve);
    if (shot.exposure)            render.GetExposure() = *shot.exposure;
    if (shot.autoExposure)        render.GetAutoExposureEnabled() = *shot.autoExposure;
    if (shot.tonemapMode)         render.GetTonemapMode() = *shot.tonemapMode;
    if (shot.bloom)               render.GetBloomEnabled() = *shot.bloom;

    ApplyShotCamera(shot);
  }

  void FrameCaptureLayer::ApplyShotCamera(const FrameCaptureShot& shot)
  {
    Entity camera = GetScene().GetActiveCamera();
    if (camera == entt::null || !GetScene().HasComponent<LocalTransform>(camera))
      return;

    auto& transform = GetScene().GetTransform(camera);
    if (shot.cameraPosition)
      transform.position = *shot.cameraPosition;

    if (shot.lookAt)
    {
      glm::vec3 forward = *shot.lookAt - transform.position;
      if (glm::length(forward) > 1e-6f)
      {
        forward = glm::normalize(forward);
        float pitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
        float yaw = std::atan2(-forward.x, -forward.z);
        transform.rotation = MakeCameraRotation(yaw, pitch);
      }
    }
    else if (shot.yawDegrees || shot.pitchDegrees)
    {
      float yaw = glm::radians(shot.yawDegrees.value_or(0.0f));
      float pitch = glm::radians(shot.pitchDegrees.value_or(0.0f));
      transform.rotation = MakeCameraRotation(yaw, pitch);
    }
  }

  void FrameCaptureLayer::ApplyPinnedResolution()
  {
#ifdef YA_EDITOR
    auto& render = GetRender();
    if (render.GetViewportWidth() == m_Spec->pinnedWidth
      && render.GetViewportHeight() == m_Spec->pinnedHeight)
      return;

    // Re-requested every frame rather than once: the viewport panel asks for its own docked
    // size on the first frame it renders, which would otherwise undo the pin. The panel only
    // re-requests when its own size changes, so this settles after one exchange.
    render.RequestViewportResize(m_Spec->pinnedWidth, m_Spec->pinnedHeight);

    Entity camera = GetScene().GetActiveCamera();
    if (camera != entt::null && GetScene().HasComponent<CameraComponent>(camera))
    {
      GetScene().GetComponent<CameraComponent>(camera).Resize(
        float(m_Spec->pinnedWidth), float(m_Spec->pinnedHeight));
    }
#endif
  }

  std::string FrameCaptureLayer::GetShotDirectory(int index) const
  {
    char name[256];
    std::snprintf(name, sizeof(name), "%03d_%s", index, m_Spec->shots[index].name.c_str());
    return m_Spec->outputDir + "/" + name;
  }

  void FrameCaptureLayer::RequestShotCapture()
  {
    const auto& shot = m_Spec->shots[m_ShotIndex];
    GetRender().RequestCapture(FrameCaptureRequest {
      .directory = GetShotDirectory(m_ShotIndex),
      .targets = shot.targets,
      .shotName = shot.name,
      .requestedBy = shot.requestedBy,
      .scenePath = GetScene().GetScenePath(),
      .shotIndex = m_ShotIndex,
      .frameInShot = m_FrameInShot,
      .frameCount = shot.frames,
      .warmupFrames = shot.warmupFrames,
      .accumSamples = shot.accumSamples.value_or(0)
    });
  }

  void FrameCaptureLayer::BeginShot()
  {
    const auto& shot = m_Spec->shots[m_ShotIndex];
    ApplyShot(shot);

    m_FrameInShot = 0;
    m_ShotStatus = "ok";
    m_WarmupLeft = shot.warmupFrames;
    m_StableFrames = 0;
    m_AccumWaitFrames = 0;
    m_LastSampleCount = -1;
    m_LastRenderExtent = {};
    m_LastOutputExtent = {};

    // The sample counter has to start from this shot's settings, or an accum wait inherited
    // from the previous shot would pass on the first frame.
    if (shot.accumSamples)
      GetRender().ResetPathTraceAccumulation();

    YA_LOG_INFO("Render", "Capture: shot %03d '%s' begins (%s)",
      m_ShotIndex, shot.name.c_str(),
      shot.requestedBy.empty() ? "current settings" : shot.requestedBy.c_str());

    m_Stage = Stage::WaitExtent;
  }

  void FrameCaptureLayer::FinishShot()
  {
    m_SessionShots.push_back(FrameCaptureSessionShot {
      .index = m_ShotIndex,
      .name = m_Spec->shots[m_ShotIndex].name,
      .dir = std::filesystem::path(GetShotDirectory(m_ShotIndex)).filename().string(),
      .status = m_ShotStatus
    });

    // Rewritten after every shot, so a run that dies half way still leaves a readable
    // session manifest listing what did complete.
    WriteSession(GetSessionStatus());

    m_ShotIndex++;
    if (m_ShotIndex >= int(m_Spec->shots.size()))
      FinishSession();
    else
      m_Stage = Stage::ApplySettings;
  }

  std::string FrameCaptureLayer::GetSessionStatus() const
  {
    std::string status = "ok";
    for (const auto& shot : m_SessionShots)
    {
      if (shot.status == "failed")
        return "failed";
      if (shot.status == "partial")
        status = "partial";
    }

    return status;
  }

  void FrameCaptureLayer::FinishSession()
  {
    std::string status = GetSessionStatus();
    WriteSession(status);

    if (status == "failed")
      m_SessionResult->exitCode = 3;
    else if (status == "partial")
      m_SessionResult->exitCode = 2;

    RestoreSettings();

    YA_LOG_INFO("Render", "Capture: session %s, %zu shot(s) in '%s'",
      status.c_str(), m_SessionShots.size(), m_Spec->outputDir.c_str());

    m_Stage = Stage::Finished;

    if (m_Spec->exitWhenDone)
      GetWindow().Close();
  }

  void FrameCaptureLayer::WriteSession(const std::string& status)
  {
    GetRender().WriteCaptureSession(FrameCaptureSessionInfo {
      .outputDir = m_Spec->outputDir,
      .commandLine = m_Spec->commandLine,
      .scenePath = GetScene().GetScenePath(),
      .status = status,
      .shots = m_SessionShots
    });
  }

  void FrameCaptureLayer::LateUpdate(double deltaTime)
  {
    auto& render = GetRender();

    if (m_Stage == Stage::ListTargets)
    {
      render.LogCaptureTargets();
      m_Stage = m_Spec->armed ? Stage::ApplySettings : Stage::Finished;
      if (m_Stage == Stage::Finished)
        GetWindow().Close();
      return;
    }

    if (m_Stage == Stage::Finished || !m_Spec->armed)
      return;

    if (m_Spec->pinnedWidth > 0)
      ApplyPinnedResolution();

    const auto& shot = m_Spec->shots[m_ShotIndex];

    // Re-applied every frame while the shot is live, so nothing - editor camera input, a
    // running system, a camera track - can drift the pose between apply and capture.
    if (m_Stage != Stage::ApplySettings)
      ApplyShotCamera(shot);

    switch (m_Stage)
    {
    case Stage::ApplySettings:
      BeginShot();
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
      m_Stage = shot.accumSamples ? Stage::WaitAccumulation : Stage::WaitCapture;
      if (m_Stage == Stage::WaitCapture)
        RequestShotCapture();
      break;

    case Stage::WaitAccumulation:
    {
      int samples = render.GetPathTraceSampleCount();
      bool rising = samples > m_LastSampleCount;
      m_LastSampleCount = samples;
      m_AccumWaitFrames = rising ? 0 : m_AccumWaitFrames + 1;

      bool reached = samples >= *shot.accumSamples;
      bool stalled = m_AccumWaitFrames >= ACCUMULATION_STALL_FRAMES;
      if (stalled && !reached)
        YA_LOG_WARN("Render", "Capture: accumulation stalled at %d of %d samples, capturing anyway",
          samples, *shot.accumSamples);

      if (!reached && !stalled)
        break;

      RequestShotCapture();
      m_Stage = Stage::WaitCapture;
      break;
    }

    case Stage::WaitCapture:
    {
      FrameCaptureResult result;
      if (!render.ConsumeCaptureResult(result))
        break;

      if (result.failed)
      {
        m_ShotStatus = "failed";
        FinishShot();
        break;
      }
      if (!result.complete && m_ShotStatus == "ok")
        m_ShotStatus = "partial";

      m_FrameInShot++;
      if (m_FrameInShot >= shot.frames)
      {
        FinishShot();
        break;
      }

      // Consecutive frames of a multi-frame shot need no second warmup: the point is the
      // frame-to-frame difference the temporal analysis measures.
      RequestShotCapture();
      break;
    }

    case Stage::ListTargets:
    case Stage::Finished:
      break;
    }
  }
}
