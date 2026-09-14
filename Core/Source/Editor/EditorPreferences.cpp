#include "Editor/EditorPreferences.h"

#include <yaml-cpp/yaml.h>

#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    std::filesystem::path GetPreferencesPath()
    {
      std::filesystem::path directory = GetEditorAppDataDirectory();
      if (directory.empty())
        return {};

      return directory / "editor.yaml";
    }

    // Opened through the wide path: YAML::LoadFile takes a narrow string.
    YAML::Node ReadPreferencesFile(const std::filesystem::path& path)
    {
      std::ifstream file(path, std::ios::binary);
      if (!file)
        return YAML::Node();

      return YAML::Load(file);
    }
  }

  std::filesystem::path GetEditorAppDataDirectory()
  {
    wchar_t* value = nullptr;
    size_t length = 0;
    if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") != 0 || value == nullptr)
      return {};

    std::filesystem::path directory;
    if (value[0] != L'\0')
      directory = std::filesystem::path(value) / "YAEngine";
    free(value);
    return directory;
  }

  std::string PathToUtf8(const std::filesystem::path& path)
  {
    std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
  }

  bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content, std::string& outError)
  {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
      outError = "cannot create '" + PathToUtf8(path.parent_path()) + "': " + ec.message();
      return false;
    }

    // Per process: two editors saving the shared preferences at once must not write one temporary file
    std::filesystem::path temporary = path;
    temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
    {
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      file.write(content.data(), static_cast<std::streamsize>(content.size()));
      file.close();
      if (!file)
      {
        outError = "cannot write '" + PathToUtf8(temporary) + "'";
        std::filesystem::remove(temporary, ec);
        return false;
      }
    }

    std::filesystem::rename(temporary, path, ec);
    if (ec)
    {
      outError = "cannot replace '" + PathToUtf8(path) + "': " + ec.message();
      std::filesystem::remove(temporary, ec);
      return false;
    }

    return true;
  }

  void EditorPreferences::Load()
  {
    std::filesystem::path path = GetPreferencesPath();
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec))
      return;

    try
    {
      const YAML::Node root = ReadPreferencesFile(path);
      if (!root.IsMap())
        return;

      const YAML::Node mcp = root["mcp"];
      if (!mcp.IsDefined() || !mcp.IsMap())
        return;

      const YAML::Node enabled = mcp["enabled"];
      if (enabled.IsDefined() && enabled.IsScalar())
        mcpEnabled = enabled.as<bool>();
    }
    catch (const YAML::Exception& e)
    {
      YA_LOG_WARN("Editor", "Preferences: cannot read '%s', using defaults: %s",
        PathToUtf8(path).c_str(), e.what());
    }
  }

  bool EditorPreferences::Save() const
  {
    std::filesystem::path path = GetPreferencesPath();
    if (path.empty())
    {
      YA_LOG_ERROR("Editor", "Preferences: LOCALAPPDATA is not set, nothing saved");
      return false;
    }

    // Starts from the file on disk so keys this build does not know about survive the save
    YAML::Node root;
    try
    {
      root = ReadPreferencesFile(path);
    }
    catch (const YAML::Exception& e)
    {
      YA_LOG_WARN("Editor", "Preferences: '%s' is malformed and will be rewritten: %s",
        PathToUtf8(path).c_str(), e.what());
      root = YAML::Node();
    }

    if (!root.IsMap())
      root = YAML::Node(YAML::NodeType::Map);
    if (!root["mcp"].IsMap())
      root["mcp"] = YAML::Node(YAML::NodeType::Map);
    root["mcp"]["enabled"] = mcpEnabled;

    YAML::Emitter out;
    out << root;

    std::string content(out.c_str());
    content.push_back('\n');

    std::string error;
    if (!WriteFileAtomically(path, content, error))
    {
      YA_LOG_ERROR("Editor", "Preferences: %s", error.c_str());
      return false;
    }

    return true;
  }
}
