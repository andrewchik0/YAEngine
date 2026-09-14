#include "SphericalHarmonics.h"

namespace YAEngine
{
  namespace
  {
    constexpr float SH_PI = 3.14159265358979323846f;

    // SH basis for bands 0 and 1
    constexpr float SH_Y0 = 0.2820947918f;  // 0.5 * sqrt(1 / pi)
    constexpr float SH_Y1 = 0.4886025119f;  // 0.5 * sqrt(3 / pi)

    // Cosine lobe convolution (Ramamoorthi & Hanrahan 2001): A0 = pi, A1 = 2*pi/3, both
    // divided by pi to match the E/pi convention documented in the header.
    constexpr float SH_COS_A0 = 1.0f;
    constexpr float SH_COS_A1 = 2.0f / 3.0f;

    SHL1Channel FinalizeChannel(const SHL1Channel& channel, float normalization)
    {
      return SHL1Channel {
        .l0 = channel.l0 * normalization * SH_COS_A0 * SH_Y0,
        .l1x = channel.l1x * normalization * SH_COS_A1 * SH_Y1,
        .l1y = channel.l1y * normalization * SH_COS_A1 * SH_Y1,
        .l1z = channel.l1z * normalization * SH_COS_A1 * SH_Y1,
      };
    }

    SHL1Channel AddChannels(const SHL1Channel& a, const SHL1Channel& b)
    {
      return SHL1Channel {
        .l0 = a.l0 + b.l0,
        .l1x = a.l1x + b.l1x,
        .l1y = a.l1y + b.l1y,
        .l1z = a.l1z + b.l1z,
      };
    }

    SHL1Channel ScaleChannel(const SHL1Channel& a, float scalar)
    {
      return SHL1Channel {
        .l0 = a.l0 * scalar,
        .l1x = a.l1x * scalar,
        .l1y = a.l1y * scalar,
        .l1z = a.l1z * scalar,
      };
    }

    float EvaluateChannel(const SHL1Channel& channel, const glm::vec3& normal)
    {
      float value = channel.l0
        + channel.l1x * normal.x
        + channel.l1y * normal.y
        + channel.l1z * normal.z;
      return std::max(value, 0.0f);
    }
  }

  SHL1RGB SHL1Accumulator::Finalize() const
  {
    if (totalSolidAngle <= 0.0f)
      return SHL1RGB {};

    // Rescaling by the measured sum rather than assuming 4*pi absorbs float error, so a
    // constant environment projects to exactly l0 = 1.
    float normalization = (4.0f * SH_PI) / totalSolidAngle;

    return SHL1RGB {
      .r = FinalizeChannel(coefficients.r, normalization),
      .g = FinalizeChannel(coefficients.g, normalization),
      .b = FinalizeChannel(coefficients.b, normalization),
    };
  }

  glm::vec3 EvaluateSHL1(const SHL1RGB& sh, const glm::vec3& normal)
  {
    return glm::vec3(
      EvaluateChannel(sh.r, normal),
      EvaluateChannel(sh.g, normal),
      EvaluateChannel(sh.b, normal));
  }

  SHL1RGB operator+(const SHL1RGB& a, const SHL1RGB& b)
  {
    return SHL1RGB {
      .r = AddChannels(a.r, b.r),
      .g = AddChannels(a.g, b.g),
      .b = AddChannels(a.b, b.b),
    };
  }

  SHL1RGB operator*(const SHL1RGB& a, float scalar)
  {
    return SHL1RGB {
      .r = ScaleChannel(a.r, scalar),
      .g = ScaleChannel(a.g, scalar),
      .b = ScaleChannel(a.b, scalar),
    };
  }
}
