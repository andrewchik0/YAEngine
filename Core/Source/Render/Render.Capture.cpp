#include "Render.h"

#include "Utils/Log.h"

// The vendored header stays byte-for-byte upstream, and its HDR writer uses sprintf, which
// MSVC deprecates once the CRT headers are already in through the precompiled header.
#pragma warning(push)
#pragma warning(disable : 4996)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <Stb/stb_image_write.h>
#pragma warning(pop)

// FrameCapture. Dumps render graph targets straight out of the images the frame just
// finished writing: raw linear bytes for anything an offline tool has to measure, plus a
// PNG for the 8-bit targets so the picture can simply be opened. The engine never
// interprets a float target - choosing an exposure and a ramp is Tools/FrameAnalysis's job.

namespace YAEngine
{
  namespace
  {
    // What one raw file holds, in the terms the manifest and the Python side speak.
    struct CaptureFormat
    {
      const char* name;
      const char* dtype;
      // Filename suffix, so a directory listing alone says how to decode the file.
      const char* suffix;
      uint32_t channels;
      uint32_t bytesPerPixel;
    };

    // How a pixel of a given format turns into channel values for the statistics.
    enum class SampleKind : uint8_t
    {
      Unorm8,
      Float16,
      Float32,
      Uint32,
      Packed1010102
    };

    const CaptureFormat* FindCaptureFormat(VkFormat format)
    {
      static constexpr CaptureFormat RGBA8      { "R8G8B8A8_UNORM", "uint8", "rgba8", 4, 4 };
      static constexpr CaptureFormat RGBA8SRGB  { "R8G8B8A8_SRGB", "uint8", "rgba8srgb", 4, 4 };
      static constexpr CaptureFormat RG8        { "R8G8_UNORM", "uint8", "rg8", 2, 2 };
      static constexpr CaptureFormat R8         { "R8_UNORM", "uint8", "r8", 1, 1 };
      static constexpr CaptureFormat RGBA16F    { "R16G16B16A16_SFLOAT", "float16", "rgba16f", 4, 8 };
      static constexpr CaptureFormat RG16F      { "R16G16_SFLOAT", "float16", "rg16f", 2, 4 };
      static constexpr CaptureFormat R16F       { "R16_SFLOAT", "float16", "r16f", 1, 2 };
      static constexpr CaptureFormat RGBA32F    { "R32G32B32A32_SFLOAT", "float32", "rgba32f", 4, 16 };
      static constexpr CaptureFormat R32F       { "R32_SFLOAT", "float32", "r32f", 1, 4 };
      static constexpr CaptureFormat D32F       { "D32_SFLOAT", "float32", "d32f", 1, 4 };
      static constexpr CaptureFormat R32U       { "R32_UINT", "uint32", "r32u", 1, 4 };
      // One uint32 per pixel that Python unpacks into four channels; channels below is the
      // decoded count, and bytesPerPixel is what actually sits in the file.
      static constexpr CaptureFormat A2B10G10R10{ "A2B10G10R10_UNORM_PACK32", "uint32", "a2b10g10r10", 4, 4 };

      switch (format)
      {
      case VK_FORMAT_R8G8B8A8_UNORM:           return &RGBA8;
      case VK_FORMAT_R8G8B8A8_SRGB:            return &RGBA8SRGB;
      case VK_FORMAT_R8G8_UNORM:               return &RG8;
      case VK_FORMAT_R8_UNORM:                 return &R8;
      case VK_FORMAT_R16G16B16A16_SFLOAT:      return &RGBA16F;
      case VK_FORMAT_R16G16_SFLOAT:            return &RG16F;
      case VK_FORMAT_R16_SFLOAT:               return &R16F;
      case VK_FORMAT_R32G32B32A32_SFLOAT:      return &RGBA32F;
      case VK_FORMAT_R32_SFLOAT:               return &R32F;
      case VK_FORMAT_D32_SFLOAT:               return &D32F;
      case VK_FORMAT_R32_UINT:                 return &R32U;
      case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return &A2B10G10R10;
      default:                                 return nullptr;
      }
    }

    SampleKind GetSampleKind(VkFormat format)
    {
      switch (format)
      {
      case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return SampleKind::Packed1010102;
      case VK_FORMAT_R32_UINT:                 return SampleKind::Uint32;
      case VK_FORMAT_R8G8B8A8_UNORM:
      case VK_FORMAT_R8G8B8A8_SRGB:
      case VK_FORMAT_R8G8_UNORM:
      case VK_FORMAT_R8_UNORM:                 return SampleKind::Unorm8;
      case VK_FORMAT_R32G32B32A32_SFLOAT:
      case VK_FORMAT_R32_SFLOAT:
      case VK_FORMAT_D32_SFLOAT:               return SampleKind::Float32;
      default:                                 return SampleKind::Float16;
      }
    }

