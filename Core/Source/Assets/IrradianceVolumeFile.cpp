#include "IrradianceVolumeFile.h"

#include <glm/gtc/packing.hpp>

#include "Utils/FormatText.h"
#include "Utils/Log.h"

#include <windows.h>

namespace YAEngine
{
  static_assert(sizeof(IrradianceVolumeFileHeader) == 100, "IrradianceVolumeFileHeader must match the documented layout");
  static_assert(sizeof(IrradianceBrick) == 16 && offsetof(IrradianceBrick, spacingIndex) == 12,
    "IrradianceBrick is written to .yaiv as is and must stay originKey int32 x3, spacingIndex uint32");
  static_assert(sizeof(SHL1RGBHalf) == 24, "SHL1RGBHalf is written to .yaiv as is and must stay 12 halves");

  namespace
  {
    constexpr char IRRADIANCE_VOLUME_FILE_MAGIC[4] = { 'Y', 'A', 'I', 'V' };
    constexpr uint32_t IRRADIANCE_VOLUME_FILE_VERSION = 4;

    // A bake takes minutes, so a target another process holds open for a moment (a second editor,
    // Explorer's preview, the search indexer, antivirus) is waited out rather than failing the save.
    constexpr uint32_t REPLACE_ATTEMPTS = 6;
    constexpr std::chrono::milliseconds REPLACE_RETRY_DELAY { 250 };
    constexpr uint64_t BRICK_RECORD_BYTES = sizeof(IrradianceBrick);
    constexpr uint64_t BRICK_NODE_INDEX_BYTES = uint64_t(IRRADIANCE_BRICK_NODE_COUNT) * sizeof(uint32_t);
    constexpr uint64_t NODE_BYTES = sizeof(SHL1RGBHalf) + sizeof(uint8_t);
    constexpr uint64_t INDIRECTION_CELL_BYTES = sizeof(uint32_t);

    bool Reject(std::string& outFailure, const char* format, ...)
    {
      va_list args;
      va_start(args, format);
      FormatText(outFailure, format, args);
      va_end(args);
      return false;
    }

    // Capped at UINT32_MAX: cells are addressed with uint32 indices.
    bool ComputeCellCount(const glm::uvec3& dims, uint64_t& outCount)
    {
      if (dims.x == 0 || dims.y == 0 || dims.z == 0)
        return false;

      uint64_t count = uint64_t(dims.x) * uint64_t(dims.y);
      if (count > UINT32_MAX)
        return false;
      count *= uint64_t(dims.z);
      if (count > UINT32_MAX)
        return false;

      outCount = count;
      return true;
    }

    uint64_t ComputeFileSize(uint64_t brickCount, uint64_t nodeCount, uint64_t cellCount)
    {
      return sizeof(IrradianceVolumeFileHeader) + brickCount * (BRICK_RECORD_BYTES + BRICK_NODE_INDEX_BYTES)
        + nodeCount * NODE_BYTES + cellCount * INDIRECTION_CELL_BYTES;
    }

    bool WriteBlob(FILE* file, const void* bytes, size_t size, const char* what, const std::string& path)
    {
      if (fwrite(bytes, 1, size, file) == size)
        return true;

      YA_LOG_ERROR("Assets", "Failed to write irradiance volume %s: %s", what, path.c_str());
      return false;
    }

    bool ReadBlob(FILE* file, void* bytes, size_t size, const char* what, const std::string& path)
    {
      if (fread(bytes, 1, size, file) == size)
        return true;

      YA_LOG_ERROR("Assets", "Failed to read irradiance volume %s: %s", what, path.c_str());
      return false;
    }

