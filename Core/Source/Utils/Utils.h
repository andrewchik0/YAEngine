#pragma once

#include "Pch.h"

namespace YAEngine
{
  inline float Halton(uint32_t i, uint32_t b)
  {
    float f = 1.0f;
    float r = 0.0f;
    while (i > 0)
    {
      f /= float(b);
      r += f * float(i % b);
      i /= b;
    }
    return r;
  }

  inline glm::vec2 GetTAAJitter(uint64_t frame)
  {
    uint32_t index = static_cast<uint32_t>(frame & 1023);
    return {
      Halton(index, 2) - 0.5f,
      Halton(index, 3) - 0.5f
    };
  }
}