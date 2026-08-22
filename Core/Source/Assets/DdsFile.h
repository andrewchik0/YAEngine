#pragma once

#include "Pch.h"
#include "CpuResourceData.h"

namespace YAEngine
{
  // Reader for BC-compressed DDS textures. Blocks are handed to the GPU exactly as
  // they lie in the file, so the mip chain is always the one the file ships with -
  // nothing is decoded here and nothing is generated later.
  class DdsFile
  {
  public:

    static bool IsDds(const void* data, size_t size);

    static bool IsDdsFile(const std::string& path);

    // Extension test only, no file access. Decode() still validates the signature,
    // so a mislabelled file fails there with a diagnostic rather than silently.
    static bool HasDdsExtension(const std::string& path);

    // A DDS file only states its color space when it carries the extended DX10
    // header. Everything else falls back to linear to pick UNORM over SRGB.
    static CpuTextureData Decode(const void* data, size_t size, bool linear, const std::string& debugName);

    static CpuTextureData Load(const std::string& path, bool linear);
  };
}
