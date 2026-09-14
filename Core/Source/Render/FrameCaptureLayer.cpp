#include "Render/FrameCaptureLayer.h"

#include "Render/Render.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Window.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void FrameCaptureLayer::OnAttach()
  {
    m_Spec = &m_Registry->Get<FrameCaptureSpec>();
    m_SessionResult = &m_Registry->Get<FrameCaptureSessionResult>();

    if (m_Spec->listTargets)
      m_Stage = Stage::ListTargets;

    if (!m_Spec->armed)
      return;

    TruncateOutputDirectory();
    m_Snapshot = SnapshotFrameCaptureSettings(GetScene(), GetRender());
    m_SessionResult->running = true;

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

  void FrameCaptureLayer::OnShotFinished(const FrameCaptureShotOutcome& outcome)
  {
    m_SessionShots.push_back(FrameCaptureSessionShot {
      .index = m_ShotIndex,
      .name = m_Spec->shots[m_ShotIndex].name,
      .dir = std::filesystem::path(outcome.directory).filename().string(),
      .status = outcome.status
    });

    // Rewritten after every shot, so a run that dies half way still leaves a readable
    // session manifest listing what did complete.
    WriteSession(GetSessionStatus());

    m_ShotIndex++;
    if (m_ShotIndex >= int(m_Spec->shots.size()))
      FinishSession();
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

    RestoreFrameCaptureSettings(m_Snapshot, GetScene(), GetRender(), m_Spec->pinnedWidth > 0);
    m_SessionResult->running = false;

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
      m_Stage = m_Spec->armed ? Stage::Shots : Stage::Finished;
      if (m_Stage == Stage::Finished)
        GetWindow().Close();
      return;
    }

    if (m_Stage == Stage::Finished || !m_Spec->armed)
      return;

    if (m_Spec->pinnedWidth > 0)
      ApplyPinnedResolution();

    // The next shot starts on the frame after the previous one finished, as each shot always has.
    if (!m_Runner.IsRunning())
    {
      m_Runner.Start(m_Spec->shots[m_ShotIndex], m_ShotIndex, GetShotDirectory(m_ShotIndex),
        [this](const FrameCaptureShotOutcome& outcome) { OnShotFinished(outcome); });
    }

    m_Runner.Tick(GetScene(), render);
  }
}
