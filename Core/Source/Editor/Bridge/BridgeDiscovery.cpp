#include "Editor/Bridge/BridgeDiscovery.h"

#include <windows.h>

#include "Editor/Bridge/BridgeJson.h"
#include "Editor/Bridge/BridgeTypes.h"
#include "Editor/EditorPreferences.h"

namespace YAEngine
{
  std::filesystem::path GetBridgeExecutablePath()
  {
    constexpr size_t MAX_LENGTH = 32768;
    std::wstring buffer(MAX_PATH, L'\0');
    while (buffer.size() <= MAX_LENGTH)
    {
      DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
      if (length == 0)
        return {};

      if (length < buffer.size())
      {
        buffer.resize(length);
        return std::filesystem::path(buffer);
      }

      buffer.resize(buffer.size() * 2);
    }
    return {};
  }

  std::filesystem::path GetBridgeDiscoveryPath()
  {
    std::filesystem::path directory = GetEditorAppDataDirectory();
    if (directory.empty())
      return {};

    return directory / "Bridge" / (std::to_string(GetBridgeProcessId()) + ".json");
  }

  bool WriteBridgeDiscoveryFile(const BridgeDiscoveryInfo& info, std::string& outError)
  {
    std::filesystem::path path = GetBridgeDiscoveryPath();
    if (path.empty())
    {
      outError = "LOCALAPPDATA is not set";
      return false;
    }

    Json document = Json::object();
    document["protocolVersion"] = BRIDGE_PROTOCOL_VERSION;
    document["pid"] = GetBridgeProcessId();
    document["port"] = info.port;
    document["token"] = info.token;
    document["exePath"] = PathToUtf8(GetBridgeExecutablePath());
    document["buildConfig"] = GetBridgeBuildConfig();
    document["repoRoot"] = PathToUtf8(GetBridgeRepoRoot());
    document["scenePath"] = info.scenePath;
    document["startedAt"] = info.startedAt;

    std::string content = document.dump(2, ' ', false, Json::error_handler_t::replace);
    content.push_back('\n');
    return WriteFileAtomically(path, content, outError);
  }

  void RemoveBridgeDiscoveryFile()
  {
    std::filesystem::path path = GetBridgeDiscoveryPath();
    if (path.empty())
      return;

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  uint32_t GetBridgeProcessId()
  {
    return static_cast<uint32_t>(GetCurrentProcessId());
  }

  const char* GetBridgeBuildConfig()
  {
#ifdef NDEBUG
    return "ReleaseEditor";
#else
    return "DebugEditor";
#endif
  }

  std::filesystem::path GetBridgeRepoRoot()
  {
    return std::filesystem::path(YA_REPO_ROOT).make_preferred();
  }

  std::string GenerateBridgeToken()
  {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";

    // MSVC's random_device is backed by the OS cryptographic generator
    std::random_device device;
    std::string token;
    token.reserve(32);
    for (int word = 0; word < 4; word++)
    {
      uint32_t value = device();
      for (int nibble = 0; nibble < 8; nibble++)
      {
        token.push_back(HEX_DIGITS[value & 0xF]);
        value >>= 4;
      }
    }
    return token;
  }

  std::string FormatBridgeTimestampUtc()
  {
    std::time_t now = std::time(nullptr);
    std::tm utc {};
    gmtime_s(&utc, &now);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
  }

  std::string FormatBridgeLocalTime(std::chrono::system_clock::time_point time)
  {
    std::time_t seconds = std::chrono::system_clock::to_time_t(time);
    std::tm local {};
    localtime_s(&local, &seconds);

    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
  }
}
