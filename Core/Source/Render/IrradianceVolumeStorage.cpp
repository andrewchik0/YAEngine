#include "IrradianceVolumeStorage.h"

#include <glm/gtc/packing.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "VulkanBuffer.h"
#include "ImageBarrier.h"
#include "DebugMarker.h"
#include "Utils/Log.h"

namespace YAEngine
{
  // std140: mat4 + four vec4, everything already 16-aligned, no implicit padding
  static_assert(sizeof(IrradianceVolumeInfo) == 128,
    "IrradianceVolumeInfo must match its std140 layout");
  static_assert(sizeof(IrradianceVolumeBuffer) == 32 + 128 * MAX_IRRADIANCE_VOLUMES,
    "IrradianceVolumeBuffer must match its std140 layout");

  namespace
  {
    constexpr VkFormat COEFFICIENT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr VkFormat VALIDITY_FORMAT = VK_FORMAT_R8_UNORM;

    // Four halfs per texel: (L0, L1x, L1y, L1z) of one color channel
    constexpr size_t COEFFICIENT_TEXEL_SIZE = 4 * sizeof(uint16_t);

    const SHL1Channel& ChannelOf(const SHL1RGB& sh, uint32_t channel)
    {
      return channel == 0 ? sh.r : (channel == 1 ? sh.g : sh.b);
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

  void IrradianceVolumeStorage::CreateImages(const RenderContext& ctx, const glm::uvec3& size)
  {
    m_AtlasSize = glm::max(size, glm::uvec3(1));

    SamplerDesc sampler {
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .maxLod = 0.0f,
    };

    for (uint32_t channel = 0; channel < 3; channel++)
    {
      ImageDesc desc {
        .width = m_AtlasSize.x,
        .height = m_AtlasSize.y,
        .depth = m_AtlasSize.z,
        .format = COEFFICIENT_FORMAT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageType = VK_IMAGE_TYPE_3D,
        .viewType = VK_IMAGE_VIEW_TYPE_3D,
      };
      m_Coefficients[channel].Init(ctx, desc, &sampler);
    }

    // Not read by the shader in v1 - it exists for the editor and as the input a
    // future manual 8-tap sampler would weight its taps by.
    ImageDesc validityDesc {
      .width = m_AtlasSize.x,
      .height = m_AtlasSize.y,
      .depth = m_AtlasSize.z,
      .format = VALIDITY_FORMAT,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .imageType = VK_IMAGE_TYPE_3D,
      .viewType = VK_IMAGE_VIEW_TYPE_3D,
    };
    m_Validity.Init(ctx, validityDesc, &sampler);
  }

  void IrradianceVolumeStorage::DestroyImages(const RenderContext& ctx)
  {
    for (auto& image : m_Coefficients)
      image.Destroy(ctx);
    m_Validity.Destroy(ctx);
  }

  void IrradianceVolumeStorage::Reset(const RenderContext& ctx)
  {
    // Descriptors may still reference the old views from frames in flight
    vkDeviceWaitIdle(ctx.device);

    DestroyImages(ctx);
    CreateImages(ctx, glm::uvec3(1));

    m_BufferData = {};
    m_BufferData.atlasInvSize = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    m_BufferData.volumeCount = 0;

    // Dummy textures still have to be readable - a descriptor pointing at an image
    // in UNDEFINED layout is a validation error even when volumeCount is zero.
    std::vector<uint8_t> zeros(std::max(COEFFICIENT_TEXEL_SIZE, size_t(1)), 0);
    auto staging = VulkanBuffer::CreateStaged(ctx, zeros.data(), zeros.size(),
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    VkBufferImageCopy region {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { 1, 1, 1 };

    for (auto& image : m_Coefficients)
    {
      TransitionImageLayout(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdCopyBufferToImage(cmd, staging.Get(), image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
      TransitionImageLayout(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      image.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    TransitionImageLayout(cmd, m_Validity.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(cmd, staging.Get(), m_Validity.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    TransitionImageLayout(cmd, m_Validity.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_Validity.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);
    staging.Destroy(ctx);
  }

  void IrradianceVolumeStorage::Upload(const RenderContext& ctx,
    const std::vector<IrradianceVolumeFileData>& volumes, std::vector<uint32_t>& outSlots)
  {
    outSlots.assign(volumes.size(), INVALID_SLOT);

    if (volumes.empty())
    {
      Reset(ctx);
      return;
    }

    std::vector<uint32_t> order(volumes.size());
    for (uint32_t i = 0; i < uint32_t(volumes.size()); i++)
      order[i] = i;

    // This is public API, so the blobs cannot be assumed to match the grid the
    // header claims - the packing loop below indexes them by nodesX/Y/Z.
    std::erase_if(order, [&](uint32_t index)
    {
      const auto& volume = volumes[index];
      uint64_t nodes = uint64_t(volume.nodesX) * volume.nodesY * volume.nodesZ;
      if (nodes != 0 && volume.coefficients.size() == nodes && volume.validity.size() == nodes)
        return false;

      YA_LOG_WARN("Render", "Irradiance volume %u claims %ux%ux%u nodes but holds %zu coefficients and %zu validity flags - skipped",
        index, volume.nodesX, volume.nodesY, volume.nodesZ,
        volume.coefficients.size(), volume.validity.size());
      return true;
    });

    if (order.empty())
    {
      Reset(ctx);
      return;
    }

    // Ascending box volume: the shader takes the first volume that contains the
    // point, so the smallest one has to come first for nesting to work.
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b)
    {
      const glm::vec3& ha = volumes[a].halfExtents;
      const glm::vec3& hb = volumes[b].halfExtents;
      return (ha.x * ha.y * ha.z) < (hb.x * hb.y * hb.z);
    });

    if (order.size() > MAX_IRRADIANCE_VOLUMES)
    {
      YA_LOG_WARN("Render", "Scene has %u irradiance volumes, only %d fit (MAX_IRRADIANCE_VOLUMES); %u skipped",
        uint32_t(order.size()), MAX_IRRADIANCE_VOLUMES,
        uint32_t(order.size()) - MAX_IRRADIANCE_VOLUMES);
      order.resize(MAX_IRRADIANCE_VOLUMES);
    }

    // Pack along X, share Y and Z. Sub-boxes are separated by an unused column.
    // A volume that would push any axis past the device limit is skipped rather
    // than allowed to fail vkCreateImage in the middle of a scene load.
    glm::uvec3 atlasSize { 0, 1, 1 };
    std::vector<uint32_t> originX;
    std::vector<uint32_t> packed;
    originX.reserve(order.size());
    packed.reserve(order.size());
    for (uint32_t index : order)
    {
      const auto& volume = volumes[index];
      uint32_t gap = packed.empty() ? 0u : VOLUME_GAP_TEXELS;
      uint32_t nextX = atlasSize.x + gap + volume.nodesX;
      uint32_t nextY = std::max(atlasSize.y, volume.nodesY);
      uint32_t nextZ = std::max(atlasSize.z, volume.nodesZ);

      if (nextX > ctx.maxImageDimension3D || nextY > ctx.maxImageDimension3D
        || nextZ > ctx.maxImageDimension3D)
      {
        YA_LOG_WARN("Render", "Irradiance volume %u (%ux%ux%u nodes) does not fit the atlas limit of %u texels per axis - skipped",
          index, volume.nodesX, volume.nodesY, volume.nodesZ, ctx.maxImageDimension3D);
        continue;
      }

      originX.push_back(atlasSize.x + gap);
      atlasSize = { nextX, nextY, nextZ };
      packed.push_back(index);
    }

    order = std::move(packed);
    if (order.empty())
    {
      Reset(ctx);
      return;
    }

    vkDeviceWaitIdle(ctx.device);
    DestroyImages(ctx);
    CreateImages(ctx, atlasSize);

    size_t texelCount = size_t(m_AtlasSize.x) * m_AtlasSize.y * m_AtlasSize.z;

    // Zero-filled so the gap columns and the unused top of shorter volumes never
    // hold garbage - they are outside every sampling range, but a readback or the
    // editor visualization would otherwise show noise.
    std::array<std::vector<uint16_t>, 3> coefficientData;
    for (auto& channel : coefficientData)
      channel.assign(texelCount * 4, 0);
    std::vector<uint8_t> validityData(texelCount, 0);

    for (size_t i = 0; i < order.size(); i++)
    {
      const auto& volume = volumes[order[i]];
      outSlots[order[i]] = uint32_t(i);

      for (uint32_t z = 0; z < volume.nodesZ; z++)
      {
        for (uint32_t y = 0; y < volume.nodesY; y++)
        {
          for (uint32_t x = 0; x < volume.nodesX; x++)
          {
            uint32_t srcIndex = x + y * volume.nodesX + z * volume.nodesX * volume.nodesY;
            size_t dstIndex = size_t(originX[i] + x)
              + size_t(y) * m_AtlasSize.x
              + size_t(z) * m_AtlasSize.x * m_AtlasSize.y;

            const SHL1RGB& sh = volume.coefficients[srcIndex];
            for (uint32_t channel = 0; channel < 3; channel++)
            {
              const SHL1Channel& c = ChannelOf(sh, channel);
              uint16_t* dst = &coefficientData[channel][dstIndex * 4];
              dst[0] = glm::packHalf1x16(c.l0);
              dst[1] = glm::packHalf1x16(c.l1x);
              dst[2] = glm::packHalf1x16(c.l1y);
              dst[3] = glm::packHalf1x16(c.l1z);
            }

            validityData[dstIndex] = volume.validity[srcIndex] ? 255 : 0;
          }
        }
      }

      // Box transform and lattice both come from the ASSET, not from the entity -
      // the baked data is only valid for the box it was captured in.
      glm::mat4 volumeToWorld = glm::translate(glm::mat4(1.0f), volume.position)
        * glm::mat4_cast(volume.rotation);

      auto& info = m_BufferData.volumes[i];
      info.worldToLocal = glm::inverse(volumeToWorld);
      info.halfExtentsFade = glm::vec4(volume.halfExtents, 0.5f * volume.spacing);
      info.atlasOrigin = glm::vec4(float(originX[i]), 0.0f, 0.0f, 0.0f);
      info.gridSize = glm::vec4(float(volume.nodesX), float(volume.nodesY), float(volume.nodesZ), 0.0f);
      info.latticeOrigin = glm::vec4(volume.latticeOrigin, volume.spacing);
    }

    m_BufferData.atlasInvSize = glm::vec4(
      1.0f / float(m_AtlasSize.x),
      1.0f / float(m_AtlasSize.y),
      1.0f / float(m_AtlasSize.z),
      0.0f);
    m_BufferData.volumeCount = int(order.size());

    // All four staging buffers are created BEFORE the command buffer is opened:
    // CreateStaged runs its own BeginSingleTimeCommands and blocks on the shared
    // fence, so creating them inside an open recording interleaves four GPU stalls
    // into it. Reset() already had this order.
    std::array<VulkanBuffer, 4> staging;
    for (uint32_t channel = 0; channel < 3; channel++)
    {
      staging[channel] = VulkanBuffer::CreateStaged(ctx, coefficientData[channel].data(),
        coefficientData[channel].size() * sizeof(uint16_t), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    }
    staging[3] = VulkanBuffer::CreateStaged(ctx, validityData.data(), validityData.size(),
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    VkBufferImageCopy region {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { m_AtlasSize.x, m_AtlasSize.y, m_AtlasSize.z };

    for (uint32_t channel = 0; channel < 3; channel++)
    {
      TransitionImageLayout(cmd, m_Coefficients[channel].GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdCopyBufferToImage(cmd, staging[channel].Get(), m_Coefficients[channel].GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
      TransitionImageLayout(cmd, m_Coefficients[channel].GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      m_Coefficients[channel].SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    TransitionImageLayout(cmd, m_Validity.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(cmd, staging[3].Get(), m_Validity.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    TransitionImageLayout(cmd, m_Validity.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_Validity.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    for (auto& buffer : staging)
      buffer.Destroy(ctx);

    YA_LOG_INFO("Render", "Irradiance volume atlas %ux%ux%u texels, %d volumes",
      m_AtlasSize.x, m_AtlasSize.y, m_AtlasSize.z, m_BufferData.volumeCount);
  }

  void IrradianceVolumeStorage::SetUp(uint32_t frameIndex, const IrradianceVolumeBuffer& data)
  {
    m_UniformBuffers[frameIndex].Update(data);
  }
}
