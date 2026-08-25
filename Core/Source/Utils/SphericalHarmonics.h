#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Spherical harmonics, band 0 + band 1 (four coefficients per color channel).
  //
  // STORAGE CONVENTION - the GLSL side must match this exactly. Coefficients are already
  // convolved with the cosine lobe and divided by pi, so irradiance(n) = l0 + l1x*n.x +
  // l1y*n.y + l1z*n.z directly - the same quantity Core/Shaders/irradiance.frag outputs as
  // E/pi. A constant environment of radiance 1 projects to l0 = 1, l1 = 0 on every channel.
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

  // Raw projection sums: basis applied, cosine lobe NOT applied yet - only Finalize
  // produces the storage convention documented above.
  struct SHL1Accumulator
  {
    SHL1RGB coefficients {};
    float totalSolidAngle = 0.0f;

    void AddSample(const glm::vec3& color, const glm::vec3& direction, float solidAngle);

    // Normalizes by the accumulated solid angle (exactly 4*pi for a full cube),
    // then applies the cosine convolution.
    SHL1RGB Finalize() const;
  };

  // Face order matches ReflectionProbeBaker's Vulkan cube layer order (0=+X, 1=-X, 2=+Y,
  // 3=-Y, 4=+Z, 5=-Z); texel (x, y) has y growing downwards, like GPU readback rows.
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
