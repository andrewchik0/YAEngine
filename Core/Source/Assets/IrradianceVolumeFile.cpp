#include "IrradianceVolumeFile.h"

#include "Utils/Log.h"

namespace YAEngine
{
  static constexpr char IRRADIANCE_VOLUME_FILE_MAGIC[4] = { 'Y', 'A', 'I', 'V' };
  static constexpr uint32_t IRRADIANCE_VOLUME_FILE_VERSION = 2;

  size_t IrradianceVolumeFile::GetCoefficientBlobSize(uint32_t nodeCount)
  {
    return size_t(nodeCount) * sizeof(SHL1RGB);
  }

  size_t IrradianceVolumeFile::GetValidityBlobSize(uint32_t nodeCount)
  {
    return size_t(nodeCount);
  }

  size_t IrradianceVolumeFile::GetTotalSize(uint32_t nodeCount)
  {
    return sizeof(IrradianceVolumeFileHeader)
      + GetCoefficientBlobSize(nodeCount)
      + GetValidityBlobSize(nodeCount);
  }

  uint32_t IrradianceVolumeFile::GetNodeIndex(uint32_t x, uint32_t y, uint32_t z,
    uint32_t nodesX, uint32_t nodesY)
  {
    return x + y * nodesX + z * nodesX * nodesY;
  }

  bool IrradianceVolumeFile::Save(const std::string& path, const IrradianceVolumeFileDesc& desc)
  {
    uint64_t expectedNodes = uint64_t(desc.nodesX) * desc.nodesY * desc.nodesZ;
    if (desc.nodeCount != expectedNodes || !desc.coefficients || !desc.validity)
    {
      YA_LOG_ERROR("Assets", "Irradiance volume desc is inconsistent (%ux%ux%u vs %u nodes): %s",
        desc.nodesX, desc.nodesY, desc.nodesZ, desc.nodeCount, path.c_str());
      return false;
    }

    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "wb");
    if (!file)
    {
      YA_LOG_ERROR("Assets", "Failed to open file for writing: %s", path.c_str());
      return false;
    }

    IrradianceVolumeFileHeader header {};
    memcpy(header.magic, IRRADIANCE_VOLUME_FILE_MAGIC, 4);
    header.version = IRRADIANCE_VOLUME_FILE_VERSION;
    header.nodesX = desc.nodesX;
    header.nodesY = desc.nodesY;
    header.nodesZ = desc.nodesZ;
    header.spacing = desc.spacing;
    header.latticeOrigin[0] = desc.latticeOrigin.x;
    header.latticeOrigin[1] = desc.latticeOrigin.y;
    header.latticeOrigin[2] = desc.latticeOrigin.z;
    header.position[0] = desc.position.x;
    header.position[1] = desc.position.y;
    header.position[2] = desc.position.z;
    header.rotation[0] = desc.rotation.x;
    header.rotation[1] = desc.rotation.y;
    header.rotation[2] = desc.rotation.z;
    header.rotation[3] = desc.rotation.w;
    header.halfExtents[0] = desc.halfExtents.x;
    header.halfExtents[1] = desc.halfExtents.y;
    header.halfExtents[2] = desc.halfExtents.z;
    header.format = uint32_t(IrradianceVolumeFormat::SHL1RGBFloat32);
    header.nodeCount = desc.nodeCount;

    if (fwrite(&header, sizeof(header), 1, file) != 1)
    {
      YA_LOG_ERROR("Assets", "Failed to write irradiance volume header: %s", path.c_str());
      fclose(file);
      return false;
    }

    size_t coeffSize = GetCoefficientBlobSize(desc.nodeCount);
    if (fwrite(desc.coefficients, 1, coeffSize, file) != coeffSize)
    {
      YA_LOG_ERROR("Assets", "Failed to write irradiance volume coefficients: %s", path.c_str());
      fclose(file);
      return false;
    }

    size_t validitySize = GetValidityBlobSize(desc.nodeCount);
    if (fwrite(desc.validity, 1, validitySize, file) != validitySize)
    {
      YA_LOG_ERROR("Assets", "Failed to write irradiance volume validity: %s", path.c_str());
      fclose(file);
      return false;
    }