    bool WriteVolume(FILE* file, const IrradianceVolumeFileData& data, const std::string& path)
    {
      IrradianceVolumeFileHeader header {};
      memcpy(header.magic, IRRADIANCE_VOLUME_FILE_MAGIC, 4);
      header.version = IRRADIANCE_VOLUME_FILE_VERSION;
      header.format = uint32_t(IrradianceVolumeFormat::SHL1RGBHalf);
      header.position[0] = data.position.x;
      header.position[1] = data.position.y;
      header.position[2] = data.position.z;
      header.rotation[0] = data.rotation.x;
      header.rotation[1] = data.rotation.y;
      header.rotation[2] = data.rotation.z;
      header.rotation[3] = data.rotation.w;
      header.halfExtents[0] = data.halfExtents.x;
      header.halfExtents[1] = data.halfExtents.y;
      header.halfExtents[2] = data.halfExtents.z;
      header.edgeFade = data.edgeFade;
      header.minSpacingIndex = data.minSpacingIndex;
      header.maxSpacingIndex = data.maxSpacingIndex;
      header.brickCount = uint32_t(data.bricks.size());
      header.nodeCount = data.GetNodeCount();
      for (int32_t axis = 0; axis < 3; axis++)
      {
        header.indirectionOriginKey[axis] = data.indirectionOriginKey[axis];
        header.indirectionDims[axis] = data.indirectionDims[axis];
      }
      header.indirectionCellKeys = data.indirectionCellKeys;

      return WriteBlob(file, &header, sizeof(header), "header", path)
        && WriteBlob(file, data.bricks.data(), data.bricks.size() * sizeof(IrradianceBrick), "bricks", path)
        && WriteBlob(file, data.brickNodeIndices.data(), data.brickNodeIndices.size() * sizeof(uint32_t), "brick node indices", path)
        && WriteBlob(file, data.coefficients.data(), data.coefficients.size() * sizeof(SHL1RGBHalf), "coefficients", path)
        && WriteBlob(file, data.validity.data(), data.validity.size(), "validity", path)
        && WriteBlob(file, data.indirection.data(), data.indirection.size() * sizeof(uint32_t), "indirection", path);
    }

