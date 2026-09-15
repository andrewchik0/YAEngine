#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Sections of the viewport toolbar's View menu, in menu order
  enum class DebugViewGroup : uint8_t
  {
    Lit,
    Material,
    Geometry,
    Lighting,
    ScreenSpace,
    ProbesAndVolumes,
    PathTracing,
    Count
  };

  struct DebugViewInfo
  {
    // DEBUG_VIEW_* id: what Render::SetDebugView takes and FrameUniforms::currentTexture carries
    int32_t id = 0;
    // Survives renumbering; --shot view=, capture manifests and the agent bridge name views by it
    const char* slug = nullptr;
    const char* name = nullptr;
    DebugViewGroup group = DebugViewGroup::Lit;
  };

  // The one table of debug views, in View menu order: grouped, and within a group in the order the
  // menu lists them. Ids cover 0 to GetDebugViewCount() - 1 but do not follow this order.
  std::span<const DebugViewInfo> GetDebugViews();
  // nullptr for an id no view has
  const DebugViewInfo* FindDebugView(int32_t id);
  const char* GetDebugViewGroupName(DebugViewGroup group);

  // "Unknown" and "unknown" for an id no view has
  const char* GetDebugViewName(int32_t id);
  const char* GetDebugViewSlug(int32_t id);
  int32_t GetDebugViewCount();
  // Accepts a decimal id or a slug ("pt-max-contrib"). Returns -1 for anything else.
  int32_t ParseDebugView(std::string_view text);
}
