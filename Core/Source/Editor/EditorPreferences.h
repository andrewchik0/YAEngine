#pragma once

#include "Pch.h"
#include "Editor/Utils/EditorTheme.h"

namespace YAEngine
{
  // Command line overrides for a single run. Engine registers them so EditorLayer reads them
  // in OnAttach; they are never written back to the preferences file.
  struct EditorPreferenceOverrides
  {
    std::optional<bool> mcpEnabled;
  };

  // What editor.yaml holds
  struct EditorPreferenceValues
  {
    bool mcpEnabled = true;
    // Built-in colors the theme starts from
    EditorThemePreset themePreset;
    // The preset's theme plus the token overrides from the file's theme section; saved back as overrides only
    EditorTheme theme = MakeEditorTheme(themePreset);
    // Open state of collapsible property groups, keyed "<window>/<group>"; only groups toggled away
    // from their default open state are stored
    std::map<std::string, bool> groupOpenStates;
    // Panels shown or hidden against their descriptor default, keyed by window name; panels at their
    // default are not stored
    std::map<std::string, bool> panelVisibility;
    // Viewport toolbar Show menu flags set against their defaults, keyed by flag; flags at their
    // default are not stored
    std::map<std::string, bool> viewportShowFlags;
    // Show menu Node Color key ("irradiance", "ringing"); empty keeps the default
    std::string viewportNodeColor;
    // Editor camera fly speed in meters per second; unset keeps the camera's default
    std::optional<float> cameraSpeed;
  };

  // Per-user editor settings in %LOCALAPPDATA%\YAEngine\editor.yaml, shared by every checkout,
  // build configuration and running editor on the machine.
  struct EditorPreferences : EditorPreferenceValues
  {
    // A missing or unreadable file leaves the defaults in place.
    void Load();
    // Rereads the file and writes only the keys this session changed since it last loaded or saved,
    // so what another editor wrote in the meantime survives. Keys this build does not know survive too.
    bool Save();

  private:
    // The values as last loaded or saved
    EditorPreferenceValues m_Saved;
  };

  // %LOCALAPPDATA%\YAEngine, or an empty path when the variable is not set.
  std::filesystem::path GetEditorAppDataDirectory();

  // path::string() throws on characters outside the ANSI code page; JSON and ImGui want UTF-8.
  std::string PathToUtf8(const std::filesystem::path& path);

  // Writes a sibling temp file and renames it over the target, so a concurrent reader sees
  // either the old content or the new one, never a partial file.
  bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content, std::string& outError);
}
