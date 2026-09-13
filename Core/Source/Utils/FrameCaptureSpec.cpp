#include "Utils/FrameCaptureSpec.h"

#include "Utils/Log.h"

#include "FrameUniforms.h"
#include "PathTraceData.h"

namespace YAEngine
{
  namespace
  {
    struct DebugViewEntry
    {
      const char* slug;
      const char* name;
    };

    // Index is the DEBUG_VIEW_* id. Order and length must match the debugViews[] labels in
    // RenderSettingsPanel.cpp, which is the list the ids are positional against.
    constexpr DebugViewEntry DEBUG_VIEWS[] = {
      { "off",                  "Off" },
      { "albedo",               "Albedo" },
      { "metallic",             "Metallic" },
      { "roughness",            "Roughness" },
      { "normals",              "Normals" },
      { "ao",                   "AO" },
      { "ssr",                  "SSR" },
      { "wireframe",            "Wireframe" },
      { "taa-delta",            "TAA Delta" },
      { "velocity",             "Velocity" },
      { "ambient-only",         "Ambient Only" },
      { "ambient-diffuse",      "Ambient Diffuse" },
      { "ambient-specular",     "Ambient Specular" },
      { "probe-index",          "Reflection Probe Index" },
      { "probe-fallback",       "Reflection Probe Fallback" },
      { "volume-coverage",      "Volume Coverage" },
      { "ssgi-validity",        "SSGI Validity" },
      { "ssgi-screen",          "SSGI Screen Part" },
      { "ssgi-fallback",        "SSGI Fallback Weight" },
      { "direct-only",          "Direct Only" },
      { "ray-query",            "Ray Query" },
      { "rt-pipeline",          "RT Pipeline" },
      { "pt-noisy",             "PT Noisy" },
      { "pt-reference",         "PT Reference" },
      { "pt-guides",            "PT Guides" },
      { "pt-max-contrib",       "PT Max Contribution" },
      { "pt-nee",               "PT NEE" },
      { "pt-environment",       "PT Environment" },
      { "pt-nonfinite",         "PT Non-Finite" },
      { "hdr-magnitude",        "HDR Magnitude" },
      { "pt-specular-motion",   "PT Specular Motion" },
    };

    static_assert(std::size(DEBUG_VIEWS) == DEBUG_VIEW_PT_SPECULAR_MOTION + 1,
      "Debug view table is out of sync with the DEBUG_VIEW_* ids in FrameUniforms.h");

    struct NamedMode
    {
      const char* key;
      AntialiasingMode mode;
    };

    constexpr NamedMode AA_MODES[] = {
      { "none",            AntialiasingMode::None },
      { "taa",             AntialiasingMode::TAA },
      { "dlaa",            AntialiasingMode::DLAA },
      { "dlss-quality",    AntialiasingMode::DLSSQuality },
      { "dlss-balanced",   AntialiasingMode::DLSSBalanced },
      { "dlss-perf",       AntialiasingMode::DLSSPerformance },
      { "dlss-ultraperf",  AntialiasingMode::DLSSUltraPerformance },
    };

    std::vector<std::string> Split(std::string_view text, char separator)
    {
      std::vector<std::string> parts;
      size_t start = 0;
      while (start <= text.size())
      {
        size_t end = text.find(separator, start);
        if (end == std::string_view::npos)
          end = text.size();

        std::string_view part = text.substr(start, end - start);
        // Trim, so "a, b" and "a=1; b=2" behave the way anyone would type them.
        auto isBlank = [](char c) { return c == ' ' || c == '\t'; };
        while (!part.empty() && isBlank(part.front()))
          part.remove_prefix(1);
        while (!part.empty() && isBlank(part.back()))
          part.remove_suffix(1);

        if (!part.empty())
          parts.emplace_back(part);

        start = end + 1;
      }

      return parts;
    }

    bool ParseInt(std::string_view text, int& out)
    {
      try
      {
        size_t consumed = 0;
        std::string owned(text);
        out = std::stoi(owned, &consumed);
        return consumed == owned.size();
      }
      catch (const std::exception&)
      {
        return false;
      }
    }

