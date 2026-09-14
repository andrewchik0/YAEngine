#pragma once

#include "Pch.h"

namespace YAEngine
{
  inline constexpr int BRIDGE_PROTOCOL_VERSION = 1;
  // Tried first so a client configured by hand keeps working across restarts; when it cannot be
  // bound the OS picks a port, which discovery reports.
  inline constexpr uint16_t BRIDGE_PREFERRED_PORT = 4242;
  // A longer incoming line closes the connection; a longer reply is replaced by an error.
  inline constexpr size_t BRIDGE_MAX_MESSAGE_BYTES = 4 * 1024 * 1024;
  inline constexpr size_t BRIDGE_MAX_ACTIVITY_RECORDS = 100;

  namespace BridgeErrorCode
  {
    inline constexpr const char* UNAUTHORIZED = "unauthorized";
    inline constexpr const char* UNKNOWN_METHOD = "unknown_method";
    inline constexpr const char* INVALID_PARAMS = "invalid_params";
    inline constexpr const char* NOT_FOUND = "not_found";
    inline constexpr const char* BUSY = "busy";
    inline constexpr const char* FAILED = "failed";
    inline constexpr const char* INTERNAL = "internal";
  }

  // Engine::Run skips Update and LateUpdate while the window is minimized, so a request that
  // finishes in a later frame would sit there until the window is restored.
  inline constexpr const char* BRIDGE_MINIMIZED_MESSAGE =
    "the editor window is minimized, and this request only completes while the editor draws; restore the window and retry";

  // One answered request, for the AI Agent panel.
  struct BridgeActivityRecord
  {
    // Local HH:MM:SS, formatted once so the panel draws the list without allocating
    std::string time;
    std::string client;
    std::string method;
    // "ok" or the error code
    std::string result;
    double durationMs = 0.0;
  };

  // A client that completed hello.
  struct BridgeClientInfo
  {
    uint64_t connectionId = 0;
    std::string name;
    // Local HH:MM:SS
    std::string connectedAt;
  };

  // Published in the service registry. Editor UI that issues capture requests of its own cannot
  // reach the bridge, yet has to stay away while a bridge shot owns Render's single request.
  struct BridgeCaptureStatus
  {
    bool shotRunning = false;
  };
}
