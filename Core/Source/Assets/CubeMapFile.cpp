#include "CubeMapFile.h"

#include "Utils/Log.h"

namespace YAEngine
{
  static constexpr char CUBEMAP_FILE_MAGIC[4] = { 'Y', 'A', 'C', 'M' };
  static constexpr uint32_t CUBEMAP_FILE_VERSION = 1;

  size_t CubeMapFile::GetMipSize(uint32_t mip,
    uint32_t baseWidth, uint32_t baseHeight,
    uint32_t bytesPerPixel)
  {
    uint32_t w = std::max(1u, baseWidth >> mip);
    uint32_t h = std::max(1u, baseHeight >> mip);
    return size_t(w) * h * bytesPerPixel;
  }

  size_t CubeMapFile::GetMipOffset(uint32_t face, uint32_t mip,
    uint32_t baseWidth, uint32_t baseHeight,
    uint32_t mipLevels, uint32_t bytesPerPixel)
  {
    // Data layout: for each face, all mips sequentially
    // [face0_mip0][face0_mip1]...[face0_mipN][face1_mip0]...
    size_t faceSize = 0;
    for (uint32_t m = 0; m < mipLevels; m++)
      faceSize += GetMipSize(m, baseWidth, baseHeight, bytesPerPixel);

    size_t offset = face * faceSize;
    for (uint32_t m = 0; m < mip; m++)
      offset += GetMipSize(m, baseWidth, baseHeight, bytesPerPixel);

    return offset;
  }

  size_t CubeMapFile::GetTotalSize(uint32_t baseWidth, uint32_t baseHeight,
    uint32_t faceCount, uint32_t mipLevels,
    uint32_t bytesPerPixel)
  {
    size_t faceSize = 0;
    for (uint32_t m = 0; m < mipLevels; m++)
      faceSize += GetMipSize(m, baseWidth, baseHeight, bytesPerPixel);
    return faceSize * faceCount;
  }

  bool CubeMapFile::Save(const std::string& path, const CubeMapFileDesc& desc)
  {
    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "wb");
    if (!file)
    {
      YA_LOG_ERROR("Assets", "Failed to open file for writing: %s", path.c_str());
      return false;
    }

    CubeMapFileHeader header {};
    memcpy(header.magic, CUBEMAP_FILE_MAGIC, 4);
    header.version = CUBEMAP_FILE_VERSION;
    header.width = desc.width;
    header.height = desc.height;
    header.faceCount = desc.faceCount;
    header.mipLevels = desc.mipLevels;
    header.vkFormat = desc.vkFormat;
    header.bytesPerPixel = desc.bytesPerPixel;

    if (fwrite(&header, sizeof(header), 1, file) != 1)
    {
      YA_LOG_ERROR("Assets", "Failed to write header: %s", path.c_str());
      fclose(file);
      return false;
    }

    if (fwrite(desc.data, 1, desc.dataSize, file) != desc.dataSize)
    {
      YA_LOG_ERROR("Assets", "Failed to write pixel data: %s", path.c_str());
      fclose(file);
      return false;
    }

    fclose(file);
    return true;
  }

  bool CubeMapFile::Load(const std::string& path, CubeMapFileData& outData)
  {
    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "rb");
    if (!file)
    {
      YA_LOG_ERROR("Assets", "Failed to open file for reading: %s", path.c_str());
      return false;
    }

    CubeMapFileHeader header {};
    if (fread(&header, sizeof(header), 1, file) != 1)
    {
      YA_LOG_ERROR("Assets", "Failed to read header: %s", path.c_str());
      fclose(file);
      return false;
    }

    if (memcmp(header.magic, CUBEMAP_FILE_MAGIC, 4) != 0)
    {
      YA_LOG_ERROR("Assets", "Invalid cubemap file magic: %s", path.c_str());
      fclose(file);
      return false;
    }

    if (header.version != CUBEMAP_FILE_VERSION)
    {
      YA_LOG_ERROR("Assets", "Unsupported cubemap file version %u: %s", header.version, path.c_str());
      fclose(file);
      return false;
    }

    outData.width = header.width;
    outData.height = header.height;
    outData.faceCount = header.faceCount;
    outData.mipLevels = header.mipLevels;
    outData.vkFormat = header.vkFormat;
    outData.bytesPerPixel = header.bytesPerPixel;

    size_t totalSize = GetTotalSize(header.width, header.height,
      header.faceCount, header.mipLevels, header.bytesPerPixel);

    outData.pixels.resize(totalSize);
    if (fread(outData.pixels.data(), 1, totalSize, file) != totalSize)
    {
      YA_LOG_ERROR("Assets", "Failed to read pixel data: %s", path.c_str());
      fclose(file);
      return false;
    }

    fclose(file);
    return true;
  }
}
