#include "Utils/DebugViews.h"

#include "FrameUniforms.h"

namespace YAEngine
{
  namespace
  {
    using enum DebugViewGroup;

    // Albedo to Normals, Wireframe and Velocity have no DEBUG_VIEW_* name: no shared macro and no
    // shader switch spells them out.
    constexpr DebugViewInfo DEBUG_VIEWS[] = {
      { 0,                             "off",                "Off",                       Lit },
      { 1,                             "albedo",             "Albedo",                    Material },
      { 2,                             "metallic",           "Metallic",                  Material },
      { 3,                             "roughness",          "Roughness",                 Material },
      { 4,                             "normals",            "Normals",                   Material },
      { 7,                             "wireframe",          "Wireframe",                 Geometry },
      { 9,                             "velocity",           "Velocity",                  Geometry },
      { DEBUG_VIEW_DIRECT_ONLY,        "direct-only",        "Direct Only",               Lighting },
      { DEBUG_VIEW_AMBIENT_ONLY,       "ambient-only",       "Ambient Only",              Lighting },
      { DEBUG_VIEW_AMBIENT_DIFFUSE,    "ambient-diffuse",    "Ambient Diffuse",           Lighting },
      { DEBUG_VIEW_AMBIENT_SPECULAR,   "ambient-specular",   "Ambient Specular",          Lighting },
      { DEBUG_VIEW_HDR_MAGNITUDE,      "hdr-magnitude",      "HDR Magnitude",             Lighting },
      { DEBUG_VIEW_AO,                 "ao",                 "AO",                        ScreenSpace },
      { DEBUG_VIEW_SSR,                "ssr",                "SSR",                       ScreenSpace },
      { DEBUG_VIEW_SSGI_VALIDITY,      "ssgi-validity",      "SSGI Validity",             ScreenSpace },
      { DEBUG_VIEW_SSGI_SCREEN,        "ssgi-screen",        "SSGI Screen Part",          ScreenSpace },
      { DEBUG_VIEW_SSGI_FALLBACK,      "ssgi-fallback",      "SSGI Fallback Weight",      ScreenSpace },
      { DEBUG_VIEW_TAA_DELTA,          "taa-delta",          "TAA Delta",                 ScreenSpace },
      { DEBUG_VIEW_PROBE_INDEX,        "probe-index",        "Reflection Probe Index",    ProbesAndVolumes },
      { DEBUG_VIEW_PROBE_FALLBACK,     "probe-fallback",     "Reflection Probe Fallback", ProbesAndVolumes },
      { DEBUG_VIEW_VOLUME_COVERAGE,    "volume-coverage",    "Volume Coverage",           ProbesAndVolumes },
      { DEBUG_VIEW_VOLUME_LEVEL,       "volume-level",       "Volume Level",              ProbesAndVolumes },
      { DEBUG_VIEW_PT_NOISY,           "pt-noisy",           "PT Noisy",                  PathTracing },
      { DEBUG_VIEW_PT_REFERENCE,       "pt-reference",       "PT Reference",              PathTracing },
      { DEBUG_VIEW_PT_GUIDES,          "pt-guides",          "PT Guides",                 PathTracing },
      { DEBUG_VIEW_PT_MAX_CONTRIB,     "pt-max-contrib",     "PT Max Contribution",       PathTracing },
      { DEBUG_VIEW_PT_NEE,             "pt-nee",             "PT NEE",                    PathTracing },
      { DEBUG_VIEW_PT_ENVIRONMENT,     "pt-environment",     "PT Environment",            PathTracing },
      { DEBUG_VIEW_PT_NONFINITE,       "pt-nonfinite",       "PT Non-Finite",             PathTracing },
      { DEBUG_VIEW_PT_SPECULAR_MOTION, "pt-specular-motion", "PT Specular Motion",        PathTracing },
    };

    constexpr size_t VIEW_COUNT = std::size(DEBUG_VIEWS);

    static_assert(VIEW_COUNT == DEBUG_VIEW_VOLUME_LEVEL + 1,
      "Debug view table is out of sync with the DEBUG_VIEW_* ids in FrameUniforms.h");

    constexpr bool IdsCoverTableOnce()
    {
      std::array<bool, VIEW_COUNT> seen {};
      for (const DebugViewInfo& view : DEBUG_VIEWS)
      {
        if (view.id < 0 || size_t(view.id) >= VIEW_COUNT || seen[size_t(view.id)])
          return false;
        seen[size_t(view.id)] = true;
      }
      return true;
    }

    // The View menu draws one heading per run of rows sharing a group
    constexpr bool GroupsInMenuOrder()
    {
      for (size_t row = 1; row < VIEW_COUNT; row++)
      {
        if (DEBUG_VIEWS[row].group < DEBUG_VIEWS[row - 1].group)
          return false;
      }
      return true;
    }

    static_assert(IdsCoverTableOnce(), "Every debug view id from 0 up must name exactly one row");
    static_assert(GroupsInMenuOrder(), "Debug view rows must be sorted by group in menu order");

    constexpr std::array<uint8_t, VIEW_COUNT> BuildRowsById()
    {
      std::array<uint8_t, VIEW_COUNT> rows {};
      for (size_t row = 0; row < VIEW_COUNT; row++)
        rows[size_t(DEBUG_VIEWS[row].id)] = uint8_t(row);
      return rows;
    }

    constexpr std::array<uint8_t, VIEW_COUNT> ROWS_BY_ID = BuildRowsById();

    constexpr const char* GROUP_NAMES[] = {
      "Lit", "Material", "Geometry", "Lighting", "Screen Space", "Probes & Volumes", "Path Tracing"
    };

    static_assert(std::size(GROUP_NAMES) == size_t(DebugViewGroup::Count));
  }

  std::span<const DebugViewInfo> GetDebugViews()
  {
    return DEBUG_VIEWS;
  }

  const DebugViewInfo* FindDebugView(int32_t id)
  {
    if (id < 0 || size_t(id) >= VIEW_COUNT)
      return nullptr;

    return &DEBUG_VIEWS[ROWS_BY_ID[size_t(id)]];
  }

  const char* GetDebugViewGroupName(DebugViewGroup group)
  {
    return size_t(group) < std::size(GROUP_NAMES) ? GROUP_NAMES[size_t(group)] : "Unknown";
  }

  const char* GetDebugViewName(int32_t id)
  {
    const DebugViewInfo* view = FindDebugView(id);
    return view != nullptr ? view->name : "Unknown";
  }

  const char* GetDebugViewSlug(int32_t id)
  {
    const DebugViewInfo* view = FindDebugView(id);
    return view != nullptr ? view->slug : "unknown";
  }

  int32_t GetDebugViewCount()
  {
    return int32_t(VIEW_COUNT);
  }

  int32_t ParseDebugView(std::string_view text)
  {
    constexpr size_t MAX_ID_DIGITS = 9;
    const bool numeric = !text.empty() && text.size() <= MAX_ID_DIGITS
      && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (numeric)
    {
      int32_t id = 0;
      for (char c : text)
        id = id * 10 + (c - '0');
      return FindDebugView(id) != nullptr ? id : -1;
    }

    for (const DebugViewInfo& view : DEBUG_VIEWS)
    {
      if (text == view.slug)
        return view.id;
    }

    return -1;
  }
}
