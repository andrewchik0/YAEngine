#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Spherical harmonics, band 0 + band 1 (four coefficients per color channel).
  //
  // STORAGE CONVENTION - the GLSL side must match this exactly.
  // Finalized coefficients are already convolved with the cosine lobe
  // (A0 = pi, A1 = 2*pi/3) and divided by pi, so evaluating them is one dot
  // product and the result is the SAME quantity the irradiance cubemap holds:
  //
  //   irradiance(n) = l0 + l1x * n.x + l1y * n.y + l1z * n.z
  //
  // Core/Shaders/irradiance.frag outputs E / pi for the same reason, and the
  // lighting path multiplies it straight by albedo. A constant environment of
  // radiance 1 therefore projects to l0 = 1 and l1 = 0 on every channel.
  struct SHL1Channel
  {
    float l0 = 0.0f;
    float l1x = 0.0f;
    float l1y = 0.0f;
    float l1z = 0.0f;
  };

  // Three channels = 12 floats = exactly three vec4 when uploaded to the GPU,
  // one 3D texture per color channel.
  struct SHL1RGB
  {
    SHL1Channel r;
    SHL1Channel g;
    SHL1Channel b;
  };

  static_assert(sizeof(SHL1RGB) == 48, "SHL1RGB must stay three tightly packed vec4");

  // Raw projection sums. Coefficients here are plain SH coefficients (basis
  // applied, cosine lobe NOT applied yet) - only Finalize produces the storage
  // convention documented above.
  struct SHL1Accumulator
  {
    SHL1RGB coefficients {};
    float totalSolidAngle = 0.0f;

    void AddSample(const glm::vec3& color, const glm::vec3& direction, float solidAngle);

    // Normalizes by the accumulated solid angle (exactly 4*pi for a full cube),
    // then applies the cosine convolution.
    SHL1RGB Finalize() const;
  };

  // Face order matches the Vulkan cube layer order used by ReflectionProbeBaker:
  // 0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z, 5 = -Z. Texel (x, y) is addressed
  // with y growing downwards, like the image rows read back from the GPU.
  glm::vec3 CubeFaceTexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t faceSize);

  // Differential solid angle of one cube face texel. Independent of the face.
  float CubeFaceTexelSolidAngle(uint32_t x, uint32_t y, uint32_t faceSize);

  // Irradiance for a surface normal, clamped to zero: an L1 fit can dip negative
  // on strongly directional environments and negative light is never wanted.
  glm::vec3 EvaluateSHL1(const SHL1RGB& sh, const glm::vec3& normal);

  // SH coefficients are linear, so these are used by the flood fill and by
  // any weighted blending of neighbouring nodes.
  SHL1RGB operator+(const SHL1RGB& a, const SHL1RGB& b);
  SHL1RGB operator*(const SHL1RGB& a, float scalar);
}