    bool ParseFloat(std::string_view text, float& out)
    {
      try
      {
        size_t consumed = 0;
        std::string owned(text);
        out = std::stof(owned, &consumed);
        return consumed == owned.size();
      }
      catch (const std::exception&)
      {
        return false;
      }
    }

    bool ParseBool(std::string_view text, bool& out)
    {
      if (text == "1" || text == "true" || text == "on")  { out = true;  return true; }
      if (text == "0" || text == "false" || text == "off") { out = false; return true; }
      return false;
    }

    bool ParseVec3(std::string_view text, glm::vec3& out)
    {
      auto parts = Split(text, ',');
      if (parts.size() != 3)
        return false;

      for (int i = 0; i < 3; i++)
      {
        if (!ParseFloat(parts[i], out[i]))
          return false;
      }

      return true;
    }

    bool ParseShotKey(const std::string& key, const std::string& value, FrameCaptureShot& shot)
    {
      if (key == "name")
      {
        shot.name = value;
        return true;
      }
      if (key == "targets")
      {
        shot.targets = Split(value, ',');
        if (shot.targets.empty())
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'targets' needs at least one name");
          return false;
        }
        return true;
      }
      if (key == "path")
      {
        if (value == "raster")   { shot.renderPath = RenderPath::Raster; return true; }
        if (value == "pt")       { shot.renderPath = RenderPath::PathTracing; return true; }
        YA_LOG_ERROR("Render", "Capture: shot key 'path' expects raster or pt, got '%s'", value.c_str());
        return false;
      }
      if (key == "aa")
      {
        for (const auto& entry : AA_MODES)
        {
          if (value == entry.key)
          {
            shot.antialiasing = entry.mode;
            return true;
          }
        }
        YA_LOG_ERROR("Render", "Capture: shot key 'aa' expects one of "
          "none|taa|dlaa|dlss-quality|dlss-balanced|dlss-perf|dlss-ultraperf, got '%s'", value.c_str());
        return false;
      }
      if (key == "view")
      {
        int view = ParseDebugView(value);
        if (view < 0)
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'view' does not name a debug view: '%s'", value.c_str());
          return false;
        }
        shot.debugView = view;
        return true;
      }
      if (key == "bounces")
      {
        int bounces = 0;
        if (!ParseInt(value, bounces) || bounces < PT_MIN_BOUNCES || bounces > PT_MAX_BOUNCES)
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'bounces' expects %d..%d, got '%s'",
            PT_MIN_BOUNCES, PT_MAX_BOUNCES, value.c_str());
          return false;
        }
        shot.pathTraceBounces = bounces;
        return true;
      }
      if (key == "clamp")
      {
        float clamp = 0.0f;
        if (!ParseFloat(value, clamp) || clamp < 0.0f)
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'clamp' expects a non-negative float, got '%s'", value.c_str());
          return false;
        }
        shot.pathTraceClamp = clamp;
        return true;
      }
      if (key == "devresolve")
      {
        bool enabled = false;
        if (!ParseBool(value, enabled))
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'devresolve' expects 0 or 1, got '%s'", value.c_str());
          return false;
        }
        shot.pathTraceDevResolve = enabled;
        return true;
      }
      if (key == "accum")
      {
        int samples = 0;
        if (!ParseInt(value, samples) || samples <= 0)
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'accum' expects a positive integer, got '%s'", value.c_str());
          return false;
        }
        shot.accumSamples = samples;
        return true;
      }
      if (key == "exposure")
      {
        float exposure = 0.0f;
        if (!ParseFloat(value, exposure))
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'exposure' expects a float, got '%s'", value.c_str());
          return false;
        }
        shot.exposure = exposure;
        return true;
      }
      if (key == "autoexposure")
      {
        bool enabled = false;
        if (!ParseBool(value, enabled))
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'autoexposure' expects 0 or 1, got '%s'", value.c_str());
          return false;
        }
        shot.autoExposure = enabled;
        return true;
      }
      if (key == "tonemap")
      {
        if (value == "aces") { shot.tonemapMode = TONEMAP_ACES; return true; }
        if (value == "agx")  { shot.tonemapMode = TONEMAP_AGX;  return true; }
        YA_LOG_ERROR("Render", "Capture: shot key 'tonemap' expects aces or agx, got '%s'", value.c_str());
        return false;
      }
      if (key == "bloom")
      {
        bool enabled = false;
        if (!ParseBool(value, enabled))
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'bloom' expects 0 or 1, got '%s'", value.c_str());
          return false;
        }
        shot.bloom = enabled;
        return true;
      }
      if (key == "warmup")
      {
        int warmup = 0;
        if (!ParseInt(value, warmup) || warmup < 0)
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'warmup' expects a non-negative integer, got '%s'", value.c_str());
          return false;
        }
        shot.warmupFrames = warmup;
        return true;
      }
      if (key == "frames")
      {
        int frames = 0;
        if (!ParseInt(value, frames) || frames <= 0)
        {
          YA_LOG_ERROR("Render", "Capture: shot key 'frames' expects a positive integer, got '%s'", value.c_str());
          return false;
        }
        shot.frames = frames;
        return true;
      }
      if (key == "camera" || key == "look")
      {
        glm::vec3 position(0.0f);
        if (!ParseVec3(value, position))
        {
          YA_LOG_ERROR("Render", "Capture: shot key '%s' expects x,y,z, got '%s'", key.c_str(), value.c_str());
          return false;
        }
        if (key == "camera")
          shot.cameraPosition = position;
        else
          shot.lookAt = position;
        return true;
      }
      if (key == "yaw" || key == "pitch")
      {
        float degrees = 0.0f;
        if (!ParseFloat(value, degrees))
        {
          YA_LOG_ERROR("Render", "Capture: shot key '%s' expects degrees, got '%s'", key.c_str(), value.c_str());
          return false;
        }
        if (key == "yaw")
          shot.yawDegrees = degrees;
        else
          shot.pitchDegrees = degrees;
        return true;
      }

      YA_LOG_ERROR("Render", "Capture: unknown shot key '%s'", key.c_str());
      return false;
    }

    bool ParseShot(const std::string& text, FrameCaptureShot& shot)
    {
      shot.requestedBy = text;

      for (const auto& pair : Split(text, ';'))
      {
        size_t equals = pair.find('=');
        if (equals == std::string::npos)
        {
          YA_LOG_ERROR("Render", "Capture: shot entry '%s' is not key=value", pair.c_str());
          return false;
        }

        if (!ParseShotKey(pair.substr(0, equals), pair.substr(equals + 1), shot))
          return false;
      }

      return true;
    }

    bool ParseResolution(std::string_view text, uint32_t& outWidth, uint32_t& outHeight)
    {
      size_t x = text.find('x');
      if (x == std::string_view::npos)
        return false;

      int width = 0;
      int height = 0;
      if (!ParseInt(text.substr(0, x), width) || !ParseInt(text.substr(x + 1), height))
        return false;
      if (width <= 0 || height <= 0)
        return false;

      outWidth = static_cast<uint32_t>(width);
      outHeight = static_cast<uint32_t>(height);
      return true;
    }

    // Retired trigger, kept because it may still be set in a shell profile. Only consulted
    // when argv armed nothing, so --capture always wins.