    float HalfToFloat(uint16_t half)
    {
      uint32_t sign = uint32_t(half & 0x8000u) << 16;
      uint32_t exponent = (half >> 10) & 0x1Fu;
      uint32_t mantissa = half & 0x3FFu;
      uint32_t bits;

      if (exponent == 0)
      {
        if (mantissa == 0)
        {
          bits = sign;
        }
        else
        {
          // Subnormal half: normalize by shifting the mantissa up until its leading bit
          // falls out, paying for each shift with one step of the float32 exponent.
          exponent = 1;
          while ((mantissa & 0x400u) == 0)
          {
            mantissa <<= 1;
            exponent--;
          }
          mantissa &= 0x3FFu;
          bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
        }
      }
      else if (exponent == 0x1Fu)
      {
        bits = sign | 0x7F800000u | (mantissa << 13);
      }
      else
      {
        bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
      }

      float value;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    }

    uint32_t DecodeSample(const uint8_t* pixel, SampleKind kind, uint32_t channels, float* out)
    {
      switch (kind)
      {
      case SampleKind::Unorm8:
        for (uint32_t c = 0; c < channels; c++)
          out[c] = float(pixel[c]) / 255.0f;
        return channels;

      case SampleKind::Float16:
      {
        const uint16_t* halves = reinterpret_cast<const uint16_t*>(pixel);
        for (uint32_t c = 0; c < channels; c++)
          out[c] = HalfToFloat(halves[c]);
        return channels;
      }

      case SampleKind::Float32:
      {
        const float* floats = reinterpret_cast<const float*>(pixel);
        for (uint32_t c = 0; c < channels; c++)
          out[c] = floats[c];
        return channels;
      }

      case SampleKind::Uint32:
      {
        uint32_t value;
        std::memcpy(&value, pixel, sizeof(value));
        out[0] = float(value);
        return 1;
      }

      case SampleKind::Packed1010102:
      {
        uint32_t value;
        std::memcpy(&value, pixel, sizeof(value));
        out[0] = float(value & 0x3FFu) / 1023.0f;
        out[1] = float((value >> 10) & 0x3FFu) / 1023.0f;
        out[2] = float((value >> 20) & 0x3FFu) / 1023.0f;
        out[3] = float((value >> 30) & 0x3u) / 3.0f;
        return 4;
      }
      }

      return 0;
    }

    // Whole-image reduction, always written into the manifest. Percentiles deliberately
    // stay out: they need a sort or a histogram and belong in the offline tool.
    struct TargetStats
    {
      uint32_t channels = 0;
      double min[4] { 0.0, 0.0, 0.0, 0.0 };
      double max[4] { 0.0, 0.0, 0.0, 0.0 };
      double sum[4] { 0.0, 0.0, 0.0, 0.0 };
      uint64_t finiteCount[4] { 0, 0, 0, 0 };
      uint64_t nanCount = 0;
      uint64_t infCount = 0;
      uint64_t above1 = 0;
      uint64_t above10 = 0;
      uint64_t above100 = 0;
      uint64_t pixels = 0;
    };

    TargetStats ComputeStats(const uint8_t* data, VkFormat format, const CaptureFormat& info,
                             uint32_t width, uint32_t height)
    {
      TargetStats stats;
      stats.channels = std::min(info.channels, 4u);
      stats.pixels = uint64_t(width) * height;

      SampleKind kind = GetSampleKind(format);
      for (uint32_t c = 0; c < stats.channels; c++)
      {
        stats.min[c] = std::numeric_limits<double>::infinity();
        stats.max[c] = -std::numeric_limits<double>::infinity();
      }

      float channelValues[4] {};
      for (uint64_t i = 0; i < stats.pixels; i++)
      {
        uint32_t decoded = DecodeSample(data + i * info.bytesPerPixel, kind, stats.channels, channelValues);
        float peak = 0.0f;
        for (uint32_t c = 0; c < decoded; c++)
        {
          float value = channelValues[c];
          if (std::isnan(value))
          {
            stats.nanCount++;
            continue;
          }
          if (std::isinf(value))
          {
            stats.infCount++;
            continue;
          }

          stats.min[c] = std::min(stats.min[c], double(value));
          stats.max[c] = std::max(stats.max[c], double(value));
          stats.sum[c] += double(value);
          stats.finiteCount[c]++;
          peak = std::max(peak, value);
        }

        if (peak > 1.0f)   stats.above1++;
        if (peak > 10.0f)  stats.above10++;
        if (peak > 100.0f) stats.above100++;
      }

      for (uint32_t c = 0; c < stats.channels; c++)
      {
        if (stats.finiteCount[c] == 0)
        {
          stats.min[c] = 0.0;
          stats.max[c] = 0.0;
        }
      }

      return stats;
    }

    // Whether a tonemap is meaningful on this target, which is the only thing the offline
    // previewer needs to know about its contents.
    const char* ClassifyColorSpace(const std::string& graphName, VkFormat format)
    {
      static const std::set<std::string> RADIANCE = {
        "litColor", "ssrColor", "taaHistory0", "taaHistory1", "dlssOutput", "rtDebug",
        "pathTraceNoisy", "pathTraceAccum", "ssgiRadiance", "ssgiWorking", "ssgiFinal"
      };

      if (graphName == "sceneColor")
        return "ldr_display";
      if (RADIANCE.contains(graphName))
        return "linear_hdr";

      // Everything else is data even when it is stored as float: depth, velocity, hit
      // distances, ids, packed normals and the ray reconstruction guides.
      (void)format;
      return "data";
    }

