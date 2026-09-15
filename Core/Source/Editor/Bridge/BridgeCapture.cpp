#include "Editor/Bridge/BridgeCapture.h"

#include "Editor/Bridge/BridgeDiscovery.h"
#include "Editor/Bridge/BridgeTypes.h"
#include "Editor/EditorPreferences.h"
#include "Render/Render.h"
#include "Utils/DebugViews.h"
#include "Utils/Log.h"
#include "Utils/ServiceRegistry.h"

namespace YAEngine
{
  namespace
  {
    // Shot directories kept under Captures/mcp: before a new shot, all but the newest this
    // many are deleted.
    constexpr size_t MAX_KEPT_SHOTS = 64;
    constexpr size_t MAX_SHOT_NAME_LENGTH = 64;
    // Keeps the index inside an int whatever a hand-made directory name holds.
    constexpr size_t MAX_INDEX_DIGITS = 9;

    struct ShotDirectory
    {
      int index = 0;
      std::filesystem::path path;
    };

    // NNN_<name> with at least three digits, the layout a --capture session writes as well.
    std::optional<int> ParseShotIndex(const std::wstring& name)
    {
      size_t digits = 0;
      while (digits < name.size() && name[digits] >= L'0' && name[digits] <= L'9')
        digits++;

      if (digits < 3 || digits > MAX_INDEX_DIGITS || digits >= name.size() || name[digits] != L'_')
        return std::nullopt;

      return std::stoi(name.substr(0, digits));
    }

    // Oldest first.
    std::vector<ShotDirectory> ListShotDirectories(const std::filesystem::path& root)
    {
      std::vector<ShotDirectory> directories;
      std::error_code ec;
      for (const auto& entry : std::filesystem::directory_iterator(root, ec))
      {
        std::error_code typeError;
        if (!entry.is_directory(typeError))
          continue;

        if (std::optional<int> index = ParseShotIndex(entry.path().filename().wstring()))
          directories.push_back(ShotDirectory { .index = *index, .path = entry.path() });
      }

      std::sort(directories.begin(), directories.end(),
        [](const ShotDirectory& a, const ShotDirectory& b) { return a.index < b.index; });
      return directories;
    }

    void PruneShotDirectories(const std::vector<ShotDirectory>& oldestFirst)
    {
      if (oldestFirst.size() <= MAX_KEPT_SHOTS)
        return;

      size_t excess = oldestFirst.size() - MAX_KEPT_SHOTS;
      for (size_t i = 0; i < excess; i++)
      {
        std::error_code ec;
        std::filesystem::remove_all(oldestFirst[i].path, ec);
        if (ec)
        {
          YA_LOG_WARN("Bridge", "Capture: cannot delete the old shot '%s': %s",
            PathToUtf8(oldestFirst[i].path).c_str(), ec.message().c_str());
        }
      }
    }

    // The name comes from an agent and ends up in a path, so it is reduced to characters that
    // can neither leave the capture directory nor trip Windows file naming rules.
    std::string SanitizeShotName(std::string_view name)
    {
      std::string sanitized;
      for (char c : name.substr(0, MAX_SHOT_NAME_LENGTH))
      {
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
          || c == '-' || c == '_';
        sanitized += keep ? c : '_';
      }

      return sanitized.empty() ? std::string("shot") : sanitized;
    }
  }

  void BridgeCapture::Init(ServiceRegistry& registry, BridgeMethodRegistry& methods, std::function<bool()> isMinimized)
  {
    m_Registry = &registry;
    m_IsMinimized = std::move(isMinimized);
    registry.Register<BridgeCaptureStatus>(&m_Status);

    methods.Register("capture.targets", [this](const Json&, const BridgeReply& reply) {
      HandleTargets(reply);
    });

    methods.Register("capture.shot", [this](const Json& params, const BridgeReply& reply) {
      HandleShot(params, reply);
    });
  }

  void BridgeCapture::LateUpdate()
  {
    if (!m_Runner.IsRunning())
      return;

    m_Runner.Tick(m_Registry->Get<Scene>(), m_Registry->Get<Render>());
  }

  void BridgeCapture::Cancel(std::string_view reason)
  {
    if (!m_Runner.IsRunning())
      return;

    m_Runner.Cancel();
    m_Status.shotRunning = false;
    RestoreFrameCaptureSettings(m_Snapshot, m_Registry->Get<Scene>(), m_Registry->Get<Render>(), false);
    YA_LOG_WARN("Bridge", "Capture: shot '%s' abandoned: %.*s",
      PathToUtf8(m_ShotDirectory).c_str(), static_cast<int>(reason.size()), reason.data());

    BridgeReply reply = std::move(m_PendingReply);
    m_PendingReply = BridgeReply();
    reply.Fail(BridgeErrorCode::FAILED, reason);
  }

  bool BridgeCapture::IsBusy() const
  {
    if (m_Runner.IsRunning())
      return true;

    return m_Registry != nullptr && m_Registry->Get<FrameCaptureSessionResult>().running;
  }

