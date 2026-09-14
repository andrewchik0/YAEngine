#include "Editor/Bridge/EditorBridge.h"

#include "Editor/Bridge/BridgeActions.h"
#include "Editor/Bridge/BridgeCapture.h"
#include "Editor/Bridge/BridgeData.h"
#include "Editor/Bridge/BridgeDiscovery.h"
#include "Editor/Bridge/BridgeJson.h"
#include "Editor/Bridge/BridgeMethods.h"
#include "Editor/Bridge/BridgeServer.h"
#include "Editor/Bridge/BridgeUi.h"
#include "Editor/Bridge/LogRingBuffer.h"
#include "Editor/EditorPreferences.h"
#include "Render/Render.h"
#include "Utils/Log.h"
#include "Utils/MainThreadDispatcher.h"
#include "Utils/ServiceRegistry.h"
#include "Utils/Timer.h"
#include "Window.h"

namespace YAEngine
{
  namespace
  {
    constexpr int64_t LOG_TAIL_DEFAULT_COUNT = 200;
    constexpr int64_t LOG_TAIL_MAX_COUNT = 2000;
    // Leaves room under the 4 MB message limit for JSON escaping of the texts
    constexpr size_t LOG_TAIL_BYTE_BUDGET = 3 * 1024 * 1024;
  }

  EditorBridge::EditorBridge()
    : m_Methods(std::make_unique<BridgeMethodRegistry>())
    , m_Capture(std::make_unique<BridgeCapture>())
    , m_Data(std::make_unique<BridgeData>())
    , m_Actions(std::make_unique<BridgeActions>())
    , m_Ui(std::make_unique<BridgeUi>())
  {
  }

  EditorBridge::~EditorBridge()
  {
    Shutdown();
  }

  void EditorBridge::Init(ServiceRegistry& registry, bool enabled)
  {
    m_Registry = &registry;
    RegisterBuiltinMethods();
    auto isCaptureBusy = [this]() { return m_Capture->IsBusy(); };
    auto isMinimized = [this]() { return IsWindowMinimized(); };
    m_Capture->Init(registry, *m_Methods, isMinimized);
    m_Data->Init(registry, *m_Methods, isCaptureBusy);
    m_Actions->Init(registry, *m_Methods, isCaptureBusy, isMinimized);
    m_Ui->Init(registry, *m_Methods, isCaptureBusy, isMinimized);
    b_Enabled = enabled;
    UpdateLogCapture();
  }

  void EditorBridge::OnSceneReady(const std::string& scenePath)
  {
    m_ScenePath = scenePath;
    b_SceneReady = true;
    if (b_Enabled)
      StartListening();
  }

  void EditorBridge::LateUpdate()
  {
    m_Capture->LateUpdate();
    m_Actions->LateUpdate();
    m_Ui->LateUpdate();
  }

  void EditorBridge::Shutdown()
  {
    // Before the listener stops, so the waiting clients still hear why their jobs ended
    m_Capture->Cancel("the editor closed before the shot finished");
    m_Actions->Cancel("the editor closed before the action finished");
    m_Ui->Shutdown("the editor closed before the ui request finished");
    StopListening();
    b_SceneReady = false;

    if (b_LogCaptureInstalled)
    {
      Log::SetSink(nullptr);
      b_LogCaptureInstalled = false;
    }
  }

  void EditorBridge::SetEnabled(bool enabled)
  {
    if (enabled == b_Enabled)
      return;

    b_Enabled = enabled;
    m_StartError.clear();
    UpdateLogCapture();
    if (!b_SceneReady)
      return;

    if (enabled)
      StartListening();
    else
      StopListening();
  }

  uint16_t EditorBridge::GetPort() const
  {
    return m_Server ? m_Server->GetPort() : 0;
  }

  const std::string& EditorBridge::GetError() const
  {
    if (!m_StartError.empty())
      return m_StartError;

    int acceptError = m_Server ? m_Server->GetAcceptError() : 0;
    if (acceptError != m_FormattedAcceptError)
    {
      m_FormattedAcceptError = acceptError;
      m_AcceptErrorText = acceptError != 0
        ? "stopped accepting connections (WSA error " + std::to_string(acceptError) + ")"
        : std::string();
    }
    return m_AcceptErrorText;
  }

  bool EditorBridge::IsWindowMinimized() const
  {
    const Window& window = m_Registry->Get<Window>();
    return window.GetWidth() == 0 || window.GetHeight() == 0;
  }

  void EditorBridge::SetScenePath(const std::string& scenePath)
  {
    if (scenePath == m_ScenePath)
      return;

    m_ScenePath = scenePath;
    if (!m_Server)
      return;

    std::string error;
    if (!WriteDiscovery(error))
      YA_LOG_WARN("Bridge", "Cannot refresh the discovery file: %s", error.c_str());
  }

  size_t EditorBridge::GetClientCount() const
  {
    return m_Server ? m_Server->GetClients().size() : 0;
  }

  const std::vector<BridgeClientInfo>& EditorBridge::GetClients() const
  {
    static const std::vector<BridgeClientInfo> s_NoClients;
    return m_Server ? m_Server->GetClients() : s_NoClients;
  }

  void EditorBridge::DisconnectAll()
  {
    if (m_Server)
      m_Server->DisconnectAll();
  }

