#pragma once

namespace YAEngine
{
  // The single enumeration of every reason a cached shadow tile's content can
  // become invalid. Stage 2 (cascade fit hysteresis) raises only CascadeRefit,
  // SunMoved, ShadowParamsChanged and DebugToggleChanged; the remaining values
  // are declared now so later stages of the caching project extend behavior
  // without ever growing a second reason list somewhere else.
  enum class ShadowInvalidation : uint8_t
  {
    None = 0,             // nothing recorded yet, never raised as a reason
    CascadeRefit,         // required frustum-slice sphere escaped the frozen cascade sphere
    SunMoved,             // sun direction left the angular hysteresis threshold
    ShadowParamsChanged,  // shadowDistance / camera fov / aspect / nearPlane changed
    ShadowsToggled,       // shadows switched on or off as a whole
    CasterMoved,          // a caster's transform changed inside a fitted tile
    CasterAddedOrRemoved, // a caster entered or left the tile's frustum
    CasterMeshChanged,    // a caster's mesh or its selected LOD stream changed
    CasterMaterialChanged,// a caster's material changed in a way shadows see (alpha test)
    DynamicSettled,       // a dynamic caster stopped moving and its tile became cacheable
    LightMoved,           // a spot/point light's position or direction changed
    LightParamsChanged,   // a light's cone / radius / bias parameters changed
    ProbeBake,            // a probe or irradiance volume bake rewrote the atlas
    Resize,               // atlas or tile layout changed
    GeometryStreamedIn,   // streamed-in geometry appeared inside an already fitted tile
    DebugToggleChanged,   // a debug A/B toggle (hysteresis, indirect, clear mode) flipped
    ShaderReloaded,       // editor hot reload swapped the pipelines that wrote the atlas
  };

  constexpr const char* ShadowInvalidationName(ShadowInvalidation reason)
  {
    switch (reason)
    {
      case ShadowInvalidation::None:                  return "none";
      case ShadowInvalidation::CascadeRefit:          return "refit";
      case ShadowInvalidation::SunMoved:              return "sun";
      case ShadowInvalidation::ShadowParamsChanged:   return "params";
      case ShadowInvalidation::ShadowsToggled:        return "shadows";
      case ShadowInvalidation::CasterMoved:           return "caster-moved";
      case ShadowInvalidation::CasterAddedOrRemoved:  return "caster-set";
      case ShadowInvalidation::CasterMeshChanged:     return "caster-mesh";
      case ShadowInvalidation::CasterMaterialChanged: return "caster-mat";
      case ShadowInvalidation::DynamicSettled:        return "settled";
      case ShadowInvalidation::LightMoved:            return "light-moved";
      case ShadowInvalidation::LightParamsChanged:    return "light-params";
      case ShadowInvalidation::ProbeBake:             return "probe-bake";
      case ShadowInvalidation::Resize:                return "resize";
      case ShadowInvalidation::GeometryStreamedIn:    return "streamed";
      case ShadowInvalidation::DebugToggleChanged:    return "debug-toggle";
      case ShadowInvalidation::ShaderReloaded:        return "shader-reload";
    }
    return "unknown";
  }
}
