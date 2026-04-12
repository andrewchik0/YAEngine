#pragma once

#include "Assets/CpuResourceData.h"

namespace YAEngine
{
  class MipGenerator
  {
  public:
    static void GenerateWithAlphaCoverage(const uint8_t* srcData, uint32_t width, uint32_t height,
                                          uint32_t mipLevels, std::vector<CpuMipLevel>& outMips);
  };
}