  void EditorBridge::StartListening()
  {
    StopListening();
    m_StartError.clear();
    m_Token = GenerateBridgeToken();

    auto server = std::make_shared<BridgeServer>(BridgeServerConfig {
      .dispatcher = &m_Registry->Get<MainThreadDispatcher>(),
      .methods = m_Methods.get(),
      .activity = &m_Activity,
      .scenePath = &m_ScenePath,
      .token = m_Token
    });

    std::string error;
    if (!server->Start(error))
    {
      m_StartError = error;
      YA_LOG_ERROR("Bridge", "Agent bridge failed to start: %s", error.c_str());
      return;
    }

    m_Server = std::move(server);
    m_StartedAt = FormatBridgeTimestampUtc();

    // Without the discovery file nobody can learn the token, so a listener without one is useless
    if (!WriteDiscovery(error))
    {
      StopListening();
      m_StartError = "cannot write the discovery file: " + error;
      YA_LOG_ERROR("Bridge", "Agent bridge stopped: %s", m_StartError.c_str());
      return;
    }

    YA_LOG_INFO("Bridge", "Agent bridge listening on 127.0.0.1:%u, discovery file '%s'",
      static_cast<unsigned>(m_Server->GetPort()), PathToUtf8(GetBridgeDiscoveryPath()).c_str());
  }

  void EditorBridge::StopListening()
  {
    if (!m_Server)
      return;

    RemoveBridgeDiscoveryFile();
    m_Server->Stop();
    m_Server.reset();
    m_Token.clear();
    YA_LOG_INFO("Bridge", "Agent bridge stopped listening");
  }

  bool EditorBridge::WriteDiscovery(std::string& outError) const
  {
    BridgeDiscoveryInfo info {
      .port = m_Server->GetPort(),
      .token = m_Token,
      .scenePath = m_ScenePath,
      .startedAt = m_StartedAt
    };
    return WriteBridgeDiscoveryFile(info, outError);
  }

  void EditorBridge::UpdateLogCapture()
  {
    if (b_Enabled == b_LogCaptureInstalled)
      return;

    Log::SetSink(b_Enabled ? &LogRingBuffer::Sink : nullptr);
    b_LogCaptureInstalled = b_Enabled;
  }

  void EditorBridge::RegisterBuiltinMethods()
  {
    m_Methods->Register("bridge.ping", [](const Json&, const BridgeReply& reply) {
      reply.Ok();
    });

    m_Methods->Register("engine.status", [this](const Json&, const BridgeReply& reply) {
      Render& render = m_Registry->Get<Render>();
      double deltaTime = m_Registry->Get<Timer>().GetDeltaTime();
      VkExtent2D renderExtent = render.GetRenderExtent();
      VkExtent2D outputExtent = render.GetOutputExtent();

      Json result = Json::object();
      result["pid"] = GetBridgeProcessId();
      result["buildConfig"] = GetBridgeBuildConfig();
      result["scenePath"] = m_ScenePath;
      result["frameIndex"] = render.GetGlobalFrameIndex();
      result["fps"] = deltaTime > 0.0 ? 1.0 / deltaTime : 0.0;
      result["frameTimeMs"] = deltaTime * 1000.0;
      result["renderExtent"] = Json::array({ renderExtent.width, renderExtent.height });
      result["outputExtent"] = Json::array({ outputExtent.width, outputExtent.height });
      result["clients"] = GetClientCount();
      result["captureBusy"] = m_Capture->IsBusy();
      reply.Ok(std::move(result));
    });

    m_Methods->Register("engine.quit", [this](const Json&, const BridgeReply& reply) {
      // Sent synchronously on this thread, so the reply is on the wire before the loop ends
      reply.Ok();
      YA_LOG_INFO("Bridge", "An agent client asked the editor to quit");
      m_Registry->Get<Window>().Close();
    });

    m_Methods->Register("log.tail", [](const Json& params, const BridgeReply& reply) {
      int64_t count = LOG_TAIL_DEFAULT_COUNT;
      std::string minLevelName = "info";
      int64_t afterSeq = 0;
      std::string error;
      if (!ReadOptionalParam(params, "count", count, error)
        || !ReadOptionalParam(params, "minLevel", minLevelName, error)
        || !ReadOptionalParam(params, "afterSeq", afterSeq, error))
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
        return;
      }

      if (count < 0 || afterSeq < 0)
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'count' and 'afterSeq' must not be negative");
        return;
      }

      std::optional<LogLevel> minLevel = ParseLogLevelName(minLevelName);
      if (!minLevel)
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'minLevel' must be verbose, info, warning or error");
        return;
      }

      std::vector<LogRingEntry> entries;
      uint64_t lastSeq = 0;
      LogRingBuffer::Get().Tail(static_cast<size_t>(std::min(count, LOG_TAIL_MAX_COUNT)), *minLevel,
        static_cast<uint64_t>(afterSeq), LOG_TAIL_BYTE_BUDGET, entries, lastSeq);

      Json lines = Json::array();
      for (const LogRingEntry& entry : entries)
      {
        Json line = Json::object();
        line["seq"] = entry.seq;
        line["level"] = GetLogLevelName(entry.level);
        line["tag"] = entry.tag;
        line["file"] = entry.file;
        line["line"] = entry.line;
        line["text"] = entry.text;
        lines.push_back(std::move(line));
      }

      Json result = Json::object();
      result["lines"] = std::move(lines);
      result["lastSeq"] = lastSeq;
      reply.Ok(std::move(result));
    });
  }
}