    fclose(file);
    return true;
  }

  bool IrradianceVolumeFile::Load(const std::string& path, IrradianceVolumeFileData& outData)
  {
    FILE* file = nullptr;
    fopen_s(&file, path.c_str(), "rb");
    if (!file)
    {
      YA_LOG_ERROR("Assets", "Failed to open file for reading: %s", path.c_str());
      return false;
    }

    IrradianceVolumeFileHeader header {};
    if (fread(&header, sizeof(header), 1, file) != 1)
    {
      YA_LOG_ERROR("Assets", "Failed to read irradiance volume header: %s", path.c_str());
      fclose(file);
      return false;
    }

    if (memcmp(header.magic, IRRADIANCE_VOLUME_FILE_MAGIC, 4) != 0)
    {
      YA_LOG_ERROR("Assets", "Invalid irradiance volume file magic: %s", path.c_str());
      fclose(file);
      return false;
    }

    // No v1 -> v2 conversion on purpose: v1 nodes sat on a per-volume lattice
    // whose geometry cannot be reconstructed on the world lattice without
    // re-capturing them.
    if (header.version != IRRADIANCE_VOLUME_FILE_VERSION)
    {
      YA_LOG_ERROR("Assets", "Irradiance volume is version %u, expected %u - rebake it: %s",
        header.version, IRRADIANCE_VOLUME_FILE_VERSION, path.c_str());
      fclose(file);
      return false;
    }

    if (header.format != uint32_t(IrradianceVolumeFormat::SHL1RGBFloat32))
    {
      YA_LOG_ERROR("Assets", "Unsupported irradiance volume format %u: %s",
        header.format, path.c_str());
      fclose(file);
      return false;
    }

    // 64-bit on purpose: the product overflows uint32 for large axes, and a header
    // tuned to wrap would otherwise pass this check with axes the loader never
    // validates again.
    uint64_t expectedNodes = uint64_t(header.nodesX) * header.nodesY * header.nodesZ;
    if (header.nodeCount != expectedNodes || header.nodeCount == 0)
    {
      YA_LOG_ERROR("Assets", "Irradiance volume node count %u does not match grid %ux%ux%u: %s",
        header.nodeCount, header.nodesX, header.nodesY, header.nodesZ, path.c_str());
      fclose(file);
      return false;
    }

    // A truncated file would otherwise read as zeroed coefficients and show up
    // as silently black lighting, so the size is checked before any allocation
    if (fseek(file, 0, SEEK_END) != 0)
    {
      YA_LOG_ERROR("Assets", "Failed to seek irradiance volume file: %s", path.c_str());
      fclose(file);
      return false;
    }

    int64_t fileSize = _ftelli64(file);
    size_t expectedSize = GetTotalSize(header.nodeCount);
    if (fileSize < 0 || size_t(fileSize) != expectedSize)
    {
      YA_LOG_ERROR("Assets", "Irradiance volume size mismatch (%lld bytes, expected %zu): %s",
        fileSize, expectedSize, path.c_str());
      fclose(file);
      return false;
    }

    if (fseek(file, long(sizeof(IrradianceVolumeFileHeader)), SEEK_SET) != 0)
    {
      YA_LOG_ERROR("Assets", "Failed to seek irradiance volume payload: %s", path.c_str());
      fclose(file);
      return false;
    }

    // Filled locally so a failed read leaves outData exactly as the caller passed it
    IrradianceVolumeFileData data;
    data.nodesX = header.nodesX;
    data.nodesY = header.nodesY;
    data.nodesZ = header.nodesZ;
    data.spacing = header.spacing;
    data.latticeOrigin = glm::vec3(header.latticeOrigin[0], header.latticeOrigin[1],
      header.latticeOrigin[2]);
    data.position = glm::vec3(header.position[0], header.position[1], header.position[2]);
    data.rotation = glm::quat(header.rotation[3], header.rotation[0],
      header.rotation[1], header.rotation[2]);
    data.halfExtents = glm::vec3(header.halfExtents[0], header.halfExtents[1], header.halfExtents[2]);

    data.coefficients.resize(header.nodeCount);
    size_t coeffSize = GetCoefficientBlobSize(header.nodeCount);
    if (fread(data.coefficients.data(), 1, coeffSize, file) != coeffSize)
    {
      YA_LOG_ERROR("Assets", "Failed to read irradiance volume coefficients: %s", path.c_str());
      fclose(file);
      return false;
    }

    data.validity.resize(header.nodeCount);
    size_t validitySize = GetValidityBlobSize(header.nodeCount);
    if (fread(data.validity.data(), 1, validitySize, file) != validitySize)
    {
      YA_LOG_ERROR("Assets", "Failed to read irradiance volume validity: %s", path.c_str());
      fclose(file);
      return false;
    }

    fclose(file);
    outData = std::move(data);
    return true;
  }
}
