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

    float AreaElement(float x, float y)
    {
      return std::atan2(x * y, std::sqrt(x * x + y * y + 1.0f));
    }

    void AddChannel(SHL1Channel& channel, float value, const glm::vec3& direction, float solidAngle)
    {
      float weighted = value * solidAngle;
      channel.l0 += weighted * SH_Y0;
      channel.l1x += weighted * SH_Y1 * direction.x;
      channel.l1y += weighted * SH_Y1 * direction.y;
      channel.l1z += weighted * SH_Y1 * direction.z;
    }

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

  void SHL1Accumulator::AddSample(const glm::vec3& color, const glm::vec3& direction, float solidAngle)
  {
    AddChannel(coefficients.r, color.r, direction, solidAngle);
    AddChannel(coefficients.g, color.g, direction, solidAngle);
    AddChannel(coefficients.b, color.b, direction, solidAngle);
    totalSolidAngle += solidAngle;
  }

  SHL1RGB SHL1Accumulator::Finalize() const
  {
    if (totalSolidAngle <= 0.0f)
      return SHL1RGB {};

    // Texel solid angles of a full cube sum to 4*pi analytically; rescaling by the
    // measured sum absorbs float error so a constant environment projects to exactly l0 = 1.
    float normalization = (4.0f * SH_PI) / totalSolidAngle;

    return SHL1RGB {
      .r = FinalizeChannel(coefficients.r, normalization),
      .g = FinalizeChannel(coefficients.g, normalization),
      .b = FinalizeChannel(coefficients.b, normalization),
    };
  }

  glm::vec3 CubeFaceTexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t faceSize)
  {
    float inv = 2.0f / float(faceSize);
    float u = (float(x) + 0.5f) * inv - 1.0f;
    float v = (float(y) + 0.5f) * inv - 1.0f;

    glm::vec3 direction;
    switch (face)
    {
      case 0: direction = glm::vec3(1.0f, -v, -u); break;
      case 1: direction = glm::vec3(-1.0f, -v, u); break;
      case 2: direction = glm::vec3(u, 1.0f, v); break;
      case 3: direction = glm::vec3(u, -1.0f, -v); break;
      case 4: direction = glm::vec3(u, -v, 1.0f); break;
      default: direction = glm::vec3(-u, -v, -1.0f); break;
    }
    return glm::normalize(direction);
  }

  float CubeFaceTexelSolidAngle(uint32_t x, uint32_t y, uint32_t faceSize)
  {
    float inv = 2.0f / float(faceSize);
    float u0 = float(x) * inv - 1.0f;
    float u1 = float(x + 1) * inv - 1.0f;
    float v0 = float(y) * inv - 1.0f;
    float v1 = float(y + 1) * inv - 1.0f;

    return AreaElement(u0, v0) - AreaElement(u0, v1) - AreaElement(u1, v0) + AreaElement(u1, v1);
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
