#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Command line overrides for a single run. Engine registers them so EditorLayer reads them
  // in OnAttach; they are never written back to the preferences file.
  struct EditorPreferenceOverrides
  {
    std::optional<bool> mcpEnabled;
  };

  // Per-user editor settings in %LOCALAPPDATA%\YAEngine\editor.yaml, shared by every checkout
  // and build configuration on the machine.
  struct EditorPreferences
  {
    bool mcpEnabled = true;

    // A missing or unreadable file leaves the defaults in place.
    void Load();
    bool Save() const;
  };

  // %LOCALAPPDATA%\YAEngine, or an empty path when the variable is not set.
  std::filesystem::path GetEditorAppDataDirectory();

  // path::string() throws on characters outside the ANSI code page; JSON and ImGui want UTF-8.
  std::string PathToUtf8(const std::filesystem::path& path);

  // Writes a sibling temp file and renames it over the target, so a concurrent reader sees
  // either the old content or the new one, never a partial file.
  bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content, std::string& outError);
}