    bool ReadVolume(FILE* file, const std::string& path, IrradianceVolumeFileData& outData)
    {
      // Magic and version first, so a file of any other version, shorter header included, is told
      // apart from a damaged one.
      char prefix[8] = {};
      if (fread(prefix, 1, sizeof(prefix), file) != sizeof(prefix))
      {
        YA_LOG_ERROR("Assets", "Failed to read irradiance volume header: %s", path.c_str());
        return false;
      }

      if (memcmp(prefix, IRRADIANCE_VOLUME_FILE_MAGIC, 4) != 0)
      {
        YA_LOG_ERROR("Assets", "Invalid irradiance volume file magic: %s", path.c_str());
        return false;
      }

      // No conversion from older versions on purpose: a v2 uniform lattice does not become a sparse
      // brick layout without tracing the nodes the bricks add, and a v3 file has no edge fade and
      // kept nodes that stand too close to geometry.
      uint32_t version = 0;
      memcpy(&version, prefix + 4, sizeof(version));
      if (version != IRRADIANCE_VOLUME_FILE_VERSION)
      {
        YA_LOG_ERROR("Assets", "Irradiance volume is version %u, expected %u (v4 adds the per-volume edge fade and rejects nodes too close to geometry) - rebake it: %s",
          version, IRRADIANCE_VOLUME_FILE_VERSION, path.c_str());
        return false;
      }

      IrradianceVolumeFileHeader header {};
      if (fseek(file, 0, SEEK_SET) != 0 || fread(&header, sizeof(header), 1, file) != 1)
      {
        YA_LOG_ERROR("Assets", "Failed to read irradiance volume header: %s", path.c_str());
        return false;
      }

      if (header.format != uint32_t(IrradianceVolumeFormat::SHL1RGBHalf))
      {
        YA_LOG_ERROR("Assets", "Unsupported irradiance volume format %u: %s", header.format, path.c_str());
        return false;
      }

      const glm::uvec3 dims(header.indirectionDims[0], header.indirectionDims[1], header.indirectionDims[2]);
      uint64_t cellCount = 0;
      if (header.brickCount == 0 || header.nodeCount == 0 || !ComputeCellCount(dims, cellCount))
      {
        YA_LOG_ERROR("Assets", "Irradiance volume has %u bricks, %u nodes and %u x %u x %u indirection cells: %s",
          header.brickCount, header.nodeCount, dims.x, dims.y, dims.z, path.c_str());
        return false;
      }

      // The counts size every allocation below, so they are held against the file size first: a
      // truncated file or a header tuned to wrap would otherwise allocate or read garbage.
      if (_fseeki64(file, 0, SEEK_END) != 0)
      {
        YA_LOG_ERROR("Assets", "Failed to seek irradiance volume file: %s", path.c_str());
        return false;
      }

      const int64_t fileSize = _ftelli64(file);
      const uint64_t expectedSize = ComputeFileSize(header.brickCount, header.nodeCount, cellCount);
      if (fileSize < 0 || uint64_t(fileSize) != expectedSize)
      {
        YA_LOG_ERROR("Assets", "Irradiance volume size mismatch (%lld bytes, expected %llu): %s",
          (long long)fileSize, (unsigned long long)expectedSize, path.c_str());
        return false;
      }

      if (_fseeki64(file, int64_t(sizeof(IrradianceVolumeFileHeader)), SEEK_SET) != 0)
      {
        YA_LOG_ERROR("Assets", "Failed to seek irradiance volume payload: %s", path.c_str());
        return false;
      }

      outData.position = glm::vec3(header.position[0], header.position[1], header.position[2]);
      outData.rotation = glm::quat(header.rotation[3], header.rotation[0], header.rotation[1], header.rotation[2]);
      outData.halfExtents = glm::vec3(header.halfExtents[0], header.halfExtents[1], header.halfExtents[2]);
      outData.edgeFade = header.edgeFade;
      outData.minSpacingIndex = header.minSpacingIndex;
      outData.maxSpacingIndex = header.maxSpacingIndex;
      outData.indirectionOriginKey = glm::ivec3(header.indirectionOriginKey[0], header.indirectionOriginKey[1],
        header.indirectionOriginKey[2]);
      outData.indirectionDims = dims;
      outData.indirectionCellKeys = header.indirectionCellKeys;

      outData.bricks.resize(header.brickCount);
      outData.brickNodeIndices.resize(size_t(header.brickCount) * IRRADIANCE_BRICK_NODE_COUNT);
      outData.coefficients.resize(header.nodeCount);
      outData.validity.resize(header.nodeCount);
      outData.indirection.resize(size_t(cellCount));

      if (!ReadBlob(file, outData.bricks.data(), outData.bricks.size() * sizeof(IrradianceBrick), "bricks", path)
        || !ReadBlob(file, outData.brickNodeIndices.data(), outData.brickNodeIndices.size() * sizeof(uint32_t), "brick node indices", path)
        || !ReadBlob(file, outData.coefficients.data(), outData.coefficients.size() * sizeof(SHL1RGBHalf), "coefficients", path)
        || !ReadBlob(file, outData.validity.data(), outData.validity.size(), "validity", path)
        || !ReadBlob(file, outData.indirection.data(), outData.indirection.size() * sizeof(uint32_t), "indirection", path))
      {
        return false;
      }

      std::string failure;
      if (!IrradianceVolumeFile::Validate(outData, failure))
      {
        YA_LOG_ERROR("Assets", "Irradiance volume is inconsistent (%s): %s", failure.c_str(), path.c_str());
        return false;
      }

      return true;
    }

    std::string DescribeWin32Error(DWORD error)
    {
      return std::system_category().message(int(error));
    }

