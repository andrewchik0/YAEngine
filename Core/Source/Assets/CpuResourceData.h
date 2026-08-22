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
    // Set only by loaders that carry a format of their own (DDS). Left undefined
    // for decoded RGBA8 data, where linear picks between UNORM and SRGB instead.
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool linear = false;
    bool hasAlpha = false;
    bool hasCpuMips = false;
    bool repeat = true;
    // Block-compressed payload: mip sizes come from the block layout, not from
    // width * height * pixelSize, and mips can never be generated on the GPU.
    bool compressed = false;
  };

  // Texture bundled inside a model file (GLB, FBX with embedded media).
  // Either a compressed image payload (width/height 0) or raw RGBA8 pixels.
  struct EmbeddedTexture
  {
    std::vector<uint8_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
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
