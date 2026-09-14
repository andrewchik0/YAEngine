#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct BridgeDiscoveryInfo
  {
    uint16_t port = 0;
    std::string token;
    std::string scenePath;
    std::string startedAt;
  };

  // %LOCALAPPDATA%\YAEngine\Bridge\<pid>.json, or an empty path when LOCALAPPDATA is not set.
  std::filesystem::path GetBridgeDiscoveryPath();
  bool WriteBridgeDiscoveryFile(const BridgeDiscoveryInfo& info, std::string& outError);
  void RemoveBridgeDiscoveryFile();

  // Absolute path of the running executable, or an empty path when the OS does not report it.
  std::filesystem::path GetBridgeExecutablePath();
  uint32_t GetBridgeProcessId();
  // "DebugEditor" or "ReleaseEditor".
  const char* GetBridgeBuildConfig();
  // The source tree this editor was built from, baked in by CMake.
  std::filesystem::path GetBridgeRepoRoot();
  // 32 lowercase hex characters from the OS cryptographic generator.
  std::string GenerateBridgeToken();
  // Current UTC time in ISO 8601, e.g. 2026-09-13T08:15:02Z.
  std::string FormatBridgeTimestampUtc();
  // Local wall clock time as HH:MM:SS. Thread-safe.
  std::string FormatBridgeLocalTime(std::chrono::system_clock::time_point time);
}
