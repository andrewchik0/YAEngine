#include "Editor/Bridge/BridgeJson.h"

namespace YAEngine
{
  std::string SerializeBridgeMessage(const Json& message)
  {
    std::string line = message.dump(-1, ' ', false, Json::error_handler_t::replace);
    line.push_back('\n');
    return line;
  }

  bool ReadOptionalParam(const Json& params, const char* key, int64_t& inOut, std::string& outError)
  {
    auto it = params.find(key);
    if (it == params.end() || it->is_null())
      return true;

    bool fits = it->is_number_integer()
      && !(it->is_number_unsigned() && it->get<uint64_t>() > uint64_t(std::numeric_limits<int64_t>::max()));
    if (!fits)
    {
      outError = std::string("'") + key + "' must be an integer";
      return false;
    }

    inOut = it->get<int64_t>();
    return true;
  }

  bool ReadOptionalParam(const Json& params, const char* key, std::string& inOut, std::string& outError)
  {
    auto it = params.find(key);
    if (it == params.end() || it->is_null())
      return true;

    if (!it->is_string())
    {
      outError = std::string("'") + key + "' must be a string";
      return false;
    }

    inOut = it->get<std::string>();
    return true;
  }

  bool ReadOptionalParam(const Json& params, const char* key, bool& inOut, std::string& outError)
  {
    auto it = params.find(key);
    if (it == params.end() || it->is_null())
      return true;

    if (!it->is_boolean())
    {
      outError = std::string("'") + key + "' must be a boolean";
      return false;
    }

    inOut = it->get<bool>();
    return true;
  }
}
