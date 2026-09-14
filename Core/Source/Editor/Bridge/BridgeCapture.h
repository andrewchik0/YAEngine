#pragma once

#include "Editor/Bridge/BridgeMethods.h"
#include "Editor/Bridge/BridgeTypes.h"
#include "Render/FrameCaptureShotRunner.h"

namespace YAEngine
{
  class ServiceRegistry;

  // capture.targets and capture.shot. A bridge shot runs inside the interactive editor, so
  // everything it overrides - render settings, camera pose, gizmos - is snapshotted before it
  // starts and restored when it ends. Main thread only.
  class BridgeCapture
  {
  public:
    // Publishes BridgeCaptureStatus in the registry.
    void Init(ServiceRegistry& registry, BridgeMethodRegistry& methods, std::function<bool()> isMinimized);
    // Advances a running shot. Runs after every layer's Update so the shot's camera pose
    // overrides the editor camera input of the same frame.
    void LateUpdate();
    // For a shot that can no longer finish: restores the settings and fails its reply.
    void Cancel(std::string_view reason);
    // A bridge shot is running, or a command line capture session has not finished yet.
    bool IsBusy() const;

  private:
    void HandleTargets(const BridgeReply& reply);
    void HandleShot(const Json& params, const BridgeReply& reply);
    void OnShotFinished(const FrameCaptureShotOutcome& outcome);

    ServiceRegistry* m_Registry = nullptr;
    std::function<bool()> m_IsMinimized;
    BridgeCaptureStatus m_Status;
    FrameCaptureShotRunner m_Runner;
    FrameCaptureSettingsSnapshot m_Snapshot;
    std::filesystem::path m_ShotDirectory;
    BridgeReply m_PendingReply;
  };
}
