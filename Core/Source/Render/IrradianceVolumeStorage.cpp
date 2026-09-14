#include "IrradianceVolumeStorage.h"

#include <glm/gtc/matrix_transform.hpp>

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "VulkanBuffer.h"
#include "ImageBarrier.h"
#include "DebugMarker.h"
#include "Utils/FormatText.h"
#include "Utils/Log.h"

namespace YAEngine
{
  // std140: mat4 + two vec4 + two ivec4, everything already 16-aligned, no implicit padding
  static_assert(sizeof(IrradianceVolumeInfo) == 128,
    "IrradianceVolumeInfo must match its std140 layout");
  static_assert(sizeof(IrradianceVolumeBuffer) == 32 + 128 * MAX_IRRADIANCE_VOLUMES,
    "IrradianceVolumeBuffer must match its std140 layout");
  static_assert(uint32_t(IRRADIANCE_POOL_BRICK_TEXELS) == IRRADIANCE_BRICK_NODES);
  static_assert(IRRADIANCE_INDIRECTION_INVALID == IRRADIANCE_BRICK_INVALID);
  static_assert(IRRADIANCE_FINEST_SPACING == IRRADIANCE_SPACINGS[0]);
  static_assert((IRRADIANCE_SPACINGS.size() - 1) <= (UINT32_MAX >> IRRADIANCE_POOL_SPACING_SHIFT));

  namespace
  {
    constexpr VkFormat COEFFICIENT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr VkFormat VALIDITY_FORMAT = VK_FORMAT_R8_UNORM;
    constexpr VkFormat INDIRECTION_FORMAT = VK_FORMAT_R32_UINT;

    constexpr uint32_t BRICK_TEXELS = IRRADIANCE_POOL_BRICK_TEXELS;

    // Four halfs per texel: (L0, L1x, L1y, L1z) of one color channel
    constexpr size_t COEFFICIENT_TEXEL_SIZE = 4 * sizeof(uint16_t);

    bool Reject(std::string& outFailure, const char* format, ...)
    {
      va_list args;
      va_start(args, format);
      FormatText(outFailure, format, args);
      va_end(args);
      return false;
    }

    // Smallest near-cubic grid holding slotCount slots. Each axis stays at or below the cube root
    // rounded up, so a count that fits maxSlotsPerAxis^3 never pushes an axis past the limit.
    glm::uvec3 ComputeSlotGrid(uint64_t slotCount)
    {
      uint64_t x = 1;
      while (x * x * x < slotCount)
        x++;
      uint64_t y = 1;
      while (x * y * y < slotCount)
        y++;
      uint64_t z = (slotCount + x * y - 1) / (x * y);
      return glm::uvec3(uint32_t(x), uint32_t(y), uint32_t(std::max<uint64_t>(z, 1)));
    }

    // Cell grids placed side by side along one axis of the indirection atlas: that axis sums the
    // grids, the other two take the largest.
    struct AtlasPacking
    {
      int32_t axis = 0;
      std::array<uint64_t, 3> size {};
      // Per packed volume, the atlas texel along axis where its cell (0,0,0) sits.
      std::vector<uint32_t> offsets;
    };

    std::array<uint64_t, 3> GetGrownAtlasSize(const AtlasPacking& packing, const glm::uvec3& dims)
    {
      std::array<uint64_t, 3> size = packing.size;
      for (int32_t a = 0; a < 3; a++)
        size[a] = a == packing.axis ? size[a] + dims[a] : std::max<uint64_t>(size[a], dims[a]);
      return size;
    }

    uint64_t GetAtlasTexels(const std::array<uint64_t, 3>& size)
    {
      return size[0] * size[1] * size[2];
    }