  void BridgeCapture::HandleTargets(const BridgeReply& reply)
  {
    FrameCaptureTargetList list = m_Registry->Get<Render>().GetCaptureTargets();

    Json targets = Json::array();
    for (const FrameCaptureTargetInfo& target : list.targets)
    {
      Json entry = Json::object();
      entry["name"] = target.name;
      entry["format"] = target.format;
      entry["extent"] = Json::array({ target.width, target.height });
      entry["resolution"] = target.outputResolution ? "output" : "render";
      entry["managed"] = target.managed;
      targets.push_back(std::move(entry));
    }

    Json aliases = Json::array();
    for (const FrameCaptureAliasInfo& alias : list.aliases)
    {
      Json entry = Json::object();
      entry["name"] = alias.name;
      entry["resolvesTo"] = alias.resolvesTo;
      aliases.push_back(std::move(entry));
    }

    Json debugViews = Json::array();
    for (int view = 0; view < GetDebugViewCount(); view++)
    {
      Json entry = Json::object();
      entry["id"] = view;
      entry["slug"] = GetDebugViewSlug(view);
      entry["name"] = GetDebugViewName(view);
      debugViews.push_back(std::move(entry));
    }

    Json passes = Json::array();
    for (const FrameCapturePassInfo& pass : list.passes)
    {
      Json entry = Json::object();
      entry["name"] = pass.name;
      entry["altName"] = pass.altName.empty() ? Json(nullptr) : Json(pass.altName);
      entry["executionIndex"] = pass.executionIndex;
      entry["colorOutputs"] = pass.colorOutputs;
      entry["storageOutputs"] = pass.storageOutputs;
      entry["depthOutput"] = pass.depthOutput.empty() ? Json(nullptr) : Json(pass.depthOutput);
      entry["enabled"] = pass.enabled;
      passes.push_back(std::move(entry));
    }

    Json result = Json::object();
    result["targets"] = std::move(targets);
    result["aliases"] = std::move(aliases);
    result["debugViews"] = std::move(debugViews);
    result["passes"] = std::move(passes);
    reply.Ok(std::move(result));
  }

  void BridgeCapture::HandleShot(const Json& params, const BridgeReply& reply)
  {
    auto shotParam = params.find("shot");
    if (shotParam == params.end() || !shotParam->is_string())
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS,
        "'shot' must be a string in the --shot grammar, e.g. \"view=normals;exposure=2\"");
      return;
    }

    std::string name;
    std::string error;
    if (!ReadOptionalParam(params, "name", name, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }

    FrameCaptureShot shot;
    if (!ParseFrameCaptureShot(shotParam->get<std::string>(), shot, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }

    // Checked now: Render would only find out on the capture frame, after the whole warmup
    Render& render = m_Registry->Get<Render>();
    if (!shot.afterPass.empty() && !render.HasCapturePass(shot.afterPass))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "unknown pass '" + shot.afterPass + "'; capture.targets lists the passes");
      return;
    }

    // Render holds one capture request at a time, so a second client would take over the first one's frame.
    if (m_Runner.IsRunning())
    {
      reply.Fail(BridgeErrorCode::BUSY, "another capture shot is still running");
      return;
    }
    if (m_Registry->Get<FrameCaptureSessionResult>().running)
    {
      reply.Fail(BridgeErrorCode::BUSY, "a command line capture session is running");
      return;
    }
    if (m_IsMinimized && m_IsMinimized())
    {
      reply.Fail(BridgeErrorCode::BUSY, BRIDGE_MINIMIZED_MESSAGE);
      return;
    }

    std::filesystem::path executable = GetBridgeExecutablePath();
    if (executable.empty())
    {
      reply.Fail(BridgeErrorCode::FAILED, "cannot determine the directory of the editor executable");
      return;
    }

    std::filesystem::path root = executable.parent_path() / "Captures" / "mcp";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
      reply.Fail(BridgeErrorCode::FAILED, "cannot create '" + PathToUtf8(root) + "': " + ec.message());
      return;
    }

    std::vector<ShotDirectory> existing = ListShotDirectories(root);
    int index = existing.empty() ? 0 : existing.back().index + 1;
    PruneShotDirectories(existing);

    shot.name = SanitizeShotName(!name.empty() ? name : !shot.name.empty() ? shot.name : "shot");

    char directoryName[96];
    std::snprintf(directoryName, sizeof(directoryName), "%03d_%s", index, shot.name.c_str());
    std::filesystem::path directory = root / directoryName;
    // Converted before any state changes: a path the narrow code page cannot hold throws here.
    std::string narrowDirectory = directory.string();

    m_ShotDirectory = std::move(directory);
    m_Snapshot = SnapshotFrameCaptureSettings(m_Registry->Get<Scene>(), render);
    m_PendingReply = reply;

    m_Status.shotRunning = true;
    m_Runner.Start(std::move(shot), index, std::move(narrowDirectory),
      [this](const FrameCaptureShotOutcome& outcome) { OnShotFinished(outcome); });
  }

  void BridgeCapture::OnShotFinished(const FrameCaptureShotOutcome& outcome)
  {
    m_Status.shotRunning = false;
    RestoreFrameCaptureSettings(m_Snapshot, m_Registry->Get<Scene>(), m_Registry->Get<Render>(), false);

    Json result = Json::object();
    result["directory"] = PathToUtf8(m_ShotDirectory);
    result["status"] = outcome.status;
    result["warnings"] = outcome.warnings;

    BridgeReply reply = std::move(m_PendingReply);
    m_PendingReply = BridgeReply();
    reply.Ok(std::move(result));
  }
}