    std::string JsonEscape(std::string_view text)
    {
      std::string escaped;
      escaped.reserve(text.size());
      for (char c : text)
      {
        switch (c)
        {
        case '"':  escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
          if (static_cast<unsigned char>(c) < 0x20)
          {
            char unicode[8];
            std::snprintf(unicode, sizeof(unicode), "\\u%04x", c);
            escaped += unicode;
          }
          else
          {
            escaped += c;
          }
        }
      }

      return escaped;
    }

    // JSON has no NaN or Infinity literals, so a non-finite value becomes null rather than
    // a token no parser accepts. %.9g round-trips a float32 exactly.
    std::string JsonNumber(double value)
    {
      if (!std::isfinite(value))
        return "null";

      char text[32];
      std::snprintf(text, sizeof(text), "%.9g", value);
      return text;
    }

    std::string JsonVec3(const glm::vec3& value)
    {
      return "[" + JsonNumber(value.x) + ", " + JsonNumber(value.y) + ", " + JsonNumber(value.z) + "]";
    }

    std::string JsonMatrix(const glm::mat4& matrix)
    {
      std::string text = "[";
      for (int row = 0; row < 4; row++)
      {
        text += row == 0 ? "[" : ", [";
        for (int column = 0; column < 4; column++)
        {
          if (column > 0)
            text += ", ";
          // GLM is column major; emit rows so the manifest reads like the maths does.
          text += JsonNumber(matrix[column][row]);
        }
        text += "]";
      }

      return text + "]";
    }

    std::string JsonChannelArray(const double* values, uint32_t channels)
    {
      std::string text = "[";
      for (uint32_t c = 0; c < channels; c++)
      {
        if (c > 0)
          text += ", ";
        text += JsonNumber(values[c]);
      }

      return text + "]";
    }

    const char* GetTonemapName(int mode)
    {
      switch (mode)
      {
      case TONEMAP_ACES: return "ACES";
      case TONEMAP_AGX:  return "AgX";
      default:           return "Unknown";
      }
    }

    const char* GetPathTraceDebugModeName(int mode)
    {
      switch (mode)
      {
      case PT_DEBUG_OFF:         return "PT_DEBUG_OFF";
      case PT_DEBUG_MAX_CONTRIB: return "PT_DEBUG_MAX_CONTRIB";
      case PT_DEBUG_NEE:         return "PT_DEBUG_NEE";
      case PT_DEBUG_ENVIRONMENT: return "PT_DEBUG_ENVIRONMENT";
      case PT_DEBUG_NONFINITE:   return "PT_DEBUG_NONFINITE";
      default:                   return "Unknown";
      }
    }

