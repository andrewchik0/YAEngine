#pragma once

#include "Pch.h"

namespace YAEngine
{
  enum class AntialiasingMode : uint32_t
  {
    None,
    TAA,
    DLAA,
    DLSSQuality,
    DLSSBalanced,
    DLSSPerformance,
    DLSSUltraPerformance,
    Count
  };

  // Shape of the camera jitter sequence a mode needs. Kept as data so the DLSS resolve
  // can ask for the full +-0.5 px sweep and its own phase count without SetUpCamera
  // growing a second code path.
  struct JitterParameters
  {
    // Half-width of the sweep in pixels.
    float amplitude = 0.25f;
    uint32_t phaseCount = 1024;
  };

  inline bool IsDLSSMode(AntialiasingMode mode)
  {
    return mode == AntialiasingMode::DLAA
      || mode == AntialiasingMode::DLSSQuality
      || mode == AntialiasingMode::DLSSBalanced
      || mode == AntialiasingMode::DLSSPerformance
      || mode == AntialiasingMode::DLSSUltraPerformance;
  }

  // Modes that accumulate over frames, and therefore need camera jitter, motion
  // vectors and per-frame rotating noise.
  inline bool IsTemporalAA(AntialiasingMode mode)
  {
    return mode == AntialiasingMode::TAA || IsDLSSMode(mode);
  }

  // Modes resolved by the engine's own TAA pass. DLSS modes skip it: slEvaluateFeature
  // resolves and upscales in one step and owns its own history.
  inline bool UsesTAAPass(AntialiasingMode mode)
  {
    return mode == AntialiasingMode::TAA;
  }

  // Ray reconstruction asks for more jitter positions than super resolution does, and states
  // a floor rather than a formula: section 3.6 of the DLSS-RR Integration Guide says there is
  // "no reason to limit the number of samples" and that "many more jitter positions (at
  // least 32)" are highly recommended. The super resolution sizing below would hand DLAA
  // eight, since the path tracer always resolves at a scaling ratio of one.
  inline constexpr uint32_t RAY_RECONSTRUCTION_MIN_JITTER_PHASES = 32;

  // upscaleRatio is output extent / render extent along one axis, 1 for DLAA.
  // rayReconstruction raises the phase floor - the sweep itself is unchanged.
  inline JitterParameters GetJitterParameters(AntialiasingMode mode, float upscaleRatio = 1.0f,
    bool rayReconstruction = false)
  {
    // The engine TAA pass was tuned against a halved sweep over 1024 Halton phases.
    if (!IsDLSSMode(mode))
      return {};

    // DLSS is trained on the full +-0.5 px sweep, and NVIDIA sizes the sequence as
    // 8 base phases scaled by the area ratio between output and render resolution.
    float phases = std::ceil(8.0f * upscaleRatio * upscaleRatio);
    if (rayReconstruction)
      phases = std::max(phases, float(RAY_RECONSTRUCTION_MIN_JITTER_PHASES));

    return {
      .amplitude = 0.5f,
      .phaseCount = static_cast<uint32_t>(std::max(1.0f, phases))
    };
  }

  inline const char* GetAntialiasingModeName(AntialiasingMode mode)
  {
    switch (mode)
    {
      case AntialiasingMode::None: return "None";
      case AntialiasingMode::TAA: return "TAA";
      case AntialiasingMode::DLAA: return "DLAA";
      case AntialiasingMode::DLSSQuality: return "DLSS Quality";
      case AntialiasingMode::DLSSBalanced: return "DLSS Balanced";
      case AntialiasingMode::DLSSPerformance: return "DLSS Performance";
      case AntialiasingMode::DLSSUltraPerformance: return "DLSS Ultra Performance";
      case AntialiasingMode::Count: break;
    }

    return "Unknown";
  }
}