#pragma warning(push)
#pragma warning(disable : 4996) // getenv, which has no portable _s replacement worth the churn
    void ApplyEnvironmentFallback(FrameCaptureSpec& spec)
    {
      const char* dir = std::getenv("YA_CAPTURE_DIR");
      if (dir == nullptr || *dir == '\0')
        return;

      YA_LOG_WARN("Render", "Capture: armed by YA_CAPTURE_DIR, which is superseded by --capture <dir>");

      spec.armed = true;
      spec.outputDir = dir;
      spec.commandLine = "YA_CAPTURE_DIR=" + spec.outputDir;

      FrameCaptureShot shot;
      shot.requestedBy = "YA_CAPTURE_DIR";

      auto readCount = [](const char* name, int& target) {
        const char* value = std::getenv(name);
        if (value == nullptr)
          return;

        int parsed = 0;
        if (ParseInt(value, parsed) && parsed > 0)
          target = parsed;
        else
          YA_LOG_WARN("Render", "Capture: ignoring %s='%s', expected a positive integer", name, value);
      };

      readCount("YA_CAPTURE_WARMUP", shot.warmupFrames);
      readCount("YA_CAPTURE_FRAMES", shot.frames);

      spec.shots.push_back(shot);
    }
#pragma warning(pop)
  }

  const char* GetDebugViewName(int view)
  {
    if (view < 0 || view >= static_cast<int>(std::size(DEBUG_VIEWS)))
      return "Unknown";

    return DEBUG_VIEWS[view].name;
  }

  const char* GetDebugViewSlug(int view)
  {
    if (view < 0 || view >= static_cast<int>(std::size(DEBUG_VIEWS)))
      return "unknown";

    return DEBUG_VIEWS[view].slug;
  }

  int GetDebugViewCount()
  {
    return static_cast<int>(std::size(DEBUG_VIEWS));
  }

  int ParseDebugView(std::string_view text)
  {
    int id = 0;
    if (ParseInt(text, id))
      return (id >= 0 && id < GetDebugViewCount()) ? id : -1;

    for (int i = 0; i < GetDebugViewCount(); i++)
    {
      if (text == DEBUG_VIEWS[i].slug)
        return i;
    }

    return -1;
  }

  bool ParseFrameCaptureSpec(int argc, char** argv, FrameCaptureSpec& outSpec)
  {
    std::string commandLine;
    for (int i = 0; i < argc; i++)
    {
      if (i > 0)
        commandLine += ' ';
      commandLine += argv[i];
    }

    auto needsValue = [argc, argv](int i, const char* flag) {
      if (i + 1 < argc)
        return true;
      YA_LOG_ERROR("Render", "Capture: %s needs a value", flag);
      return false;
    };

    for (int i = 1; i < argc; i++)
    {
      std::string_view arg(argv[i]);

      if (arg == "--capture")
      {
        if (!needsValue(i, "--capture"))
          return false;
        outSpec.armed = true;
        outSpec.outputDir = argv[++i];
      }
      else if (arg == "--shot")
      {
        if (!needsValue(i, "--shot"))
          return false;

        FrameCaptureShot shot;
        if (!ParseShot(argv[++i], shot))
          return false;
        outSpec.shots.push_back(std::move(shot));
      }
      else if (arg == "--capture-exit")
      {
        outSpec.exitWhenDone = true;
      }
      else if (arg == "--capture-list-targets")
      {
        outSpec.listTargets = true;
      }
      else if (arg == "--capture-res")
      {
        if (!needsValue(i, "--capture-res"))
          return false;
        if (!ParseResolution(argv[++i], outSpec.pinnedWidth, outSpec.pinnedHeight))
        {
          YA_LOG_ERROR("Render", "Capture: --capture-res expects <W>x<H>, got '%s'", argv[i]);
          return false;
        }
      }
      else if (arg.starts_with("--capture") || arg.starts_with("--shot"))
      {
        YA_LOG_ERROR("Render", "Capture: unknown flag '%s'", argv[i]);
        return false;
      }
    }

    if (!outSpec.shots.empty() && !outSpec.armed)
    {
      YA_LOG_ERROR("Render", "Capture: --shot needs --capture <dir>");
      return false;
    }

    if (!outSpec.armed)
      ApplyEnvironmentFallback(outSpec);
    else
      outSpec.commandLine = commandLine;

    if (!outSpec.armed)
      return true;

    // With --capture but no --shot, one shot of the current state is still the useful
    // default: the flag alone has to produce a capture.
    if (outSpec.shots.empty())
      outSpec.shots.emplace_back();

    for (size_t i = 0; i < outSpec.shots.size(); i++)
    {
      auto& shot = outSpec.shots[i];
      if (shot.name.empty())
      {
        char generated[16];
        std::snprintf(generated, sizeof(generated), "shot%03zu", i);
        shot.name = generated;
      }
      if (shot.targets.empty())
        shot.targets = { "final", "resolved" };
    }

    return true;
  }
}