    // Deliberately not using TransitionImageLayout: it asserts on layout pairs it has no
    // barrier recipe for, and capture touches images in whatever state the graph left them.
    void CaptureBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                        VkImageLayout oldLayout, VkImageLayout newLayout)
    {
      VkImageMemoryBarrier barrier{};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = oldLayout;
      barrier.newLayout = newLayout;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = image;
      // Only mip 0 is copied, so only mip 0 changes layout and comes straight back.
      barrier.subresourceRange = { aspect, 0, 1, 0, 1 };
      barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

      vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // One target the resolver accepted and the copy is going to service.
    struct CaptureTarget
    {
      std::string alias;
      std::string graphName;
      RGHandle handle = RG_INVALID_HANDLE;
      const CaptureFormat* format = nullptr;
      VkFormat vkFormat = VK_FORMAT_UNDEFINED;
      VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
      VkExtent2D extent {};
      bool outputResolution = false;
      VkDeviceSize size = 0;
      VulkanBuffer staging;
      TargetStats stats;
      std::string rawFile;
      std::string pngFile;
    };

    // Every value the manifest records about the frame, collected inside Render where the
    // private state is reachable, so the writer below needs nothing from the class.
    struct ManifestContext
    {
      const FrameCaptureRequest* request = nullptr;
      const FrameUniforms* uniforms = nullptr;
      const std::vector<std::string>* warnings = nullptr;
      VkExtent2D renderExtent {};
      VkExtent2D outputExtent {};
      uint64_t globalFrameIndex = 0;
      RenderPath selectedPath = RenderPath::Raster;
      RenderPath effectivePath = RenderPath::Raster;
      AntialiasingMode selectedAA = AntialiasingMode::None;
      AntialiasingMode effectiveAA = AntialiasingMode::None;
      bool rayReconstructionAvailable = false;
      bool rayReconstructionActive = false;
      bool pathTraceDevResolve = false;
      int requestedDebugView = 0;
      int tonemapMode = 0;
      float exposure = 0.0f;
      float gamma = 0.0f;
      bool autoExposure = false;
      bool bloomEnabled = false;
      float bloomIntensity = 0.0f;
      float bloomThreshold = 0.0f;
      bool ssr = false;
      bool gtao = false;
      bool ssgi = false;
      bool rayTracingEnabled = false;
      int pathTraceMaxBounces = 0;
      float pathTraceFireflyClamp = 0.0f;
      int pathTraceSampleCount = 0;
      int pathTraceDebugMode = 0;
    };

    void WriteManifest(const ManifestContext& mc, const std::vector<CaptureTarget>& targets)
      {
      const FrameCaptureRequest& request = *mc.request;
      size_t targetCount = targets.size();

      std::string path = request.directory + "/manifest.json";
      std::ofstream out(path);
      if (!out)
      {
        YA_LOG_ERROR("Render", "Capture: failed to open '%s' for writing", path.c_str());
        return;
      }

      const FrameUniforms& uniforms = *mc.uniforms;
      VkExtent2D renderExtent = mc.renderExtent;
      VkExtent2D outputExtent = mc.outputExtent;

      // Reconstructed from the forward vector rather than read off the editor camera, so the
      // numbers describe the camera the frame was actually rendered with.
      const glm::vec3& forward = uniforms.cameraDirection;
      float pitchDegrees = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
      float yawDegrees = glm::degrees(std::atan2(-forward.x, -forward.z));

      int effectiveView = uniforms.currentTexture;

      out << "{\n";
      out << "  \"shot\": { \"index\": " << request.shotIndex
          << ", \"name\": \"" << JsonEscape(request.shotName)
          << "\", \"requestedBy\": \"" << JsonEscape(request.requestedBy) << "\" },\n";
      out << "  \"timing\": { \"globalFrameIndex\": " << mc.globalFrameIndex
          << ", \"warmupFrames\": " << request.warmupFrames
          << ", \"frameInShot\": " << request.frameInShot
          << ", \"framesCaptured\": " << request.frameCount << " },\n";
      out << "  \"resolution\": {\n";
      out << "    \"renderExtent\": [" << renderExtent.width << ", " << renderExtent.height << "],\n";
      out << "    \"outputExtent\": [" << outputExtent.width << ", " << outputExtent.height << "],\n";
      out << "    \"upscaleRatio\": "
          << JsonNumber(double(outputExtent.width) / double(std::max(1u, renderExtent.width))) << "\n";
      out << "  },\n";
      out << "  \"scene\": \"" << JsonEscape(request.scenePath) << "\",\n";
      out << "  \"camera\": {\n";
      out << "    \"position\": " << JsonVec3(uniforms.cameraPosition) << ",\n";
      out << "    \"forward\": " << JsonVec3(forward) << ",\n";
      out << "    \"yawDegrees\": " << JsonNumber(yawDegrees) << ",\n";
      out << "    \"pitchDegrees\": " << JsonNumber(pitchDegrees) << ",\n";
      out << "    \"fovDegrees\": " << JsonNumber(glm::degrees(uniforms.fov)) << ",\n";
      out << "    \"nearPlane\": " << JsonNumber(uniforms.nearPlane) << ",\n";
      out << "    \"farPlane\": " << JsonNumber(uniforms.farPlane) << ",\n";
      out << "    \"reversedZ\": true,\n";
      out << "    \"jitterPixels\": ["
          << JsonNumber(uniforms.jitterX * 0.5 * renderExtent.width) << ", "
          << JsonNumber(uniforms.jitterY * 0.5 * renderExtent.height) << "],\n";
      out << "    \"view\": " << JsonMatrix(uniforms.view) << ",\n";
      out << "    \"projUnjittered\": " << JsonMatrix(uniforms.unjitteredProj) << "\n";
      out << "  },\n";
      out << "  \"settings\": {\n";
      out << "    \"renderPath\": { \"selected\": \"" << GetRenderPathName(mc.selectedPath)
          << "\", \"effective\": \"" << GetRenderPathName(mc.effectivePath) << "\" },\n";
      out << "    \"antialiasing\": { \"selected\": \"" << GetAntialiasingModeName(mc.selectedAA)
          << "\", \"effective\": \"" << GetAntialiasingModeName(mc.effectiveAA) << "\" },\n";
      out << "    \"rayReconstruction\": { \"available\": " << (mc.rayReconstructionAvailable ? "true" : "false")
          << ", \"active\": " << (mc.rayReconstructionActive ? "true" : "false") << " },\n";
      out << "    \"pathTraceDevResolve\": " << (mc.pathTraceDevResolve ? "true" : "false") << ",\n";
      // Requested and effective are separate because Render silently falls the selection back
      // to 0 whenever the pass that feeds the view has not run.
      out << "    \"debugView\": { \"requested\": " << mc.requestedDebugView
          << ", \"effective\": " << effectiveView
          << ", \"name\": \"" << JsonEscape(GetDebugViewName(effectiveView)) << "\" },\n";
      out << "    \"tonemapMode\": { \"value\": " << mc.tonemapMode
          << ", \"name\": \"" << GetTonemapName(mc.tonemapMode) << "\" },\n";
      out << "    \"exposure\": " << JsonNumber(mc.exposure) << ",\n";
      out << "    \"gamma\": " << JsonNumber(mc.gamma) << ",\n";
      out << "    \"autoExposure\": " << (mc.autoExposure ? "true" : "false") << ",\n";
      out << "    \"bloom\": { \"enabled\": " << (mc.bloomEnabled ? "true" : "false")
          << ", \"intensity\": " << JsonNumber(mc.bloomIntensity)
          << ", \"threshold\": " << JsonNumber(mc.bloomThreshold) << " },\n";
      out << "    \"ssr\": " << (mc.ssr ? "true" : "false") << ",\n";
      out << "    \"gtao\": " << (mc.gtao ? "true" : "false") << ",\n";
      out << "    \"ssgi\": " << (mc.ssgi ? "true" : "false") << ",\n";
      out << "    \"rayTracingEnabled\": " << (mc.rayTracingEnabled ? "true" : "false") << ",\n";
      out << "    \"pathTracing\": {\n";
      out << "      \"maxBounces\": " << mc.pathTraceMaxBounces << ",\n";
      out << "      \"fireflyClamp\": " << JsonNumber(mc.pathTraceFireflyClamp) << ",\n";
      out << "      \"sampleCount\": " << mc.pathTraceSampleCount << ",\n";
      out << "      \"requestedSampleCount\": " << request.accumSamples << ",\n";
      out << "      \"debugMode\": { \"value\": " << mc.pathTraceDebugMode
          << ", \"name\": \"" << GetPathTraceDebugModeName(mc.pathTraceDebugMode) << "\" }\n";
      out << "    }\n";
      out << "  },\n";
      out << "  \"targets\": [\n";

      for (size_t i = 0; i < targetCount; i++)
      {
        const auto& target = targets[i];
        const auto& stats = target.stats;
        uint32_t rowPitch = target.extent.width * target.format->bytesPerPixel;

        double mean[4] { 0.0, 0.0, 0.0, 0.0 };
        for (uint32_t c = 0; c < stats.channels; c++)
          mean[c] = stats.finiteCount[c] > 0 ? stats.sum[c] / double(stats.finiteCount[c]) : 0.0;

        double pixels = double(std::max<uint64_t>(1, stats.pixels));

        out << "    {\n";
        out << "      \"name\": \"" << JsonEscape(target.alias) << "\",\n";
        out << "      \"graphName\": \"" << JsonEscape(target.graphName) << "\",\n";
        out << "      \"files\": { \"raw\": \"" << JsonEscape(target.rawFile) << "\"";
        if (!target.pngFile.empty())
          out << ", \"png\": \"" << JsonEscape(target.pngFile) << "\"";
        if (request.frameCount > 1)
          out << ", \"pattern\": \"" << JsonEscape(target.alias + "." + target.format->suffix + ".%03d.bin")
              << "\"";
        out << " },\n";
        out << "      \"extent\": [" << target.extent.width << ", " << target.extent.height << "],\n";
        out << "      \"resolution\": \"" << (target.outputResolution ? "Output" : "Render") << "\",\n";
        out << "      \"mipLevel\": 0,\n";
        out << "      \"aspect\": \""
            << (target.aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? "depth" : "color") << "\",\n";
        out << "      \"format\": { \"vk\": " << int(target.vkFormat)
            << ", \"name\": \"" << target.format->name
            << "\", \"dtype\": \"" << target.format->dtype
            << "\", \"channels\": " << target.format->channels
            << ", \"bytesPerPixel\": " << target.format->bytesPerPixel << " },\n";
        out << "      \"rowPitchBytes\": " << rowPitch << ",\n";
        out << "      \"colorSpace\": \"" << ClassifyColorSpace(target.graphName, target.vkFormat) << "\",\n";
        out << "      \"stats\": {\n";
        out << "        \"channels\": [";
        static const char* CHANNEL_NAMES[] = { "r", "g", "b", "a" };
        for (uint32_t c = 0; c < stats.channels; c++)
          out << (c > 0 ? ", \"" : "\"") << CHANNEL_NAMES[c] << "\"";
        out << "],\n";
        out << "        \"min\": " << JsonChannelArray(stats.min, stats.channels) << ",\n";
        out << "        \"max\": " << JsonChannelArray(stats.max, stats.channels) << ",\n";
        out << "        \"mean\": " << JsonChannelArray(mean, stats.channels) << ",\n";
        out << "        \"nanCount\": " << stats.nanCount << ", \"infCount\": " << stats.infCount << ",\n";
        out << "        \"fracAbove1\": " << JsonNumber(double(stats.above1) / pixels)
            << ", \"fracAbove10\": " << JsonNumber(double(stats.above10) / pixels)
            << ", \"fracAbove100\": " << JsonNumber(double(stats.above100) / pixels) << "\n";
        out << "      }\n";
        out << "    }" << (i + 1 < targetCount ? "," : "") << "\n";
      }

      out << "  ],\n";
      out << "  \"warnings\": [";
      for (size_t i = 0; i < (*mc.warnings).size(); i++)
        out << (i > 0 ? ", \"" : "\"") << JsonEscape((*mc.warnings)[i]) << "\"";
      out << "]\n";
      out << "}\n";
    }
  }

