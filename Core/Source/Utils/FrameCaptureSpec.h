#pragma once

#include "Pch.h"

// The only engine headers this file needs: two dependency-free enums, so a parsed shot
// carries a validated mode rather than a string the layer has to re-check.
#include "Render/AntialiasingMode.h"
#include "Render/RenderPath.h"

namespace YAEngine
{
  // One capture configuration. Every override is optional: an unset field leaves whatever
  // the scene and the current render state already hold, and the manifest records the value
  // that was actually in effect rather than the one that was asked for.
  struct FrameCaptureShot
  {
    std::string name;
    // The raw --shot string, copied into the manifest so a capture names its own recipe.
    std::string requestedBy;
    std::vector<std::string> targets;

    std::optional<RenderPath> renderPath;
    std::optional<AntialiasingMode> antialiasing;
    std::optional<int> debugView;
    std::optional<int> pathTraceBounces;
    std::optional<float> pathTraceClamp;
    std::optional<bool> pathTraceDevResolve;
    // Hold still until the path tracer has averaged at least this many samples.
    std::optional<int> accumSamples;
    std::optional<float> exposure;
    std::optional<bool> autoExposure;
    std::optional<int> tonemapMode;
    std::optional<bool> bloom;

    std::optional<glm::vec3> cameraPosition;
    // Yaw and pitch are derived from cameraPosition and lookAt when this is set; an agent
    // knows where an object is but not which yaw points at it.
    std::optional<glm::vec3> lookAt;
    std::optional<float> yawDegrees;
    std::optional<float> pitchDegrees;

    int warmupFrames = 60;
    int frames = 1;
  };

  struct FrameCaptureSpec
  {
    // Set by --capture alone. Nothing else in the engine reacts to the other fields, so a
    // disarmed spec never reaches the sequencer layer at all.
    bool armed = false;
    bool exitWhenDone = false;
    bool listTargets = false;
    std::string outputDir;
    // Zero means "leave the viewport at whatever size it already has".
    uint32_t pinnedWidth = 0;
    uint32_t pinnedHeight = 0;
    std::string commandLine;
    std::vector<FrameCaptureShot> shots;
  };

  // Where the capture session publishes its outcome, so main can hand it back as the
  // process exit code: 0 ok, 2 partial, 3 failed.
  struct FrameCaptureSessionResult
  {
    int exitCode = 0;
  };

  // What the sequencer asks Render to dump at the end of the frame Draw is about to finish.
  struct FrameCaptureRequest
  {
    std::string directory;
    std::vector<std::string> targets;
    std::string shotName;
    std::string requestedBy;
    std::string scenePath;
    int shotIndex = 0;
    // Position of this frame inside a multi-frame shot, and how many frames it dumps.
    int frameInShot = 0;
    int frameCount = 1;
    int warmupFrames = 0;
    int accumSamples = 0;
  };

  struct FrameCaptureResult
  {
    // False when at least one target was skipped; the reasons are in warnings.
    bool complete = true;
    // A target name that resolves to nothing fails the whole shot: capturing an empty
    // directory silently is the failure mode this tool exists to avoid.
    bool failed = false;
    std::vector<std::string> warnings;
  };

  // One entry of the session manifest's shot list.
  struct FrameCaptureSessionShot
  {
    int index = 0;
    std::string name;
    std::string dir;
    std::string status;
  };

  struct FrameCaptureSessionInfo
  {
    std::string outputDir;
    std::string commandLine;
    std::string scenePath;
    std::string status;
    std::vector<FrameCaptureSessionShot> shots;
  };

  // Parses argv into a spec. Returns false, after logging the reason, on any malformed
  // flag or shot key - the caller is expected to abort before the window opens.
  bool ParseFrameCaptureSpec(int argc, char** argv, FrameCaptureSpec& outSpec);

  // Debug view id <-> name. The table mirrors the DEBUG_VIEW_* ids in
  // Core/Shared/FrameUniforms.h and the labels in RenderSettingsPanel; it lives here
  // because the panel is editor-only and both the parser and the manifest need it.
  const char* GetDebugViewName(int view);
  // Accepts a decimal id or a slug ("pt-max-contrib"). Returns -1 for anything else.
  int ParseDebugView(std::string_view text);
  int GetDebugViewCount();
  const char* GetDebugViewSlug(int view);
}