    // One attempt at moving source over target. FileRenameInfoEx with POSIX semantics replaces a
    // target that other processes hold open with FILE_SHARE_DELETE, where MoveFileEx fails with a
    // sharing violation. It needs Windows 10 1709 and NTFS, so MoveFileEx is tried when it fails.
    bool TryReplaceFile(const std::filesystem::path& source, const std::filesystem::path& target, std::string& outError)
    {
      std::error_code pathError;
      std::filesystem::path absoluteTarget = std::filesystem::absolute(target, pathError);
      if (pathError)
        absoluteTarget = target;
      const std::wstring targetName = absoluteTarget.make_preferred().wstring();

      // FILE_RENAME_INFO already holds one character, room for the terminator.
      std::vector<uint8_t> infoBytes(sizeof(FILE_RENAME_INFO) + targetName.size() * sizeof(wchar_t));
      FILE_RENAME_INFO* info = static_cast<FILE_RENAME_INFO*>(static_cast<void*>(infoBytes.data()));
      info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
      info->RootDirectory = nullptr;
      info->FileNameLength = DWORD(targetName.size() * sizeof(wchar_t));
      memcpy(info->FileName, targetName.c_str(), (targetName.size() + 1) * sizeof(wchar_t));

      DWORD renameError = ERROR_SUCCESS;
      const HANDLE handle = CreateFileW(source.wstring().c_str(), DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (handle == INVALID_HANDLE_VALUE)
      {
        renameError = GetLastError();
      }
      else
      {
        const bool renamed = SetFileInformationByHandle(handle, FileRenameInfoEx, info, DWORD(infoBytes.size())) != FALSE;
        if (!renamed)
          renameError = GetLastError();
        CloseHandle(handle);
        if (renamed)
          return true;
      }

      if (MoveFileExW(source.wstring().c_str(), targetName.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE)
        return true;

      const DWORD moveError = GetLastError();
      outError = "rename: " + DescribeWin32Error(renameError) + "; move: " + DescribeWin32Error(moveError);
      return false;
    }
  }

  SHL1RGBHalf PackSHL1RGBHalf(const SHL1RGB& sh)
  {
    SHL1RGBHalf packed;
    const SHL1Channel* channels[3] = { &sh.r, &sh.g, &sh.b };
    for (size_t c = 0; c < 3; c++)
    {
      packed.halves[c * 4] = glm::packHalf1x16(channels[c]->l0);
      packed.halves[c * 4 + 1] = glm::packHalf1x16(channels[c]->l1x);
      packed.halves[c * 4 + 2] = glm::packHalf1x16(channels[c]->l1y);
      packed.halves[c * 4 + 3] = glm::packHalf1x16(channels[c]->l1z);
    }
    return packed;
  }

  SHL1RGB UnpackSHL1RGBHalf(const SHL1RGBHalf& packed)
  {
    SHL1RGB sh;
    SHL1Channel* channels[3] = { &sh.r, &sh.g, &sh.b };
    for (size_t c = 0; c < 3; c++)
    {
      channels[c]->l0 = glm::unpackHalf1x16(packed.halves[c * 4]);
      channels[c]->l1x = glm::unpackHalf1x16(packed.halves[c * 4 + 1]);
      channels[c]->l1y = glm::unpackHalf1x16(packed.halves[c * 4 + 2]);
      channels[c]->l1z = glm::unpackHalf1x16(packed.halves[c * 4 + 3]);
    }
    return sh;
  }

  bool IrradianceVolumeFile::Validate(const IrradianceVolumeFileData& data, std::string& outFailure)
  {
    outFailure.clear();

    const uint64_t brickCount = data.bricks.size();
    const uint64_t nodeCount = data.validity.size();
    if (brickCount == 0 || nodeCount == 0 || brickCount > UINT32_MAX || nodeCount > UINT32_MAX)
    {
      return Reject(outFailure, "%llu bricks and %llu nodes, both must be between 1 and %u",
        (unsigned long long)brickCount, (unsigned long long)nodeCount, UINT32_MAX);
    }

    if (data.minSpacingIndex > data.maxSpacingIndex || data.maxSpacingIndex >= IRRADIANCE_SPACINGS.size())
      return Reject(outFailure, "spacing indices %u..%u are out of range", data.minSpacingIndex, data.maxSpacingIndex);

    // The shader divides by it.
    if (!std::isfinite(data.edgeFade) || data.edgeFade <= 0.0f)
      return Reject(outFailure, "edge fade %g m is not finite and positive", double(data.edgeFade));

    if (data.indirectionCellKeys != uint32_t(GetIrradianceBrickSizeKeys(data.minSpacingIndex)))
    {
      return Reject(outFailure, "indirection cell of %u keys, the %g m bricks are %d", data.indirectionCellKeys,
        double(IRRADIANCE_SPACINGS[data.minSpacingIndex]), GetIrradianceBrickSizeKeys(data.minSpacingIndex));
    }

    uint64_t cellCount = 0;
    if (!ComputeCellCount(data.indirectionDims, cellCount))
    {
      return Reject(outFailure, "indirection of %u x %u x %u cells is empty or exceeds %u cells",
        data.indirectionDims.x, data.indirectionDims.y, data.indirectionDims.z, UINT32_MAX);
    }

    if (data.brickNodeIndices.size() != brickCount * IRRADIANCE_BRICK_NODE_COUNT || data.coefficients.size() != nodeCount
      || data.indirection.size() != cellCount)
    {
      return Reject(outFailure, "%zu node indices, %zu coefficients and %zu indirection cells for %llu bricks, %llu nodes and %llu cells",
        data.brickNodeIndices.size(), data.coefficients.size(), data.indirection.size(),
        (unsigned long long)brickCount, (unsigned long long)nodeCount, (unsigned long long)cellCount);
    }

    for (size_t b = 0; b < data.bricks.size(); b++)
    {
      const uint32_t spacingIndex = data.bricks[b].spacingIndex;
      if (spacingIndex < data.minSpacingIndex || spacingIndex > data.maxSpacingIndex)
      {
        return Reject(outFailure, "brick %zu has spacing index %u outside %u..%u", b, spacingIndex,
          data.minSpacingIndex, data.maxSpacingIndex);
      }
    }

    for (size_t i = 0; i < data.brickNodeIndices.size(); i++)
    {
      if (data.brickNodeIndices[i] >= nodeCount)
      {
        return Reject(outFailure, "brick %zu node %zu index %u exceeds %llu nodes", i / IRRADIANCE_BRICK_NODE_COUNT,
          i % IRRADIANCE_BRICK_NODE_COUNT, data.brickNodeIndices[i], (unsigned long long)nodeCount);
      }
    }

    for (size_t c = 0; c < data.indirection.size(); c++)
    {
      const uint32_t brick = data.indirection[c];
      if (brick != IRRADIANCE_BRICK_INVALID && brick >= brickCount)
        return Reject(outFailure, "indirection cell %zu names brick %u of %llu", c, brick, (unsigned long long)brickCount);
    }

    return true;
  }

  bool IrradianceVolumeFile::Save(const std::string& path, const IrradianceVolumeFileData& data)
  {
    std::string failure;
    if (!Validate(data, failure))
    {
      YA_LOG_ERROR("Assets", "Irradiance volume data is inconsistent (%s), not saved: %s", failure.c_str(), path.c_str());
      return false;
    }

    // Written aside and moved over the target, so a write failing halfway leaves an earlier bake intact.
    const std::string tempPath = path + ".tmp";
    FILE* file = nullptr;
    fopen_s(&file, tempPath.c_str(), "wb");
    if (!file)
    {
      YA_LOG_ERROR("Assets", "Failed to open file for writing: %s", tempPath.c_str());
      return false;
    }

    bool written = WriteVolume(file, data, tempPath);
    if (fclose(file) != 0 && written)
    {
      YA_LOG_ERROR("Assets", "Failed to finish writing irradiance volume: %s", tempPath.c_str());
      written = false;
    }

    if (!written)
    {
      std::error_code error;
      std::filesystem::remove(tempPath, error);
      return false;
    }

    std::string replaceError;
    for (uint32_t attempt = 1; ; attempt++)
    {
      if (TryReplaceFile(tempPath, path, replaceError))
        return true;
      if (attempt == REPLACE_ATTEMPTS)
        break;
      std::this_thread::sleep_for(REPLACE_RETRY_DELAY);
    }

    // The .tmp is a complete, validated bake: deleting it would throw away minutes of integration.
    YA_LOG_ERROR("Assets", "Failed to replace irradiance volume %s after %u attempts (%s); the complete bake is kept in %s",
      path.c_str(), REPLACE_ATTEMPTS, replaceError.c_str(), tempPath.c_str());
    return false;
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

    // Filled locally so a failed read leaves outData exactly as the caller passed it
    IrradianceVolumeFileData data;
    const bool loaded = ReadVolume(file, path, data);
    fclose(file);
    if (!loaded)
      return false;

    outData = std::move(data);
    return true;
  }
}