  void Render::RequestCapture(const FrameCaptureRequest& request)
  {
    m_CaptureRequest = request;
    b_CaptureRequested = true;
  }

  bool Render::ConsumeCaptureResult(FrameCaptureResult& outResult)
  {
    if (!b_CaptureResultReady)
      return false;

    b_CaptureResultReady = false;
    outResult = m_CaptureResult;
    return true;
  }

  void Render::LogCaptureTargets()
  {
    YA_LOG_INFO("Render", "Capture: %u graph targets", m_Graph.GetResourceCount());
    for (RGHandle handle = 0; handle < m_Graph.GetResourceCount(); handle++)
    {
      if (!m_Graph.IsResourceManaged(handle))
        continue;

      const auto& desc = m_Graph.GetResourceDesc(handle);
      const CaptureFormat* format = FindCaptureFormat(desc.format);
      VkExtent2D extent = desc.resolution == RGResolution::Output
        ? m_Graph.GetOutputExtent() : m_Graph.GetExtent();

      YA_LOG_INFO("Render", "Capture:   %-18s %-24s %-6s %ux%u mips=%u%s",
        desc.name.c_str(),
        format ? format->name : "UNSUPPORTED",
        desc.resolution == RGResolution::Output ? "Output" : "Render",
        extent.width, extent.height, desc.mipLevels,
        desc.aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? " depth" : "");
    }

    YA_LOG_INFO("Render", "Capture: aliases");
    YA_LOG_INFO("Render", "Capture:   final          -> sceneColor (editor builds only)");
    YA_LOG_INFO("Render", "Capture:   resolved       -> this frame's resolved colour");
    YA_LOG_INFO("Render", "Capture:   prev_resolved  -> the previous frame's resolved colour");
    YA_LOG_INFO("Render", "Capture:   taa            -> the TAA history slot this frame wrote");
    YA_LOG_INFO("Render", "Capture:   pt_noisy       -> pathTraceNoisy");
    YA_LOG_INFO("Render", "Capture:   pt_accum       -> pathTraceAccum");
    YA_LOG_INFO("Render", "Capture:   gbuffer        -> gbuffer0,gbuffer1,mainDepth,mainVelocity");
    YA_LOG_INFO("Render", "Capture:   pt             -> pt_noisy,pt_accum,ptHitDistance,"
      "ptSpecularMotion,ptDiffuseAlbedo,ptSpecularAlbedo,ptNormalRoughness");
    YA_LOG_INFO("Render", "Capture:   default        -> final,resolved");
    YA_LOG_INFO("Render", "Capture:   all            -> every capturable graph resource");

    // The other half of what a shot can name. Listed from the same table --shot parses, so a
    // slug that is missing here is missing everywhere.
    YA_LOG_INFO("Render", "Capture: debug views, as view=<slug> or view=<id>");
    for (int view = 0; view < GetDebugViewCount(); view++)
    {
      YA_LOG_INFO("Render", "Capture:   %-2d %-18s %s",
        view, GetDebugViewSlug(view), GetDebugViewName(view));
    }
  }

