#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct CpuMipLevel
  {
    std::vector<uint8_t> data;
    uint32_t width, height;
  };

  struct CpuTextureData
  {
    std::vector<CpuMipLevel> mips;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pixelSize = 4;
    bool linear = false;
    bool hasAlpha = false;
    bool hasCpuMips = false;
    bool repeat = true;
  };

  struct CpuMeshData
  {
    std::vector<uint8_t> vertexData;
    std::vector<uint32_t> indices;
    size_t attribOffset = 0;
    size_t vertexCount = 0;
    glm::vec3 minBB { 0.0f };
    glm::vec3 maxBB { 0.0f };
  };

  struct CpuCubeMapFace
  {
    std::vector<uint8_t> pixels;
  };

  struct CpuCubeMapData
  {
    CpuCubeMapFace faces[6];
    uint32_t faceSize = 0;
    uint32_t pixelSize = 4;
  };
}
