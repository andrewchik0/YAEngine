#pragma once

#include "Layer.h"
#include "Render/FrameCaptureShotRunner.h"
#include "Utils/FrameCaptureSpec.h"

namespace YAEngine
{
  // Drives a command line FrameCapture session without a human in the loop: runs each shot
  // through the shot runner, keeps the session manifest and publishes the exit code. Pushed
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
      Shots,
      Finished
    };

    void TruncateOutputDirectory();
    void ApplyPinnedResolution();
    void OnShotFinished(const FrameCaptureShotOutcome& outcome);
    void FinishSession();
    std::string GetSessionStatus() const;
    void WriteSession(const std::string& status);
    std::string GetShotDirectory(int index) const;

    const FrameCaptureSpec* m_Spec = nullptr;
    FrameCaptureSessionResult* m_SessionResult = nullptr;

    Stage m_Stage = Stage::Shots;
    int m_ShotIndex = 0;

    std::vector<FrameCaptureSessionShot> m_SessionShots;
    // Taken once before the first shot and put back after the last, not around each shot.
    FrameCaptureSettingsSnapshot m_Snapshot;
    FrameCaptureShotRunner m_Runner;
  };
}
