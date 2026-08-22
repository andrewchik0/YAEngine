#include "DdsFile.h"

#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    constexpr size_t MAGIC_SIZE = 4;
    constexpr uint32_t HEADER_SIZE = 124;
    constexpr size_t DX10_HEADER_SIZE = 20;
    constexpr uint32_t DDPF_FOURCC = 0x4;
    constexpr uint32_t DDSCAPS2_CUBEMAP = 0x200;
    constexpr uint32_t DDSCAPS2_VOLUME = 0x200000;
    constexpr uint32_t DX10_MISC_TEXTURECUBE = 0x4;
    constexpr uint32_t DX10_DIMENSION_TEXTURE3D = 4;

    // Field offsets from the start of the file, magic included.
    constexpr size_t OFFSET_HEADER_SIZE = 4;
    constexpr size_t OFFSET_HEIGHT = 12;
    constexpr size_t OFFSET_WIDTH = 16;
    constexpr size_t OFFSET_MIP_COUNT = 28;
    constexpr size_t OFFSET_PF_FLAGS = 80;
    constexpr size_t OFFSET_FOURCC = 84;
    constexpr size_t OFFSET_CAPS2 = 112;
    constexpr size_t OFFSET_DXGI_FORMAT = 128;
    constexpr size_t OFFSET_DX10_DIMENSION = 132;
    constexpr size_t OFFSET_DX10_MISC_FLAG = 136;
    constexpr size_t OFFSET_DX10_ARRAY_SIZE = 140;

    // Alpha below this is discarded by gbuffer.frag, so it is what decides whether
    // a texture actually needs the alpha-tested pipeline.
    constexpr uint32_t ALPHA_TEST_THRESHOLD = 128;

    constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
    {
      return static_cast<uint32_t>(static_cast<uint8_t>(a))
        | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
        | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16)
        | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
    }

    // How the alpha of a block format can be inspected. That a format *can* carry
    // alpha says nothing about whether the texture uses it, and assuming it does
    // puts opaque geometry on the discard path and costs early-Z.
    enum class AlphaKind : uint8_t
    {
      None,     // no alpha channel at all
      Explicit, // BC2: four explicit bits per texel
      Blocked,  // BC3: two endpoints plus interpolated 3-bit indices
      Assumed,  // carries alpha, but decoding it here is not worth it (BC7)
    };

    struct BlockFormat
    {
      VkFormat format = VK_FORMAT_UNDEFINED;
      uint32_t blockBytes = 0;
      AlphaKind alpha = AlphaKind::None;
    };

    BlockFormat ResolveFourCC(uint32_t fourCC, bool linear)
    {
      switch (fourCC)
      {
      case MakeFourCC('D', 'X', 'T', '1'):
        return { linear ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 8, AlphaKind::None };
      case MakeFourCC('D', 'X', 'T', '3'):
        return { linear ? VK_FORMAT_BC2_UNORM_BLOCK : VK_FORMAT_BC2_SRGB_BLOCK, 16, AlphaKind::Explicit };
      case MakeFourCC('D', 'X', 'T', '5'):
        return { linear ? VK_FORMAT_BC3_UNORM_BLOCK : VK_FORMAT_BC3_SRGB_BLOCK, 16, AlphaKind::Blocked };
      case MakeFourCC('A', 'T', 'I', '1'):
      case MakeFourCC('B', 'C', '4', 'U'):
        return { VK_FORMAT_BC4_UNORM_BLOCK, 8, AlphaKind::None };
      case MakeFourCC('A', 'T', 'I', '2'):
      case MakeFourCC('B', 'C', '5', 'U'):
        return { VK_FORMAT_BC5_UNORM_BLOCK, 16, AlphaKind::None };
      // BC4S and BC5S are deliberately absent: sampleMaterialNormal() decodes a
      // two-channel normal assuming UNORM, while a SNORM sampler already returns
      // [-1, 1]. Rejecting the file beats loading a texture that shades wrong.
      default:
        return {};
      }
    }

    // DXGI_FORMAT values for the block-compressed families. Typeless and UNORM
    // variants leave the color space open, so those follow the caller's request.
    BlockFormat ResolveDxgi(uint32_t dxgiFormat, bool linear)
    {
      switch (dxgiFormat)
      {
      case 70:
      case 71:
        return { linear ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 8, AlphaKind::None };
      case 72:
        return { VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 8, AlphaKind::None };
      case 73:
      case 74:
        return { linear ? VK_FORMAT_BC2_UNORM_BLOCK : VK_FORMAT_BC2_SRGB_BLOCK, 16, AlphaKind::Explicit };
      case 75:
        return { VK_FORMAT_BC2_SRGB_BLOCK, 16, AlphaKind::Explicit };
      case 76:
      case 77:
        return { linear ? VK_FORMAT_BC3_UNORM_BLOCK : VK_FORMAT_BC3_SRGB_BLOCK, 16, AlphaKind::Blocked };
      case 78:
        return { VK_FORMAT_BC3_SRGB_BLOCK, 16, AlphaKind::Blocked };
      case 79:
      case 80:
        return { VK_FORMAT_BC4_UNORM_BLOCK, 8, AlphaKind::None };
      case 82:
      case 83:
        return { VK_FORMAT_BC5_UNORM_BLOCK, 16, AlphaKind::None };
      case 94:
      case 95:
        return { VK_FORMAT_BC6H_UFLOAT_BLOCK, 16, AlphaKind::None };
      case 96:
        return { VK_FORMAT_BC6H_SFLOAT_BLOCK, 16, AlphaKind::None };
      case 97:
      case 98:
        return { linear ? VK_FORMAT_BC7_UNORM_BLOCK : VK_FORMAT_BC7_SRGB_BLOCK, 16, AlphaKind::Assumed };
      case 99:
        return { VK_FORMAT_BC7_SRGB_BLOCK, 16, AlphaKind::Assumed };
      // 81 (BC4_SNORM) and 84 (BC5_SNORM) are rejected, see ResolveFourCC
      default:
        return {};
      }
    }

    // Four explicit bits per texel, expanded to eight the way the hardware does.
    bool HasSubThresholdAlphaBC2(const uint8_t* blocks, size_t blockCount)
    {
      for (size_t block = 0; block < blockCount; block++)
      {
        const uint8_t* alpha = blocks + block * 16;
        for (uint32_t i = 0; i < 8; i++)
        {
          if ((alpha[i] & 0x0F) * 17u < ALPHA_TEST_THRESHOLD) return true;
          if ((alpha[i] >> 4) * 17u < ALPHA_TEST_THRESHOLD) return true;
        }
      }
      return false;
    }

    // Two endpoints plus sixteen 3-bit indices into an eight entry table. The
    // table is built first so blocks that cannot reach the threshold at all skip
    // the index unpacking entirely - that is the common case in opaque textures.
    bool HasSubThresholdAlphaBC3(const uint8_t* blocks, size_t blockCount)
    {
      for (size_t block = 0; block < blockCount; block++)
      {
        const uint8_t* alpha = blocks + block * 16;
        const uint32_t a0 = alpha[0];
        const uint32_t a1 = alpha[1];

        uint32_t table[8] = { a0, a1 };
        if (a0 > a1)
        {
          for (uint32_t i = 0; i < 6; i++)
            table[2 + i] = ((6 - i) * a0 + (1 + i) * a1) / 7;
        }
        else
        {
          for (uint32_t i = 0; i < 4; i++)
            table[2 + i] = ((4 - i) * a0 + (1 + i) * a1) / 5;
          table[6] = 0;
          table[7] = 255;
        }

        uint32_t belowMask = 0;
        for (uint32_t i = 0; i < 8; i++)
        {
          if (table[i] < ALPHA_TEST_THRESHOLD)
            belowMask |= 1u << i;
        }
        if (belowMask == 0)
          continue;

        uint64_t indices = 0;
        for (uint32_t i = 0; i < 6; i++)
          indices |= uint64_t(alpha[2 + i]) << (8 * i);

        for (uint32_t texel = 0; texel < 16; texel++)
        {
          if ((belowMask >> ((indices >> (3 * texel)) & 7)) & 1u)
            return true;
        }
      }
      return false;
    }

    // Mip 0 only: a smaller level averages sparse cutouts away, and the sub-4x4
    // levels are mostly block padding, so they answer the wrong question.
    bool ScanAlpha(AlphaKind kind, const std::vector<uint8_t>& mip0, uint32_t blockBytes)
    {
      if (blockBytes == 0)
        return true;

      const size_t blockCount = mip0.size() / blockBytes;
      switch (kind)
      {
      case AlphaKind::Explicit: return HasSubThresholdAlphaBC2(mip0.data(), blockCount);
      case AlphaKind::Blocked:  return HasSubThresholdAlphaBC3(mip0.data(), blockCount);
      case AlphaKind::Assumed:  return true;
      default:                  return false;
      }
    }
  }

  bool DdsFile::IsDds(const void* data, size_t size)
  {
    if (data == nullptr || size < MAGIC_SIZE)
      return false;

    const auto* bytes = static_cast<const uint8_t*>(data);
    return bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == ' ';
  }

  bool DdsFile::IsDdsFile(const std::string& path)
  {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
      return false;

    uint8_t magic[MAGIC_SIZE] = {};
    file.read(reinterpret_cast<char*>(magic), MAGIC_SIZE);
    return file.gcount() == static_cast<std::streamsize>(MAGIC_SIZE) && IsDds(magic, MAGIC_SIZE);
  }

  bool DdsFile::HasDdsExtension(const std::string& path)
  {
    if (path.size() < 4)
      return false;

    std::string extension = path.substr(path.size() - 4);
    for (char& c : extension)
      c = char(std::tolower(static_cast<unsigned char>(c)));

    return extension == ".dds";
  }

  CpuTextureData DdsFile::Decode(const void* data, size_t size, bool linear, const std::string& debugName)
  {
    CpuTextureData result;

    if (!IsDds(data, size) || size < MAGIC_SIZE + HEADER_SIZE)
    {
      YA_LOG_ERROR("Assets", "DDS '%s': missing signature or truncated header", debugName.c_str());
      return result;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    auto readU32 = [bytes](size_t offset) -> uint32_t
    {
      return static_cast<uint32_t>(bytes[offset])
        | (static_cast<uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    };

    const uint32_t headerSize = readU32(OFFSET_HEADER_SIZE);
    if (headerSize != HEADER_SIZE)
    {
      YA_LOG_ERROR("Assets", "DDS '%s': unexpected header size %u", debugName.c_str(), headerSize);
      return result;
    }

    const uint32_t height = readU32(OFFSET_HEIGHT);
    const uint32_t width = readU32(OFFSET_WIDTH);
    if (width == 0 || height == 0)
    {
      YA_LOG_ERROR("Assets", "DDS '%s': zero dimensions %ux%u", debugName.c_str(), width, height);
      return result;
    }

    const uint32_t pixelFormatFlags = readU32(OFFSET_PF_FLAGS);
    if ((pixelFormatFlags & DDPF_FOURCC) == 0)
    {
      YA_LOG_ERROR("Assets", "DDS '%s': uncompressed DDS is not supported", debugName.c_str());
      return result;
    }

    // Only plain 2D textures are handled. A cube or volume file would otherwise
    // decode as its first face or slice with every size check still passing, so
    // the result would be silently wrong rather than diagnosably broken.
    const uint32_t caps2 = readU32(OFFSET_CAPS2);
    if ((caps2 & (DDSCAPS2_CUBEMAP | DDSCAPS2_VOLUME)) != 0)
    {
      YA_LOG_ERROR("Assets", "DDS '%s': cubemap and volume textures are not supported (caps2 0x%08X)",
        debugName.c_str(), caps2);
      return result;
    }

    const uint32_t fourCC = readU32(OFFSET_FOURCC);
    size_t payloadOffset = MAGIC_SIZE + HEADER_SIZE;
    BlockFormat blockFormat;

    if (fourCC == MakeFourCC('D', 'X', '1', '0'))
    {
      if (size < payloadOffset + DX10_HEADER_SIZE)
      {
        YA_LOG_ERROR("Assets", "DDS '%s': truncated DX10 header", debugName.c_str());
        return result;
      }

      const uint32_t resourceDimension = readU32(OFFSET_DX10_DIMENSION);
      const uint32_t miscFlag = readU32(OFFSET_DX10_MISC_FLAG);
      const uint32_t arraySize = readU32(OFFSET_DX10_ARRAY_SIZE);
      if (resourceDimension == DX10_DIMENSION_TEXTURE3D
        || (miscFlag & DX10_MISC_TEXTURECUBE) != 0 || arraySize > 1)
      {
        YA_LOG_ERROR("Assets", "DDS '%s': only 2D non-array textures are supported (dimension %u, misc 0x%08X, array size %u)",
          debugName.c_str(), resourceDimension, miscFlag, arraySize);
        return result;
      }

      const uint32_t dxgiFormat = readU32(OFFSET_DXGI_FORMAT);
      blockFormat = ResolveDxgi(dxgiFormat, linear);
      payloadOffset += DX10_HEADER_SIZE;

      if (blockFormat.format == VK_FORMAT_UNDEFINED)
      {
        YA_LOG_ERROR("Assets", "DDS '%s': unsupported DXGI format %u", debugName.c_str(), dxgiFormat);
        return result;
      }
    }
    else
    {
      blockFormat = ResolveFourCC(fourCC, linear);
      if (blockFormat.format == VK_FORMAT_UNDEFINED)
      {
        YA_LOG_ERROR("Assets", "DDS '%s': unsupported FourCC 0x%08X", debugName.c_str(), fourCC);
        return result;
      }
    }

    const uint32_t mipCount = std::max(1u, readU32(OFFSET_MIP_COUNT));

    // Bounded before it sizes anything: for a BC format every level at or below
    // 4x4 is one block, so a header claiming thousands of mips passes the payload
    // check below while asking for an allocation the size of the claim.
    uint32_t maxMipCount = 1;
    for (uint32_t dimension = std::max(width, height); dimension > 1; dimension /= 2)
      maxMipCount++;

    if (mipCount > maxMipCount)
    {
      YA_LOG_ERROR("Assets", "DDS '%s': %u mip levels declared, %ux%u allows at most %u",
        debugName.c_str(), mipCount, width, height, maxMipCount);
      return result;
    }

    std::vector<CpuMipLevel> mips(mipCount);
    size_t offset = payloadOffset;
    uint32_t mipWidth = width;
    uint32_t mipHeight = height;

    for (uint32_t i = 0; i < mipCount; i++)
    {
      const uint32_t blocksX = std::max(1u, (mipWidth + 3) / 4);
      const uint32_t blocksY = std::max(1u, (mipHeight + 3) / 4);
      const size_t levelSize = static_cast<size_t>(blocksX) * blocksY * blockFormat.blockBytes;

      if (offset + levelSize > size)
      {
        YA_LOG_ERROR("Assets", "DDS '%s': payload ends inside mip %u of %u", debugName.c_str(), i, mipCount);
        return result;
      }

      mips[i].width = mipWidth;
      mips[i].height = mipHeight;
      mips[i].data.assign(bytes + offset, bytes + offset + levelSize);

      offset += levelSize;
      mipWidth = std::max(1u, mipWidth / 2);
      mipHeight = std::max(1u, mipHeight / 2);
    }

    result.hasAlpha = ScanAlpha(blockFormat.alpha, mips[0].data, blockFormat.blockBytes);

    result.mips = std::move(mips);
    result.width = width;
    result.height = height;
    result.format = blockFormat.format;
    result.linear = linear;
    result.hasCpuMips = true;
    result.compressed = true;
    result.repeat = true;

    return result;
  }

  CpuTextureData DdsFile::Load(const std::string& path, bool linear)
  {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
      YA_LOG_ERROR("Assets", "DDS: failed to open '%s'", path.c_str());
      return {};
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> contents(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(contents.data()), size))
    {
      YA_LOG_ERROR("Assets", "DDS: failed to read '%s'", path.c_str());
      return {};
    }

    return Decode(contents.data(), contents.size(), linear, path);
  }
}
