#pragma once

#include "Pch.h"
#include "Editor/Bridge/BridgeTypes.h"

namespace YAEngine
{
  class BridgeActions;
  class BridgeCapture;
  class BridgeData;
  class BridgeMethodRegistry;
  class BridgeServer;
  class BridgeUi;
  class ServiceRegistry;

  // The editor side of the agent bridge: enable state, the listener, the discovery file, log
  // capture and the built-in methods. Main thread only.
  class EditorBridge
  {
  public:
    EditorBridge();
    ~EditorBridge();

    EditorBridge(const EditorBridge&) = delete;
    EditorBridge& operator=(const EditorBridge&) = delete;

    // Registers the built-in methods and, when enabled, starts capturing the log.
    void Init(ServiceRegistry& registry, bool enabled);
    // Listening starts here rather than in Init: a client that finds the discovery file then
    // gets answers instead of waiting out the scene load on a dispatcher nobody drains yet.
    void OnSceneReady(const std::string& scenePath);
    // Advances jobs that span frames, such as a capture shot or a deferred action, whether or
    // not the listener runs.
    void LateUpdate();
    void Shutdown();

    // Applies immediately. Persisting the choice is up to the caller.
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return b_Enabled; }
    bool IsListening() const { return m_Server != nullptr; }
    uint16_t GetPort() const;
    // Why the listener failed to start or stopped accepting; empty while all is well.
    const std::string& GetError() const;

    // Rewrites the discovery file when the path actually changed.
    void SetScenePath(const std::string& scenePath);

    // Where other editor code registers its methods.
    BridgeMethodRegistry& GetMethods() { return *m_Methods; }
    // Where editor code registers the operations agents run through actions.run.
    BridgeActions& GetActions() { return *m_Actions; }

    size_t GetClientCount() const;
    const std::vector<BridgeClientInfo>& GetClients() const;
    const std::deque<BridgeActivityRecord>& GetActivity() const { return m_Activity; }
    void DisconnectAll();

  private:
    void StartListening();
    void StopListening();
    bool WriteDiscovery(std::string& outError) const;
    void UpdateLogCapture();
    void RegisterBuiltinMethods();
    // The state Engine::Run skips Update and LateUpdate in, so jobs that span frames stand still.
    bool IsWindowMinimized() const;

    ServiceRegistry* m_Registry = nullptr;
    std::unique_ptr<BridgeMethodRegistry> m_Methods;
    std::unique_ptr<BridgeCapture> m_Capture;
    std::unique_ptr<BridgeData> m_Data;
    std::unique_ptr<BridgeActions> m_Actions;
    std::unique_ptr<BridgeUi> m_Ui;
    std::shared_ptr<BridgeServer> m_Server;
    std::deque<BridgeActivityRecord> m_Activity;

    std::string m_ScenePath;
    std::string m_Token;
    std::string m_StartedAt;
    std::string m_StartError;
    // GetError formats the accept error when it changes rather than on every panel frame
    mutable int m_FormattedAcceptError = 0;
    mutable std::string m_AcceptErrorText;

    bool b_Enabled = false;
    bool b_SceneReady = false;
    bool b_LogCaptureInstalled = false;
  };
}
