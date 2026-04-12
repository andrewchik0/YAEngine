#include "MipGenerator.h"

namespace YAEngine
{
  void MipGenerator::GenerateWithAlphaCoverage(const uint8_t* srcData, uint32_t width, uint32_t height,
                                               uint32_t mipLevels, std::vector<CpuMipLevel>& outMips)
  {
    outMips.resize(mipLevels);
    outMips[0].width = width;
    outMips[0].height = height;
    outMips[0].data.assign(srcData, srcData + width * height * 4);

    constexpr float alphaCutoff = 128.0f;
    uint32_t passCount = 0;
    uint32_t totalTexels = width * height;
    for (uint32_t i = 0; i < totalTexels; i++)
    {
      if (outMips[0].data[i * 4 + 3] >= static_cast<uint8_t>(alphaCutoff))
        passCount++;
    }
    float targetCoverage = static_cast<float>(passCount) / static_cast<float>(totalTexels);

    for (uint32_t m = 1; m < mipLevels; m++)
    {
      uint32_t srcW = outMips[m - 1].width;
      uint32_t srcH = outMips[m - 1].height;
      uint32_t dstW = std::max(1u, srcW / 2);
      uint32_t dstH = std::max(1u, srcH / 2);

      outMips[m].width = dstW;
      outMips[m].height = dstH;
      outMips[m].data.resize(dstW * dstH * 4);

      auto& src = outMips[m - 1].data;
      auto& dst = outMips[m].data;

      for (uint32_t y = 0; y < dstH; y++)
      {
        for (uint32_t x = 0; x < dstW; x++)
        {
          uint32_t sx = x * 2;
          uint32_t sy = y * 2;
          for (uint32_t c = 0; c < 4; c++)
          {
            uint32_t sum = src[(sy * srcW + sx) * 4 + c];
            uint32_t count = 1;
            if (sx + 1 < srcW) { sum += src[(sy * srcW + sx + 1) * 4 + c]; count++; }
            if (sy + 1 < srcH) { sum += src[((sy + 1) * srcW + sx) * 4 + c]; count++; }
            if (sx + 1 < srcW && sy + 1 < srcH) { sum += src[((sy + 1) * srcW + sx + 1) * 4 + c]; count++; }
            dst[(y * dstW + x) * 4 + c] = static_cast<uint8_t>(sum / count);
          }
        }
      }

      uint32_t mipTexels = dstW * dstH;
      float lo = 0.1f, hi = 8.0f;
      for (int32_t iter = 0; iter < 20; iter++)
      {
        float mid = (lo + hi) * 0.5f;
        uint32_t pass = 0;
        for (uint32_t i = 0; i < mipTexels; i++)
        {
          if (dst[i * 4 + 3] * mid >= alphaCutoff)
            pass++;
        }
        float coverage = static_cast<float>(pass) / static_cast<float>(mipTexels);
        if (coverage < targetCoverage)
          lo = mid;
        else
          hi = mid;
      }

      float scale = (lo + hi) * 0.5f;
      for (uint32_t i = 0; i < mipTexels; i++)
      {
        float a = dst[i * 4 + 3] * scale;
        dst[i * 4 + 3] = static_cast<uint8_t>(std::min(255.0f, std::round(a)));
      }
    }
  }
}
