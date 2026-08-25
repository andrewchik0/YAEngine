#pragma once

namespace YAEngine
{
  // The single enumeration of every reason a cached shadow tile's content can
  // become invalid.
  enum class ShadowInvalidation : uint8_t
  {
    None = 0,             // nothing recorded yet, never raised as a reason
    CascadeRefit,         // required frustum-slice sphere escaped the frozen cascade sphere
    SunMoved,             // sun direction left the angular hysteresis threshold
    ShadowParamsChanged,  // shadowDistance / camera fov / aspect / nearPlane changed
    ShadowsToggled,       // shadows switched on or off as a whole
    CasterMoved,          // a caster's transform changed inside a fitted tile
    CasterAddedOrRemoved, // a caster entered or left the tile's frustum
    LightParamsChanged,   // a light's cone / radius / bias parameters changed
    ProbeBake,            // a probe or irradiance volume bake rewrote the atlas
    Resize,               // atlas or tile layout changed
    GeometryStreamedIn,   // streamed-in geometry appeared inside an already fitted tile
    SettingsChanged,      // a shadow render setting changed (shadows on/off, cascade LOD)
    ShaderReloaded,       // editor hot reload swapped the pipelines that wrote the atlas
  };
}
