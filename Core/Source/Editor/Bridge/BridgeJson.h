#pragma once

#include "Pch.h"

#include <nlohmann/json.hpp>

namespace YAEngine
{
  using Json = nlohmann::json;

  // One protocol message: compact JSON and the terminating newline. Invalid UTF-8 (a log line or
  // a path in the ANSI code page) is replaced rather than thrown on.
  std::string SerializeBridgeMessage(const Json& message);

  // Readers for optional request params. An absent or null key leaves the value untouched; a
  // present key of the wrong type returns false with a message for invalid_params.
  bool ReadOptionalParam(const Json& params, const char* key, int64_t& inOut, std::string& outError);
  bool ReadOptionalParam(const Json& params, const char* key, std::string& inOut, std::string& outError);
  bool ReadOptionalParam(const Json& params, const char* key, bool& inOut, std::string& outError);
}