    // IrradianceVolumeFile::Validate checks counts and indices only. A box failing this would reach
    // the shader as a NaN transform, and a key range past int32 would overflow the checks below.
    // A single volume's cells along one axis must also fit one texture on their own.
    bool ValidateBoxAndBounds(const IrradianceVolumeFileData& volume, uint32_t maxDimension, std::string& outFailure)
    {
      const glm::vec4 rotation(volume.rotation.x, volume.rotation.y, volume.rotation.z, volume.rotation.w);
      if (glm::any(glm::isnan(volume.position)) || glm::any(glm::isinf(volume.position))
        || glm::any(glm::isnan(volume.halfExtents)) || glm::any(glm::isinf(volume.halfExtents))
        || glm::any(glm::isnan(rotation)) || glm::any(glm::isinf(rotation)))
      {
        return Reject(outFailure, "the box position, rotation or half extents are not finite");
      }

      if (glm::any(glm::lessThanEqual(volume.halfExtents, glm::vec3(0.0f))))
      {
        return Reject(outFailure, "half extents %g x %g x %g are not positive", double(volume.halfExtents.x),
          double(volume.halfExtents.y), double(volume.halfExtents.z));
      }

      const float rotationLength = glm::length(rotation);
      if (std::abs(rotationLength - 1.0f) > 1e-3f)
        return Reject(outFailure, "the rotation quaternion has length %g", double(rotationLength));

      for (int32_t axis = 0; axis < 3; axis++)
      {
        const uint32_t cells = volume.indirectionDims[axis];
        if (cells > maxDimension)
          return Reject(outFailure, "%u indirection cells along axis %d exceed the device limit of %u", cells, axis, maxDimension);

        const int64_t endKey = int64_t(volume.indirectionOriginKey[axis]) + int64_t(cells) * int64_t(volume.indirectionCellKeys);
        if (endKey > int64_t(INT32_MAX))
          return Reject(outFailure, "the indirection along axis %d ends at key %lld, past the int32 range", axis, (long long)endKey);
      }
      return true;
    }

    // The shader derives a brick's origin from the cell alone, as the multiple of the brick size
    // at or below it. IrradianceVolumeFile::Validate checks counts and ranges, not that.
    bool ValidateIndirectionGeometry(const IrradianceVolumeFileData& volume, std::string& outFailure)
    {
      const glm::ivec3 dims(volume.indirectionDims);
      const int32_t cellKeys = int32_t(volume.indirectionCellKeys);
      for (int32_t z = 0; z < dims.z; z++)
      {
        for (int32_t y = 0; y < dims.y; y++)
        {
          for (int32_t x = 0; x < dims.x; x++)
          {
            size_t cellIndex = size_t(x) + size_t(y) * size_t(dims.x) + size_t(z) * size_t(dims.x) * size_t(dims.y);
            uint32_t brickIndex = volume.indirection[cellIndex];
            if (brickIndex == IRRADIANCE_BRICK_INVALID)
              continue;

            const IrradianceBrick& brick = volume.bricks[brickIndex];
            const int32_t ratio = 1 << (brick.spacingIndex - volume.minSpacingIndex);
            const glm::ivec3 cell(x, y, z);
            const glm::ivec3 expectedOrigin = volume.indirectionOriginKey + (cell / ratio) * ratio * cellKeys;
            if (brick.originKey != expectedOrigin)
            {
              return Reject(outFailure, "cell (%d, %d, %d) names brick %u at key (%d, %d, %d), expected (%d, %d, %d)",
                x, y, z, brickIndex, brick.originKey.x, brick.originKey.y, brick.originKey.z,
                expectedOrigin.x, expectedOrigin.y, expectedOrigin.z);
            }
          }
        }
      }
      return true;
    }