  void Render::CaptureFrame()
  {
    b_CaptureRequested = false;

    const FrameCaptureRequest& request = m_CaptureRequest;
    m_CaptureResult = {};

    auto warn = [this](const std::string& message) {
      YA_LOG_WARN("Render", "Capture: %s", message.c_str());
      m_CaptureResult.warnings.push_back(message);
      m_CaptureResult.complete = false;
    };

    // historyWrite for this frame - m_TAAIndex has not been advanced yet at the call site
    RGHandle taaOutput = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;

    auto resolveAlias = [&](const std::string& name) -> RGHandle {
      if (name == "resolved")      return GetResolvedColorHandle();
      if (name == "prev_resolved") return GetPreviousResolvedColorHandle();
      if (name == "taa")           return taaOutput;
      if (name == "pt_noisy")      return m_PathTraceNoisy;
      if (name == "pt_accum")      return m_PathTraceAccum;
#ifdef YA_EDITOR
      if (name == "final")         return m_SceneColor;
#endif
      return m_Graph.FindResource(name);
    };

    std::vector<CaptureTarget> targets;
    auto addTarget = [&](const std::string& alias) {
      RGHandle handle = resolveAlias(alias);
      if (handle == RG_INVALID_HANDLE)
      {
#ifndef YA_EDITOR
        if (alias == "final")
        {
          warn("target 'final' skipped: the tonemapped image only exists in an editor build");
          return true;
        }
#endif
        std::string message = "unknown target '" + alias + "'";
        YA_LOG_ERROR("Render", "Capture: %s", message.c_str());
        m_CaptureResult.warnings.push_back(message);
        m_CaptureResult.failed = true;
        return false;
      }

      const auto& desc = m_Graph.GetResourceDesc(handle);
      const CaptureFormat* format = FindCaptureFormat(desc.format);
      if (format == nullptr)
      {
        warn("target '" + alias + "' skipped: unsupported format " + std::to_string(int(desc.format)));
        return true;
      }

      // After a resize the graph recreates images with UNDEFINED layout. Restoring to
      // UNDEFINED is illegal, and the contents would be garbage anyway.
      VkImageLayout layout = m_Graph.GetResource(handle).GetLayout();
      if (layout == VK_IMAGE_LAYOUT_UNDEFINED)
      {
        warn("target '" + alias + "' skipped: layout UNDEFINED");
        return true;
      }

      bool outputResolution = desc.resolution == RGResolution::Output;
      VkExtent2D extent = outputResolution ? m_Graph.GetOutputExtent() : m_Graph.GetExtent();

      CaptureTarget target;
      target.alias = alias;
      target.graphName = desc.name;
      target.handle = handle;
      target.format = format;
      target.vkFormat = desc.format;
      target.aspect = desc.aspect;
      target.layout = layout;
      target.extent = extent;
      target.outputResolution = outputResolution;
      target.size = VkDeviceSize(extent.width) * extent.height * format->bytesPerPixel;
      targets.push_back(std::move(target));
      return true;
    };

    auto addGroup = [&](const std::string& name) {
      if (name == "gbuffer")
        return addTarget("gbuffer0") && addTarget("gbuffer1")
          && addTarget("mainDepth") && addTarget("mainVelocity");

      if (name == "pt")
        return addTarget("pt_noisy") && addTarget("pt_accum") && addTarget("ptHitDistance")
          && addTarget("ptSpecularMotion") && addTarget("ptDiffuseAlbedo") && addTarget("ptSpecularAlbedo")
          && addTarget("ptNormalRoughness");

      if (name == "default")
        return addTarget("final") && addTarget("resolved");

      if (name == "all")
      {
        for (RGHandle handle = 0; handle < m_Graph.GetResourceCount(); handle++)
        {
          if (!m_Graph.IsResourceManaged(handle))
            continue;

          const auto& desc = m_Graph.GetResourceDesc(handle);
          // 'all' is a sweep, not a request: a resource nothing has written or that this
          // build cannot decode is left out quietly rather than warned about once per name.
          if (FindCaptureFormat(desc.format) == nullptr)
            continue;
          if (m_Graph.GetResource(handle).GetLayout() == VK_IMAGE_LAYOUT_UNDEFINED)
            continue;

          addTarget(desc.name);
        }
        return true;
      }

      return addTarget(name);
    };

    for (const auto& name : request.targets)
    {
      if (!addGroup(name))
        break;
    }

    if (m_CaptureResult.failed || targets.empty())
    {
      if (targets.empty() && !m_CaptureResult.failed)
        warn("no capturable target resolved");
      b_CaptureResultReady = true;
      return;
    }

    // Created only once a target actually resolved, so a failed shot leaves no empty
    // directory for a reader to mistake for a capture that produced nothing.
    std::error_code ec;
    std::filesystem::create_directories(request.directory, ec);
    if (ec)
    {
      YA_LOG_ERROR("Render", "Capture: cannot create '%s': %s",
        request.directory.c_str(), ec.message().c_str());
      m_CaptureResult.failed = true;
      b_CaptureResultReady = true;
      return;
    }

    auto& ctx = m_Backend.GetContext();
    vkDeviceWaitIdle(ctx.device);

    for (auto& target : targets)
      target.staging = VulkanBuffer::CreateReadback(ctx, target.size);

    // One submit for the whole shot: the wait around it dominates the cost, and doing it
    // per target would multiply that by the target count for no benefit.
    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    for (auto& target : targets)
    {
      VkImage image = m_Graph.GetResource(target.handle).GetImage();
      CaptureBarrier(cmd, image, target.aspect, target.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

      VkBufferImageCopy region{};
      region.imageSubresource = { target.aspect, 0, 0, 1 };
      region.imageExtent = { target.extent.width, target.extent.height, 1 };
      vkCmdCopyImageToBuffer(cmd, image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.staging.Get(), 1, &region);

      CaptureBarrier(cmd, image, target.aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.layout);
    }
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    bool multiFrame = request.frameCount > 1;
    char frameSuffix[8] = "";
    if (multiFrame)
      std::snprintf(frameSuffix, sizeof(frameSuffix), ".%03d", request.frameInShot);

    for (auto& target : targets)
    {
      const uint8_t* mapped = static_cast<const uint8_t*>(target.staging.GetMapped());

      target.rawFile = target.alias + "." + target.format->suffix + frameSuffix + ".bin";
      std::string rawPath = request.directory + "/" + target.rawFile;

      std::ofstream out(rawPath, std::ios::binary);
      if (out)
        out.write(reinterpret_cast<const char*>(mapped), std::streamsize(target.size));
      else
        warn("failed to write '" + target.rawFile + "'");
      out.close();

      // PNG only where no interpretive decision is needed: an 8-bit UNORM target already
      // is the image. Float targets are Tools/FrameAnalysis's business.
      if (target.vkFormat == VK_FORMAT_R8G8B8A8_UNORM
        || target.vkFormat == VK_FORMAT_R8G8B8A8_SRGB
        || target.vkFormat == VK_FORMAT_R8_UNORM)
      {
        std::string pngFile = target.alias + std::string(frameSuffix) + ".png";
        std::string pngPath = request.directory + "/" + pngFile;
        int written = stbi_write_png(pngPath.c_str(), int(target.extent.width), int(target.extent.height),
          int(target.format->channels), mapped, int(target.extent.width * target.format->bytesPerPixel));
        if (written != 0)
          target.pngFile = pngFile;
        else
          warn("failed to write '" + pngFile + "'");
      }

      target.stats = ComputeStats(mapped, target.vkFormat, *target.format,
        target.extent.width, target.extent.height);

      target.staging.Destroy(ctx);
    }

    ManifestContext manifestContext {
      .request = &request,
      .uniforms = &m_FrameUniformBuffer.uniforms,
      .warnings = &m_CaptureResult.warnings,
      .renderExtent = m_Graph.GetExtent(),
      .outputExtent = m_Graph.GetOutputExtent(),
      .globalFrameIndex = m_GlobalFrameIndex,
      .selectedPath = m_RenderPath,
      .effectivePath = m_EffectiveRenderPath,
      .selectedAA = m_AntialiasingMode,
      .effectiveAA = m_EffectiveAntialiasingMode,
      .rayReconstructionAvailable = IsRayReconstructionAvailable(),
      .rayReconstructionActive = IsRayReconstructionResolve(),
      .pathTraceDevResolve = b_PathTraceDevResolve,
      .requestedDebugView = m_CurrentTexture,
      .tonemapMode = m_TonemapMode,
      .exposure = m_Exposure,
      .gamma = m_Gamma,
      .autoExposure = b_AutoExposureEnabled,
      .bloomEnabled = b_BloomEnabled,
      .bloomIntensity = m_BloomIntensity,
      .bloomThreshold = m_BloomThreshold,
      .ssr = b_SSREnabled,
      .gtao = b_AOEnabled,
      .ssgi = b_SSGIEnabled,
      .rayTracingEnabled = b_RayTracingEnabled,
      .pathTraceMaxBounces = m_PathTraceMaxBounces,
      .pathTraceFireflyClamp = m_PathTraceFireflyClamp,
      .pathTraceSampleCount = m_PathTraceSampleIndex,
      .pathTraceDebugMode = GetPathTraceDebugMode()
    };
    WriteManifest(manifestContext, targets);

    YA_LOG_INFO("Render", "Capture: shot %03d '%s' frame %d/%d -> '%s' (%zu targets)",
      request.shotIndex, request.shotName.c_str(), request.frameInShot + 1, request.frameCount,
      request.directory.c_str(), targets.size());

    b_CaptureResultReady = true;
  }


  void Render::WriteCaptureSession(const FrameCaptureSessionInfo& info)
  {
    std::string path = info.outputDir + "/session.json";
    std::ofstream out(path);
    if (!out)
    {
      YA_LOG_ERROR("Render", "Capture: failed to open '%s' for writing", path.c_str());
      return;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_Backend.GetPhysicalDevice(), &properties);

    char driver[32];
    std::snprintf(driver, sizeof(driver), "%u.%u.%u",
      VK_API_VERSION_MAJOR(properties.driverVersion),
      VK_API_VERSION_MINOR(properties.driverVersion),
      VK_API_VERSION_PATCH(properties.driverVersion));

    char api[32];
    std::snprintf(api, sizeof(api), "%u.%u.%u",
      VK_API_VERSION_MAJOR(properties.apiVersion),
      VK_API_VERSION_MINOR(properties.apiVersion),
      VK_API_VERSION_PATCH(properties.apiVersion));

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &now);
    char started[32];
    std::strftime(started, sizeof(started), "%Y-%m-%dT%H:%M:%SZ", &utc);

#ifdef NDEBUG
    const char* config = "Release";
#else
    const char* config = "Debug";
#endif
#ifdef YA_EDITOR
    const char* editor = "true";
#else
    const char* editor = "false";
#endif

    out << "{\n";
    out << "  \"tool\": \"FrameCapture\",\n";
    out << "  \"version\": 1,\n";
    out << "  \"startedUtc\": \"" << started << "\",\n";
    out << "  \"commandLine\": \"" << JsonEscape(info.commandLine) << "\",\n";
    out << "  \"build\": { \"config\": \"" << config << "\", \"editor\": " << editor
        << ", \"dlssEnabled\": " << (IsDLSSAvailable() ? "true" : "false") << " },\n";
    out << "  \"gpu\": { \"name\": \"" << JsonEscape(properties.deviceName)
        << "\", \"driver\": \"" << driver << "\", \"vulkanApi\": \"" << api << "\" },\n";
    out << "  \"scene\": \"" << JsonEscape(info.scenePath) << "\",\n";
    out << "  \"shots\": [\n";
    for (size_t i = 0; i < info.shots.size(); i++)
    {
      const auto& shot = info.shots[i];
      out << "    { \"index\": " << shot.index
          << ", \"name\": \"" << JsonEscape(shot.name)
          << "\", \"dir\": \"" << JsonEscape(shot.dir)
          << "\", \"status\": \"" << JsonEscape(shot.status) << "\" }"
          << (i + 1 < info.shots.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"status\": \"" << JsonEscape(info.status) << "\"\n";
    out << "}\n";
  }
}
