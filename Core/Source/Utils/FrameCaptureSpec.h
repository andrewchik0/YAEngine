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
    // Render graph pass (name or alt name) to capture right after, instead of at the end of the
    // frame. With a pass set and no targets key, targets stays empty: only Render knows which
    // images the pass writes, and those are the default.
    std::string afterPass;

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

  // Where the capture session publishes its state and outcome, so main can hand the outcome
  // back as the process exit code: 0 ok, 2 partial, 3 failed.
  struct FrameCaptureSessionResult
  {
    int exitCode = 0;
    // From arming until the last shot finished. Other capture clients stay away meanwhile,
    // since Render services one capture request at a time.
    bool running = false;
  };

  // What the sequencer asks Render to dump at the end of the frame Draw is about to finish.
  struct FrameCaptureRequest
  {
    std::string directory;
    std::vector<std::string> targets;
    // Pass name or alt name to dump right after; empty dumps the finished frame.
    std::string afterPass;
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

  // One render graph resource as capture sees it.
  struct FrameCaptureTargetInfo
  {
    std::string name;
    // The capture format name, or "UNSUPPORTED" when capture cannot decode the resource.
    std::string format;
    uint32_t width = 0;
    uint32_t height = 0;
    bool outputResolution = false;
    // False for an imported image, which the graph neither allocates nor resizes.
    bool managed = true;
    uint32_t mipLevels = 1;
    bool depth = false;
  };

  struct FrameCaptureAliasInfo
  {
    std::string name;
    // Graph resource names the alias stands for in the current state; aliases that follow the
    // render path or the TAA ping-pong change between frames.
    std::vector<std::string> resolvesTo;
    // The fixed wording --capture-list-targets prints.
    std::string description;
  };

  // One render graph pass as capture sees it, for after=<pass>.
  struct FrameCapturePassInfo
  {
    std::string name;
    // Empty for a pass without an alternate label; either name addresses the pass.
    std::string altName;
    uint32_t executionIndex = 0;
    std::vector<std::string> colorOutputs;
    std::vector<std::string> storageOutputs;
    // Empty when the pass writes no depth resource of the graph.
    std::string depthOutput;
    // Whether the pass runs in the current state; capturing after a disabled one fails.
    bool enabled = true;
  };

  struct FrameCaptureTargetList
  {
    std::vector<FrameCaptureTargetInfo> targets;
    std::vector<FrameCaptureAliasInfo> aliases;
    // In execution order.
    std::vector<FrameCapturePassInfo> passes;
  };

  // Parses argv into a spec. Returns false, after logging the reason, on any malformed
  // flag or shot key - the caller is expected to abort before the window opens.
  bool ParseFrameCaptureSpec(int argc, char** argv, FrameCaptureSpec& outSpec);
  // Parses one --shot string. Nothing is logged; on failure outError says why. The name is
  // left empty when the string sets none, and targets default to final,resolved unless the
  // shot names a pass with after=.
  bool ParseFrameCaptureShot(const std::string& text, FrameCaptureShot& outShot, std::string& outError);
}
