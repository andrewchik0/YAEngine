#pragma once

#include "Pch.h"

namespace YAEngine
{
  // How the path tracer meets a Transparent material. Independent of the raster translucency
  // (opacity, fresnelOpacity), which only raster reads; raster ignores this entirely.
  enum class TransmissionMode : uint8_t
  {
    // Absent from every ray, as a transparent material always was
    None,
    // Single-layer geometry, one hit a whole slab crossed without bending: windows, bottles modeled as one sheet
    Sheet,
    // Closed thin walls, one hit one interface crossed without bending: glasses and cups
    ThinWalled,
    // The closed boundary of a refracting medium: liquids, ice, thick glass
    Solid
  };

  inline constexpr float MIN_TRANSMISSION_IOR = 1.0f;
  inline constexpr float MAX_TRANSMISSION_IOR = 3.0f;
  // Keeps -ln(colour) / distance finite.
  inline constexpr float MIN_TRANSMITTANCE_DISTANCE = 0.001f;
  inline constexpr int32_t MAX_MEDIUM_PRIORITY = 15;

  // The scene file spelling.
  inline const char* GetTransmissionModeKey(TransmissionMode mode)
  {
    switch (mode)
    {
      case TransmissionMode::Sheet: return "sheet";
      case TransmissionMode::ThinWalled: return "thinWalled";
      case TransmissionMode::Solid: return "solid";
      default: return "none";
    }
  }

  inline TransmissionMode ParseTransmissionMode(const std::string& key)
  {
    // "thin" is what scenes wrote before Sheet and ThinWalled were told apart, and it meant a sheet
    if (key == "sheet" || key == "thin")
      return TransmissionMode::Sheet;
    if (key == "thinWalled")
      return TransmissionMode::ThinWalled;
    if (key == "solid")
      return TransmissionMode::Solid;
    return TransmissionMode::None;
  }
}
