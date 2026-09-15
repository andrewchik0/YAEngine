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

    // Through a const node, since operator[] on a non-const one adds the key. A missing key then yields an
    // invalid node whose type queries throw, so IsDefined is asked first.
    bool IsMapEntry(const YAML::Node& node, const char* key)
    {
      const YAML::Node value = node[key];
      return value.IsDefined() && value.IsMap();
    }

    // The map at key, replacing whatever else is stored there
    YAML::Node RequireMapEntry(YAML::Node& node, const char* key)
    {
      if (!IsMapEntry(node, key))
        node[key] = YAML::Node(YAML::NodeType::Map);
      return node[key];
    }

    void RemoveEntryIfEmpty(YAML::Node& node, const char* key)
    {
      if (IsMapEntry(node, key) && node[key].size() == 0)
        node.remove(key);
    }

    bool ReadThemeToken(const YAML::Node& value, float& outValue)
    {
      float parsed = 0.0f;
      if (!value.IsScalar() || !YAML::convert<float>::decode(value, parsed) || !std::isfinite(parsed))
        return false;

      outValue = parsed;
      return true;
    }

    bool ReadThemeToken(const YAML::Node& value, glm::vec2& outValue)
    {
      glm::vec2 parsed(0.0f);
      if (!value.IsSequence() || value.size() != 2
        || !ReadThemeToken(value[0], parsed.x) || !ReadThemeToken(value[1], parsed.y))
        return false;

      outValue = parsed;
      return true;
    }

    bool ReadThemeToken(const YAML::Node& value, glm::vec4& outValue)
    {
      return value.IsScalar() && ParseEditorThemeColor(value.Scalar(), outValue);
    }

    YAML::Node MakeThemeTokenNode(float value)
    {
      return YAML::Node(value);
    }

    YAML::Node MakeThemeTokenNode(const glm::vec2& value)
    {
      YAML::Node node(YAML::NodeType::Sequence);
      node.push_back(value.x);
      node.push_back(value.y);
      node.SetStyle(YAML::EmitterStyle::Flow);
      return node;
    }

    YAML::Node MakeThemeTokenNode(const glm::vec4& value)
    {
      return YAML::Node(FormatEditorThemeColor(value));
    }

    // The preset is read first: the tokens are overrides of its theme
    void LoadTheme(const YAML::Node& section, EditorThemePreset& preset, EditorTheme& theme)
    {
      if (!section.IsDefined() || section.IsNull())
        return;

      if (!section.IsMap())
      {
        YA_LOG_WARN("Editor", "Preferences: 'theme' is not a map, using the default theme");
        return;
      }

      const YAML::Node palette = section["palette"];
      if (palette.IsDefined() && !(palette.IsScalar() && ParseEditorThemePalette(palette.Scalar(), preset.palette)))
      {
        YA_LOG_WARN("Editor", "Preferences: 'theme.palette' is not a known palette, using '%s'",
          GetEditorThemePaletteKey(preset.palette));
      }

      const YAML::Node mode = section["mode"];
      if (mode.IsDefined() && !(mode.IsScalar() && ParseEditorThemeMode(mode.Scalar(), preset.mode)))
      {
        YA_LOG_WARN("Editor", "Preferences: 'theme.mode' is neither 'dark' nor 'light', using '%s'",
          GetEditorThemeModeKey(preset.mode));
      }

      theme = MakeEditorTheme(preset);
      VisitEditorThemeTokens([&](const char* key, auto member)
      {
        const YAML::Node value = section[key];
        if (value.IsDefined() && !ReadThemeToken(value, theme.*member))
          YA_LOG_WARN("Editor", "Preferences: theme key '%s' is malformed, using the preset's value", key);
      });

      ClampEditorTheme(theme);
    }

    // Tokens are stored as overrides of the preset's theme: a changed token equal to the preset's value is
    // removed, so a user who went back to it keeps getting the preset's current value. A new preset rewrites
    // every token against it. Returns whether anything changed.
    bool SaveTheme(YAML::Node& root, const EditorPreferenceValues& values, const EditorPreferenceValues& saved)
    {
      const bool presetChanged = values.themePreset != saved.themePreset;
      if (presetChanged)
      {
        const EditorThemePreset defaults;
        auto storeKey = [&](const char* key, const char* value, bool isDefault)
        {
          if (!isDefault)
            RequireMapEntry(root, "theme")[key] = value;
          else if (IsMapEntry(root, "theme"))
            root["theme"].remove(key);
        };
        storeKey("palette", GetEditorThemePaletteKey(values.themePreset.palette),
          values.themePreset.palette == defaults.palette);
        storeKey("mode", GetEditorThemeModeKey(values.themePreset.mode), values.themePreset.mode == defaults.mode);
      }

      const EditorTheme presetTheme = MakeEditorTheme(values.themePreset);
      bool changed = presetChanged;
      VisitEditorThemeTokens([&](const char* key, auto member)
      {
        if (!presetChanged && values.theme.*member == saved.theme.*member)
          return;

        changed = true;
        if (values.theme.*member != presetTheme.*member)
          RequireMapEntry(root, "theme")[key] = MakeThemeTokenNode(values.theme.*member);
        else if (IsMapEntry(root, "theme"))
          root["theme"].remove(key);
      });

      RemoveEntryIfEmpty(root, "theme");
      return changed;
    }

    // Sections of name -> bool entries (group open states, panel visibility)
    void LoadBoolMap(const YAML::Node& section, const char* sectionName, std::map<std::string, bool>& values)
    {
      if (!section.IsDefined() || section.IsNull())
        return;

      if (!section.IsMap())
      {
        YA_LOG_WARN("Editor", "Preferences: '%s' is not a map, its entries use their defaults", sectionName);
        return;
      }

      for (const auto& entry : section)
      {
        bool value = false;
        if (entry.first.IsScalar() && entry.second.IsScalar() && YAML::convert<bool>::decode(entry.second, value))
          values[entry.first.Scalar()] = value;
        else
          YA_LOG_WARN("Editor", "Preferences: a malformed '%s' entry is ignored", sectionName);
      }
    }

    // Writes the entries this session set or changed and removes the ones it removed. Returns whether
    // any entry changed.
    bool SaveBoolMap(YAML::Node& parent, const char* sectionName, const std::map<std::string, bool>& values,
      const std::map<std::string, bool>& saved)
    {
      bool changed = false;
      for (const auto& [key, value] : values)
      {
        auto previous = saved.find(key);
        if (previous != saved.end() && previous->second == value)
          continue;

        RequireMapEntry(parent, sectionName)[key] = value;
        changed = true;
      }

      for (const auto& [key, value] : saved)
      {
        if (values.contains(key))
          continue;

        if (IsMapEntry(parent, sectionName))
          parent[sectionName].remove(key);
        changed = true;
      }

      RemoveEntryIfEmpty(parent, sectionName);
      return changed;
    }

    void LoadViewport(const YAML::Node& section, EditorPreferenceValues& values)
    {
      if (!section.IsDefined() || section.IsNull())
        return;

      if (!section.IsMap())
      {
        YA_LOG_WARN("Editor", "Preferences: 'viewport' is not a map, the viewport toolbar uses its defaults");
        return;
      }

      LoadBoolMap(section["show"], "viewport.show", values.viewportShowFlags);

      const YAML::Node nodeColor = section["node_color"];
      if (nodeColor.IsDefined() && !nodeColor.IsNull())
      {
        if (nodeColor.IsScalar())
          values.viewportNodeColor = nodeColor.Scalar();
        else
          YA_LOG_WARN("Editor", "Preferences: 'viewport.node_color' is malformed, using its default");
      }

      const YAML::Node speed = section["camera_speed"];
      if (speed.IsDefined() && !speed.IsNull())
      {
        float parsed = 0.0f;
        if (ReadThemeToken(speed, parsed) && parsed > 0.0f)
          values.cameraSpeed = parsed;
        else
          YA_LOG_WARN("Editor", "Preferences: 'viewport.camera_speed' is malformed, using the default speed");
      }
    }

    bool SaveViewport(YAML::Node& root, const EditorPreferenceValues& values, const EditorPreferenceValues& saved)
    {
      const bool showChanged = values.viewportShowFlags != saved.viewportShowFlags;
      const bool nodeColorChanged = values.viewportNodeColor != saved.viewportNodeColor;
      const bool speedChanged = values.cameraSpeed != saved.cameraSpeed;
      if (!showChanged && !nodeColorChanged && !speedChanged)
        return false;

      YAML::Node section = RequireMapEntry(root, "viewport");

      if (showChanged)
        SaveBoolMap(section, "show", values.viewportShowFlags, saved.viewportShowFlags);

      if (nodeColorChanged)
      {
        if (values.viewportNodeColor.empty())
          section.remove("node_color");
        else
          section["node_color"] = values.viewportNodeColor;
      }

      if (speedChanged)
      {
        if (values.cameraSpeed)
          section["camera_speed"] = *values.cameraSpeed;
        else
          section.remove("camera_speed");
      }

      RemoveEntryIfEmpty(root, "viewport");
      return true;
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
    if (!path.empty() && std::filesystem::exists(path, ec))
    {
      try
      {
        const YAML::Node root = ReadPreferencesFile(path);
        if (root.IsMap())
        {
          // 'layout' from older builds is left alone: the dock layout version now lives in each imgui.ini
          LoadTheme(root["theme"], themePreset, theme);
          LoadBoolMap(root["groups"], "groups", groupOpenStates);
          LoadBoolMap(root["panels"], "panels", panelVisibility);
          LoadViewport(root["viewport"], *this);

          const YAML::Node mcp = root["mcp"];
          if (mcp.IsDefined() && mcp.IsMap())
          {
            const YAML::Node enabled = mcp["enabled"];
            if (enabled.IsDefined() && enabled.IsScalar())
              mcpEnabled = enabled.as<bool>();
          }
        }
      }
      catch (const YAML::Exception& e)
      {
        YA_LOG_WARN("Editor", "Preferences: cannot read '%s', using defaults: %s",
          PathToUtf8(path).c_str(), e.what());
      }
    }

    m_Saved = *this;
  }

  bool EditorPreferences::Save()
  {
    std::filesystem::path path = GetPreferencesPath();
    if (path.empty())
    {
      YA_LOG_ERROR("Editor", "Preferences: LOCALAPPDATA is not set, nothing saved");
      return false;
    }

    // Starts from the file on disk, so keys this build does not know and keys another editor wrote survive
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

    // A hand-edited file can hold shapes the merge below trips over; that costs this save, not the editor
    std::string content;
    try
    {
      bool changed = false;
      if (mcpEnabled != m_Saved.mcpEnabled)
      {
        RequireMapEntry(root, "mcp")["enabled"] = mcpEnabled;
        changed = true;
      }
      changed |= SaveTheme(root, *this, m_Saved);
      changed |= SaveBoolMap(root, "groups", groupOpenStates, m_Saved.groupOpenStates);
      changed |= SaveBoolMap(root, "panels", panelVisibility, m_Saved.panelVisibility);
      changed |= SaveViewport(root, *this, m_Saved);

      if (!changed)
        return true;

      YAML::Emitter out;
      out << root;
      content = out.c_str();
      content.push_back('\n');
    }
    catch (const YAML::Exception& e)
    {
      YA_LOG_ERROR("Editor", "Preferences: cannot update '%s', nothing saved: %s", PathToUtf8(path).c_str(), e.what());
      return false;
    }

    std::string error;
    if (!WriteFileAtomically(path, content, error))
    {
      YA_LOG_ERROR("Editor", "Preferences: %s", error.c_str());
      return false;
    }

    m_Saved = *this;
    return true;
  }
}
