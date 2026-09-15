#include "VulkanAccelerationStructure.h"

#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "Utils/Log.h"

namespace YAEngine
{
  AccelerationStructureScratch AccelerationStructureScratch::Create(const RenderContext& ctx, VkDeviceSize size)
  {
    const VkDeviceSize alignment = std::max<VkDeviceSize>(
      ctx.accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment, 1);

    AccelerationStructureScratch scratch;
    // One alignment of slack, so rounding the address up can never walk off the end.
    scratch.buffer = VulkanBuffer::CreateGpuOnly(ctx, size + alignment,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    const VkDeviceAddress address = scratch.buffer.GetDeviceAddress(ctx);
    scratch.deviceAddress = (address + alignment - 1) / alignment * alignment;

    return scratch;
  }

  namespace
  {
    // The geometry half of a top level build. The sizes query and the build itself must
    // describe the same geometry for the queried size to be the one the build needs, so
    // both go through this. A sizes query never dereferences the address and passes 0.
    VkAccelerationStructureGeometryKHR MakeInstanceGeometry(VkDeviceAddress instanceAddress)
    {
      return VkAccelerationStructureGeometryKHR {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry = {
          .instances = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers = VK_FALSE,
            .data = { .deviceAddress = instanceAddress },
          },
        },
      };
    }

    VkAccelerationStructureBuildGeometryInfoKHR MakeTopLevelBuildInfo(
      const VkAccelerationStructureGeometryKHR* geometry)
    {
      return VkAccelerationStructureBuildGeometryInfoKHR {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        // The structure is rebuilt whole every frame, so build cost is what matters and
        // FAST_TRACE would be paid on every one of them. A digest-keyed refit would go
        // here: ALLOW_UPDATE plus MODE_UPDATE on frames the snapshot's transform digest
        // says nothing moved.
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = geometry,
      };
    }
  }

  bool VulkanAccelerationStructure::Create(const RenderContext& ctx,
    VkAccelerationStructureTypeKHR type, VkDeviceSize size)
  {
    m_Buffer = VulkanBuffer::CreateGpuOnly(ctx, size,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    VkAccelerationStructureCreateInfoKHR createInfo {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
      .buffer = m_Buffer.Get(),
      .offset = 0,
      .size = size,
      .type = type,
    };

    VkResult result = ctx.rayTracing.createAccelerationStructure(ctx.device, &createInfo, nullptr, &m_Handle);
    if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Vulkan", "Failed to create acceleration structure: %d", result);
      m_Handle = VK_NULL_HANDLE;
      m_Buffer.Destroy(ctx);
      return false;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
      .accelerationStructure = m_Handle,
    };
    m_DeviceAddress = ctx.rayTracing.getAccelerationStructureDeviceAddress(ctx.device, &addressInfo);

    return true;
  }

  bool VulkanAccelerationStructure::BuildBottomLevel(const RenderContext& ctx,
    const AccelerationStructureGeometry& geometry)
  {
    if (!ctx.rayTracing.IsLoaded())
      return false;

    VkAccelerationStructureGeometryKHR asGeometry {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
      .geometry = {
        .triangles = {
          .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
          .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
          .vertexData = { .deviceAddress = geometry.vertexAddress },
          .vertexStride = geometry.vertexStride,
          .maxVertex = geometry.maxVertex,
          .indexType = VK_INDEX_TYPE_UINT32,
          .indexData = { .deviceAddress = geometry.indexAddress },
        },
      },
      // Deliberately not VK_GEOMETRY_OPAQUE_BIT_KHR. Whether a surface is alpha tested or a
      // dielectric is a property of the material an instance carries, so it belongs to the TLAS
      // instance flags; marking the geometry opaque here would skip the any-hit shader for every
      // instance built from the same mesh. No duplicate any-hit calls: the shadow any-hit
      // multiplies a transmittance in per call, and a triangle reported twice would count twice.
      .flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR,
    };

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
      // Compaction would add ALLOW_COMPACTION here, plus a query pool pass and a copy into
      // a second structure. That is a further submit and fence wait per mesh, so it waits
      // until acceleration structure memory actually shows up in a profile.
      .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
      .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .geometryCount = 1,
      .pGeometries = &asGeometry,
    };

    const uint32_t triangleCount = geometry.indexCount / 3;

    VkAccelerationStructureBuildSizesInfoKHR sizes {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    ctx.rayTracing.getAccelerationStructureBuildSizes(ctx.device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &triangleCount, &sizes);

    if (!Create(ctx, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize))
      return false;

    AccelerationStructureScratch scratch = AccelerationStructureScratch::Create(ctx, sizes.buildScratchSize);

    buildInfo.dstAccelerationStructure = m_Handle;
    buildInfo.scratchData.deviceAddress = scratch.deviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR range { .primitiveCount = triangleCount };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;

    VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

    // The geometry was staged by earlier submits on this queue that were already waited
    // on, which orders them but does not make their writes visible to the build. A
    // barrier's first scope reaches back over those submits, so one here is enough.
    VkMemoryBarrier barrier {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    ctx.rayTracing.cmdBuildAccelerationStructures(cmd, 1, &buildInfo, &ranges);

    // Blocks on the single-time fence, so the scratch is free to go right after.
    ctx.commandBuffer->EndSingleTimeCommands(cmd);

    scratch.Destroy(ctx);

    return true;
  }

  VkAccelerationStructureBuildSizesInfoKHR VulkanAccelerationStructure::GetTopLevelBuildSizes(
    const RenderContext& ctx, uint32_t instanceCount)
  {
    VkAccelerationStructureGeometryKHR geometry = MakeInstanceGeometry(0);
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = MakeTopLevelBuildInfo(&geometry);

    VkAccelerationStructureBuildSizesInfoKHR sizes {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    ctx.rayTracing.getAccelerationStructureBuildSizes(ctx.device,
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &instanceCount, &sizes);

    return sizes;
  }

  bool VulkanAccelerationStructure::CreateTopLevel(const RenderContext& ctx, VkDeviceSize size)
  {
    // Growing a frame slot's structure is the only caller, and the frame that read the
    // one being replaced has already retired behind its fence.
    Destroy(ctx);

    return Create(ctx, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, size);
  }

  void VulkanAccelerationStructure::CmdBuildTopLevel(const RenderContext& ctx, VkCommandBuffer cmd,
    VkDeviceAddress instanceAddress, uint32_t instanceCount, VkDeviceAddress scratchAddress)
  {
    VkAccelerationStructureGeometryKHR geometry = MakeInstanceGeometry(instanceAddress);
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = MakeTopLevelBuildInfo(&geometry);
    buildInfo.dstAccelerationStructure = m_Handle;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR range { .primitiveCount = instanceCount };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;

    ctx.rayTracing.cmdBuildAccelerationStructures(cmd, 1, &buildInfo, &ranges);
  }

  void VulkanAccelerationStructure::Destroy(const RenderContext& ctx)
  {
    // The handle first: it is a view over m_Buffer, and freeing that memory out from
    // under a live structure is a use-after-free.
    if (m_Handle != VK_NULL_HANDLE)
    {
      ctx.rayTracing.destroyAccelerationStructure(ctx.device, m_Handle, nullptr);
      m_Handle = VK_NULL_HANDLE;
    }

    m_Buffer.Destroy(ctx);
    m_DeviceAddress = 0;
  }
}