    // Host visible, so the images are filled straight from it: one copy and one submit, where a
    // staged buffer would first be copied to device memory in a submit of its own.
    VulkanBuffer CreateUploadBuffer(const RenderContext& ctx, const void* data, size_t size)
    {
      VulkanBuffer buffer = VulkanBuffer::CreateMapped(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      std::memcpy(buffer.GetMapped(), data, size);
      return buffer;
    }

    void CopyToShaderReadImage(VkCommandBuffer cmd, VulkanImage& image, VkBuffer staging, const glm::uvec3& size)
    {
      VkBufferImageCopy region {};
      region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      region.imageExtent = { size.x, size.y, size.z };

      TransitionImageLayout(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdCopyBufferToImage(cmd, staging, image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
      TransitionImageLayout(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      image.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }

  void IrradianceVolumeStorage::Init(const RenderContext& ctx)
  {
    m_UniformBuffers.resize(ctx.maxFramesInFlight);
    for (size_t i = 0; i < ctx.maxFramesInFlight; i++)
    {
      m_UniformBuffers[i].Create(ctx, sizeof(IrradianceVolumeBuffer));
      YA_DEBUG_NAMEF(ctx.device, VK_OBJECT_TYPE_BUFFER,
        m_UniformBuffers[i].Get(), "Irradiance Volume UBO %zu", i);
    }

    Reset(ctx);
  }

  void IrradianceVolumeStorage::Destroy(const RenderContext& ctx)
  {
    DestroyImages(ctx);
    for (auto& ubo : m_UniformBuffers)
      ubo.Destroy(ctx);
    m_UniformBuffers.clear();
  }

  void IrradianceVolumeStorage::CreateImages(const RenderContext& ctx, const glm::uvec3& poolSize,
    const glm::uvec3& indirectionSize)
  {
    m_PoolSize = glm::max(poolSize, glm::uvec3(1));
    m_IndirectionSize = glm::max(indirectionSize, glm::uvec3(1));

    SamplerDesc linearSampler {
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .maxLod = 0.0f,
    };

    for (uint32_t channel = 0; channel < 3; channel++)
    {
      ImageDesc desc {
        .width = m_PoolSize.x,
        .height = m_PoolSize.y,
        .depth = m_PoolSize.z,
        .format = COEFFICIENT_FORMAT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageType = VK_IMAGE_TYPE_3D,
        .viewType = VK_IMAGE_VIEW_TYPE_3D,
      };
      m_Coefficients[channel].Init(ctx, desc, &linearSampler);
    }

    // Not read by the shader - it exists for the editor and as the input a
    // future manual 8-tap sampler would weight its taps by.
    ImageDesc validityDesc {
      .width = m_PoolSize.x,
      .height = m_PoolSize.y,
      .depth = m_PoolSize.z,
      .format = VALIDITY_FORMAT,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .imageType = VK_IMAGE_TYPE_3D,
      .viewType = VK_IMAGE_VIEW_TYPE_3D,
    };
    m_Validity.Init(ctx, validityDesc, &linearSampler);

    // Read with texelFetch only; an unsigned integer format may not be linearly filtered.
    SamplerDesc nearestSampler {
      .magFilter = VK_FILTER_NEAREST,
      .minFilter = VK_FILTER_NEAREST,
      .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .maxLod = 0.0f,
    };

    ImageDesc indirectionDesc {
      .width = m_IndirectionSize.x,
      .height = m_IndirectionSize.y,
      .depth = m_IndirectionSize.z,
      .format = INDIRECTION_FORMAT,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .imageType = VK_IMAGE_TYPE_3D,
      .viewType = VK_IMAGE_VIEW_TYPE_3D,
    };
    m_Indirection.Init(ctx, indirectionDesc, &nearestSampler);
  }

  void IrradianceVolumeStorage::DestroyImages(const RenderContext& ctx)
  {
    for (auto& image : m_Coefficients)
      image.Destroy(ctx);
    m_Validity.Destroy(ctx);
    m_Indirection.Destroy(ctx);
  }

  void IrradianceVolumeStorage::Reset(const RenderContext& ctx)
  {
    // Descriptors may still reference the old views from frames in flight
    vkDeviceWaitIdle(ctx.device);

    m_Generation++;
    DestroyImages(ctx);
    CreateImages(ctx, glm::uvec3(1), glm::uvec3(1));

    m_BufferData = {};
    m_BufferData.poolInvSize = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    m_BufferData.volumeCount = 0;
    m_BufferData.poolSlotsX = 1;
    m_BufferData.poolSlotsY = 1;

    // Dummy textures still have to be readable - a descriptor pointing at an image
    // in UNDEFINED layout is a validation error even when volumeCount is zero.
    std::array<uint8_t, COEFFICIENT_TEXEL_SIZE> zeros {};
    VulkanBuffer zeroStaging = CreateUploadBuffer(ctx, zeros.data(), zeros.size());
    const uint32_t invalidCell = IRRADIANCE_INDIRECTION_INVALID;
    VulkanBuffer indirectionStaging = CreateUploadBuffer(ctx, &invalidCell, sizeof(invalidCell));

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();
    for (auto& image : m_Coefficients)
      CopyToShaderReadImage(cmd, image, zeroStaging.Get(), glm::uvec3(1));
    CopyToShaderReadImage(cmd, m_Validity, zeroStaging.Get(), glm::uvec3(1));
    CopyToShaderReadImage(cmd, m_Indirection, indirectionStaging.Get(), glm::uvec3(1));
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    zeroStaging.Destroy(ctx);
    indirectionStaging.Destroy(ctx);
  }

  void IrradianceVolumeStorage::Upload(const RenderContext& ctx,
    const std::vector<IrradianceVolumeFileData>& volumes, std::vector<uint32_t>& outSlots)
  {
    m_Generation++;
    outSlots.assign(volumes.size(), INVALID_SLOT);

    std::vector<uint32_t> order;
    order.reserve(volumes.size());
    for (uint32_t index = 0; index < uint32_t(volumes.size()); index++)
    {
      // Public API: the blobs cannot be assumed to match their counts, and the packing below
      // indexes them by those counts. The geometry check relies on the bounds check before it.
      std::string failure;
      if (!IrradianceVolumeFile::Validate(volumes[index], failure)
        || !ValidateBoxAndBounds(volumes[index], ctx.maxImageDimension3D, failure)
        || !ValidateIndirectionGeometry(volumes[index], failure))
      {
        YA_LOG_WARN("Render", "Irradiance volume %u is inconsistent (%s) - skipped", index, failure.c_str());
        continue;
      }
      order.push_back(index);
    }

    // Ascending box volume: the shader takes the first volume that contains the
    // point, so the smallest one has to come first for nesting to work.
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b)
    {
      const glm::vec3& ha = volumes[a].halfExtents;
      const glm::vec3& hb = volumes[b].halfExtents;
      return (ha.x * ha.y * ha.z) < (hb.x * hb.y * hb.z);
    });

    // A volume that would push the pool or the indirection atlas past the device limit or the
    // atlas budget is skipped here, before any image is released, rather than allowed to fail an
    // allocation in the middle of a scene load. A skipped volume takes no MAX_IRRADIANCE_VOLUMES
    // place, so the next one can still have it.
    const uint32_t maxDimension = ctx.maxImageDimension3D;
    const uint64_t maxSlotsPerAxis = maxDimension / BRICK_TEXELS;
    const uint64_t slotCapacity = std::min(maxSlotsPerAxis * maxSlotsPerAxis * maxSlotsPerAxis,
      uint64_t(IRRADIANCE_POOL_SLOT_MASK) + 1);

    // Every packing axis is followed at once and the atlas takes the one needing the fewest texels
    // among those every packed volume fits, so volumes flat along the same axis stack without
    // padding each other out.
    std::array<AtlasPacking, 3> packings;
    for (int32_t axis = 0; axis < 3; axis++)
      packings[axis].axis = axis;
    uint32_t atlasAxes = 0b111;

    uint64_t slotCount = 0;
    uint32_t beyondVolumeLimit = 0;
    std::vector<uint32_t> packed;
    std::vector<uint32_t> slotBases;
    for (uint32_t index : order)
    {
      if (packed.size() == MAX_IRRADIANCE_VOLUMES)
      {
        beyondVolumeLimit++;
        continue;
      }

      const auto& volume = volumes[index];
      const uint64_t nextSlots = slotCount + volume.bricks.size();

      std::array<std::array<uint64_t, 3>, 3> grownSizes;
      uint32_t fittingAxes = 0;
      for (int32_t axis = 0; axis < 3; axis++)
      {
        grownSizes[axis] = GetGrownAtlasSize(packings[axis], volume.indirectionDims);
        const std::array<uint64_t, 3>& size = grownSizes[axis];
        if (size[0] <= maxDimension && size[1] <= maxDimension && size[2] <= maxDimension
          && GetAtlasTexels(size) <= MAX_INDIRECTION_ATLAS_TEXELS)
        {
          fittingAxes |= 1u << axis;
        }
      }
      fittingAxes &= atlasAxes;

      if (nextSlots > slotCapacity || fittingAxes == 0)
      {
        YA_LOG_WARN("Render", "Irradiance volume %u (%zu bricks, %ux%ux%u cells) does not fit next to the smaller volumes: the brick pool holds %llu slots, the indirection atlas %u texels per axis and %llu in total - skipped",
          index, volume.bricks.size(), volume.indirectionDims.x, volume.indirectionDims.y,
          volume.indirectionDims.z, (unsigned long long)slotCapacity, maxDimension,
          (unsigned long long)MAX_INDIRECTION_ATLAS_TEXELS);
        continue;
      }

      // Axes the volume does not fit along are dropped for good, so their sizes are never read again.
      for (int32_t axis = 0; axis < 3; axis++)
      {
        packings[axis].offsets.push_back(uint32_t(packings[axis].size[axis]));
        packings[axis].size = grownSizes[axis];
      }
      atlasAxes = fittingAxes;

      slotBases.push_back(uint32_t(slotCount));
      slotCount = nextSlots;
      packed.push_back(index);
    }

    if (beyondVolumeLimit > 0)
    {
      YA_LOG_WARN("Render", "Scene has more irradiance volumes than fit (MAX_IRRADIANCE_VOLUMES = %d); the %u largest skipped",
        MAX_IRRADIANCE_VOLUMES, beyondVolumeLimit);
    }

    if (packed.empty())
    {
      Reset(ctx);
      return;
    }

    const glm::uvec3 slotGrid = ComputeSlotGrid(slotCount);
    if (glm::any(glm::greaterThan(slotGrid * BRICK_TEXELS, glm::uvec3(maxDimension))))
    {
      YA_LOG_ERROR("Render", "Irradiance brick pool of %ux%ux%u slots exceeds the device limit of %u texels per axis - no volume uploaded",
        slotGrid.x, slotGrid.y, slotGrid.z, maxDimension);
      Reset(ctx);
      return;
    }

    const AtlasPacking* atlas = nullptr;
    for (const AtlasPacking& packing : packings)
    {
      if ((atlasAxes & (1u << packing.axis)) != 0 && (atlas == nullptr || GetAtlasTexels(packing.size) < GetAtlasTexels(atlas->size)))
        atlas = &packing;
    }
    const glm::uvec3 indirectionSize(uint32_t(atlas->size[0]), uint32_t(atlas->size[1]), uint32_t(atlas->size[2]));
    const glm::uvec3 poolSize = slotGrid * BRICK_TEXELS;

    const size_t poolRow = poolSize.x;
    const size_t poolPlane = size_t(poolSize.x) * poolSize.y;
    const size_t poolTexels = poolPlane * poolSize.z;
    const size_t atlasRow = indirectionSize.x;
    const size_t atlasPlane = size_t(indirectionSize.x) * indirectionSize.y;
    const size_t atlasTexels = atlasPlane * indirectionSize.z;

    // Allocated and filled while the previous set is still bound, and released by scope on every path.
    struct UploadBuffers
    {
      const RenderContext& ctx;
      std::array<VulkanBuffer, 5> buffers {};
      ~UploadBuffers()
      {
        for (VulkanBuffer& buffer : buffers)
          buffer.Destroy(ctx);
      }
    } upload { ctx };

    try
    {
      for (uint32_t channel = 0; channel < 3; channel++)
        upload.buffers[channel] = VulkanBuffer::CreateMapped(ctx, poolTexels * COEFFICIENT_TEXEL_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      upload.buffers[3] = VulkanBuffer::CreateMapped(ctx, poolTexels, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      upload.buffers[4] = VulkanBuffer::CreateMapped(ctx, atlasTexels * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    }
    catch (const std::exception& e)
    {
      // Reset rather than keep the previous set: every slot reported below is INVALID_SLOT, and the
      // bound images have to agree with that.
      YA_LOG_ERROR("Render", "Irradiance volume upload buffers for a %ux%ux%u texel pool and a %ux%ux%u indirection atlas could not be allocated (%s) - no volume uploaded",
        poolSize.x, poolSize.y, poolSize.z, indirectionSize.x, indirectionSize.y, indirectionSize.z, e.what());
      Reset(ctx);
      return;
    }

    std::array<uint16_t*, 3> coefficientTexels {};
    for (uint32_t channel = 0; channel < 3; channel++)
      coefficientTexels[channel] = static_cast<uint16_t*>(upload.buffers[channel].GetMapped());
    uint8_t* validityTexels = static_cast<uint8_t*>(upload.buffers[3].GetMapped());
    uint32_t* indirectionTexels = static_cast<uint32_t*>(upload.buffers[4].GetMapped());

    const auto getSlotTexelOrigin = [&slotGrid](uint64_t slot)
    {
      return glm::uvec3(uint32_t(slot % slotGrid.x), uint32_t((slot / slotGrid.x) % slotGrid.y),
        uint32_t(slot / (uint64_t(slotGrid.x) * slotGrid.y))) * BRICK_TEXELS;
    };

    const auto writeSlotTexel = [&](const glm::uvec3& texelOrigin, uint32_t x, uint32_t y, uint32_t z,
      const uint16_t* halves, uint8_t valid)
    {
      const size_t texel = size_t(texelOrigin.x + x) + size_t(texelOrigin.y + y) * poolRow
        + size_t(texelOrigin.z + z) * poolPlane;
      for (uint32_t channel = 0; channel < 3; channel++)
        std::copy_n(halves + channel * 4, 4, coefficientTexels[channel] + texel * 4);
      validityTexels[texel] = valid;
    };

    // Unused slots and cells are filled too, so a readback never shows garbage. Bricks write every
    // texel of their slots below, so only the slots past the last brick are zeroed here.
    const SHL1RGBHalf zeroHalves {};
    const uint64_t gridSlots = uint64_t(slotGrid.x) * slotGrid.y * slotGrid.z;
    for (uint64_t slot = slotCount; slot < gridSlots; slot++)
    {
      const glm::uvec3 texelOrigin = getSlotTexelOrigin(slot);
      for (uint32_t z = 0; z < BRICK_TEXELS; z++)
        for (uint32_t y = 0; y < BRICK_TEXELS; y++)
          for (uint32_t x = 0; x < BRICK_TEXELS; x++)
            writeSlotTexel(texelOrigin, x, y, z, zeroHalves.halves.data(), 0);
    }
    std::fill_n(indirectionTexels, atlasTexels, IRRADIANCE_INDIRECTION_INVALID);

    IrradianceVolumeBuffer bufferData {};
    for (size_t i = 0; i < packed.size(); i++)
    {
      const auto& volume = volumes[packed[i]];

      for (uint32_t b = 0; b < uint32_t(volume.bricks.size()); b++)
      {
        const glm::uvec3 texelOrigin = getSlotTexelOrigin(uint64_t(slotBases[i]) + b);
        const size_t brickOffset = size_t(b) * IRRADIANCE_BRICK_NODE_COUNT;

        for (uint32_t z = 0; z < BRICK_TEXELS; z++)
        {
          for (uint32_t y = 0; y < BRICK_TEXELS; y++)
          {
            for (uint32_t x = 0; x < BRICK_TEXELS; x++)
            {
              const uint32_t node = volume.brickNodeIndices[brickOffset + x + BRICK_TEXELS * (y + BRICK_TEXELS * z)];
              writeSlotTexel(texelOrigin, x, y, z, volume.coefficients[node].halves.data(), volume.validity[node] ? 255 : 0);
            }
          }
        }
      }

      glm::uvec3 atlasOffset(0);
      atlasOffset[atlas->axis] = atlas->offsets[i];

      const glm::uvec3 dims = volume.indirectionDims;
      for (uint32_t z = 0; z < dims.z; z++)
      {
        for (uint32_t y = 0; y < dims.y; y++)
        {
          for (uint32_t x = 0; x < dims.x; x++)
          {
            const uint32_t brickIndex = volume.indirection[size_t(x) + size_t(y) * dims.x + size_t(z) * dims.x * dims.y];
            if (brickIndex == IRRADIANCE_BRICK_INVALID)
              continue;

            indirectionTexels[size_t(atlasOffset.x + x) + size_t(atlasOffset.y + y) * atlasRow + size_t(atlasOffset.z + z) * atlasPlane] =
              (slotBases[i] + brickIndex) | (volume.bricks[brickIndex].spacingIndex << IRRADIANCE_POOL_SPACING_SHIFT);
          }
        }
      }

      // Box transform and bricks both come from the ASSET, not from the entity -
      // the baked data is only valid for the box it was captured in.
      glm::mat4 volumeToWorld = glm::translate(glm::mat4(1.0f), volume.position)
        * glm::mat4_cast(volume.rotation);

      auto& info = bufferData.volumes[i];
      info.worldToLocal = glm::inverse(volumeToWorld);
      info.halfExtentsFade = glm::vec4(volume.halfExtents, volume.edgeFade);
      info.indirectionOrigin = glm::vec4(GetIrradianceKeyWorldPosition(volume.indirectionOriginKey),
        float(volume.indirectionCellKeys) * IRRADIANCE_SPACINGS[0]);
      info.indirectionAtlas = glm::ivec4(glm::ivec3(atlasOffset), 0);
      info.indirectionDims = glm::ivec4(glm::ivec3(dims), int32_t(volume.minSpacingIndex));
    }

    bufferData.poolInvSize = glm::vec4(glm::vec3(1.0f) / glm::vec3(poolSize), 0.0f);
    bufferData.volumeCount = int(packed.size());
    bufferData.poolSlotsX = int(slotGrid.x);
    bufferData.poolSlotsY = int(slotGrid.y);

    // Descriptors may still reference the old views from frames in flight.
    vkDeviceWaitIdle(ctx.device);
    try
    {
      DestroyImages(ctx);
      CreateImages(ctx, poolSize, indirectionSize);

      VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();
      for (uint32_t channel = 0; channel < 3; channel++)
        CopyToShaderReadImage(cmd, m_Coefficients[channel], upload.buffers[channel].Get(), poolSize);
      CopyToShaderReadImage(cmd, m_Validity, upload.buffers[3].Get(), poolSize);
      CopyToShaderReadImage(cmd, m_Indirection, upload.buffers[4].Get(), indirectionSize);
      ctx.commandBuffer->EndSingleTimeCommands(cmd);
    }
    catch (const std::exception& e)
    {
      YA_LOG_ERROR("Render", "Irradiance volume images (%ux%ux%u texel pool, %ux%ux%u indirection atlas) could not be created or filled (%s) - no volume uploaded",
        poolSize.x, poolSize.y, poolSize.z, indirectionSize.x, indirectionSize.y, indirectionSize.z, e.what());
      Reset(ctx);
      return;
    }

    m_BufferData = bufferData;
    for (size_t i = 0; i < packed.size(); i++)
      outSlots[packed[i]] = uint32_t(i);

    YA_LOG_INFO("Render", "Irradiance brick pool %ux%ux%u texels (%llu of %u slots), indirection atlas %ux%ux%u packed along %c, %d volumes",
      m_PoolSize.x, m_PoolSize.y, m_PoolSize.z, (unsigned long long)slotCount, slotGrid.x * slotGrid.y * slotGrid.z,
      m_IndirectionSize.x, m_IndirectionSize.y, m_IndirectionSize.z, "xyz"[atlas->axis], m_BufferData.volumeCount);
  }

  void IrradianceVolumeStorage::SetUp(uint32_t frameIndex, const IrradianceVolumeBuffer& data)
  {
    m_UniformBuffers[frameIndex].Update(data);
  }
}
