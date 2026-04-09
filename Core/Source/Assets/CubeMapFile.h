#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct CubeMapFileHeader
  {
    char magic[4];        // "YACM"
    uint32_t version;     // 1
    uint32_t width;
    uint32_t height;
    uint32_t faceCount;   // 6 for cubemap
    uint32_t mipLevels;
    uint32_t vkFormat;
    uint32_t bytesPerPixel;
  };

  struct CubeMapFileDesc
  {
    uint32_t width;
    uint32_t height;
    uint32_t faceCount;
    uint32_t mipLevels;
    uint32_t vkFormat;
    uint32_t bytesPerPixel;
    const void* data;
    size_t dataSize;
  };

  struct CubeMapFileData
  {
    uint32_t width;
    uint32_t height;
    uint32_t faceCount;
    uint32_t mipLevels;
    uint32_t vkFormat;
    uint32_t bytesPerPixel;
    std::vector<uint8_t> pixels;
  };

  class CubeMapFile
  {
  public:

    static bool Save(const std::string& path, const CubeMapFileDesc& desc);
    static bool Load(const std::string& path, CubeMapFileData& outData);

    static size_t GetMipOffset(uint32_t face, uint32_t mip,
      uint32_t baseWidth, uint32_t baseHeight,
      uint32_t mipLevels, uint32_t bytesPerPixel);

    static size_t GetMipSize(uint32_t mip,
      uint32_t baseWidth, uint32_t baseHeight,
      uint32_t bytesPerPixel);

    static size_t GetTotalSize(uint32_t baseWidth, uint32_t baseHeight,
      uint32_t faceCount, uint32_t mipLevels,
      uint32_t bytesPerPixel);
  };
}
