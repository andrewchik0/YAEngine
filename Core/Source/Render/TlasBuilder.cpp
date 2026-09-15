#include "TlasBuilder.h"

#include "RenderContext.h"
#include "RenderObject.h"
#include "VulkanVertexBuffer.h"
#include "Assets/MaterialManager.h"
#include "Assets/MeshManager.h"
#include "Utils/Log.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace YAEngine
{
  namespace
  {
    constexpr VkBufferUsageFlags INSTANCE_BUFFER_USAGE =
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

    constexpr VkBufferUsageFlags RECORD_BUFFER_USAGE = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    // What VkAccelerationStructureGeometryInstancesDataKHR::data requires of the address
    // it is handed.
    constexpr VkDeviceAddress INSTANCE_ADDRESS_ALIGNMENT = 16;

    // A TLAS instance carries the top three rows of the world matrix, row-major. GLM is
    // column-major, so this is a transpose of that 3x4 corner and nothing else.
    VkTransformMatrixKHR ToTransformMatrix(const glm::mat4& world)
    {
      VkTransformMatrixKHR result;
      for (uint32_t row = 0; row < 3; row++)
      {
        for (uint32_t column = 0; column < 4; column++)
          result.matrix[row][column] = world[column][row];
      }
      return result;
    }

    // RayTracingInstanceRecord::worldToPrevWorld for one object. Identity for a singular
    // transform rather than the inf or NaN its inverse would put into the motion vectors.
    void WriteWorldToPrevWorld(const RenderObject& object, glm::vec4 rows[3])
    {
      glm::mat4 delta(1.0f);
      if (std::abs(glm::determinant(glm::mat3(object.worldTransform))) > 0.0f)
        delta = object.prevWorldTransform * glm::affineInverse(object.worldTransform);

      for (uint32_t row = 0; row < 3; row++)
        rows[row] = glm::vec4(delta[0][row], delta[1][row], delta[2][row], delta[3][row]);
    }
  }

  void TlasBuilder::Init(const RenderContext& ctx)
  {
    uint32_t slotCount = ctx.maxFramesInFlight;
#ifdef YA_EDITOR
    m_BakeSlot = ctx.maxFramesInFlight;
    slotCount++;
#endif

    if (!ctx.raytracingSupported)
      return;

    m_Slots.resize(slotCount);
    for (FrameSlot& slot : m_Slots)
      EnsureCapacity(ctx, slot, INITIAL_INSTANCE_CAPACITY);
  }

  void TlasBuilder::Destroy(const RenderContext& ctx)
  {
    for (FrameSlot& slot : m_Slots)
    {
      // The structure before its scratch and its build input, matching the order every
      // other teardown here uses: the handle is a view over memory the rest owns.
      slot.structure.Destroy(ctx);
      slot.scratch.Destroy(ctx);
      slot.instances.Destroy(ctx);
      slot.records.Destroy(ctx);
    }

    m_Slots.clear();
  }

  const VulkanVertexBuffer* TlasBuilder::ResolveGeometry(const RenderObject& object,
    MeshManager& meshes, MaterialManager& materials)
  {
    // The snapshot was taken before this frame started and an asset can have been
    // destroyed since, which moves the slot's generation past the handle's.
    if (!meshes.Has(object.mesh) || !materials.Has(object.material))
      return nullptr;

    const VulkanVertexBuffer& vertexBuffer = meshes.GetVertexBuffer(object.mesh);
    if (!vertexBuffer.HasBottomLevel())
      return nullptr;

    return &vertexBuffer;
  }

  uint32_t TlasBuilder::EnsureCapacity(const RenderContext& ctx, FrameSlot& slot, uint64_t required)
  {
    uint32_t capacity = uint32_t(slot.instances.GetSize() / sizeof(VkAccelerationStructureInstanceKHR));

    uint32_t target = std::max(capacity, INITIAL_INSTANCE_CAPACITY);
    while (target < required && target < MAX_INSTANCE_CAPACITY)
      target = std::min(target * 2, MAX_INSTANCE_CAPACITY);

    if (target > capacity)
    {
      // Nothing in flight still reads either buffer - a frame slot's fence has been waited
      // on, and the bake slot is only used on single-time command buffers, which wait for
      // completion - so the old allocations can go now instead of onto a deferred destroy
      // queue. Descriptors naming them are rewritten before their next use.
      slot.instances.Destroy(ctx);
      slot.records.Destroy(ctx);

      slot.instances = VulkanBuffer::CreateMapped(ctx,
        VkDeviceSize(target) * sizeof(VkAccelerationStructureInstanceKHR), INSTANCE_BUFFER_USAGE);
      slot.records = VulkanBuffer::CreateMapped(ctx,
        VkDeviceSize(target) * sizeof(RayTracingInstanceRecord), RECORD_BUFFER_USAGE);
      slot.instanceAddress = slot.instances.GetDeviceAddress(ctx);
      slot.recordBufferChanged = true;
      capacity = target;

      slot.addressUsable = slot.instanceAddress % INSTANCE_ADDRESS_ALIGNMENT == 0;
      if (!slot.addressUsable)
      {
        YA_LOG_ERROR("Vulkan", "TLAS instance buffer is at address %llu, which is not %llu byte aligned",
          (unsigned long long)slot.instanceAddress, (unsigned long long)INSTANCE_ADDRESS_ALIGNMENT);
      }
    }

    if (!slot.addressUsable)
      return 0;

    return capacity;
  }

  void TlasBuilder::Build(const RenderContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
    const SceneSnapshot& snapshot, MeshManager& meshes, MaterialManager& materials)
  {
    if (frameIndex >= m_Slots.size() || !ctx.rayTracing.IsLoaded())
      return;

    FrameSlot& slot = m_Slots[frameIndex];
    // Cleared up front so every early return below leaves the slot marked untraceable,
    // which is what a consumer tests.
    slot.instanceCount = 0;
    slot.built = false;
    slot.recordBufferChanged = false;

    // Pass one only counts, because the buffers have to be grown before anything can be
    // written into them. It resolves exactly what pass two does, through the same helper.
    uint64_t required = 0;
    for (const RenderObject& object : snapshot.objects)
    {
      if (ResolveGeometry(object, meshes, materials) == nullptr)
        continue;

      // Matches DrawMeshes: an unlit mesh draws once at its own transform there even when
      // the asset carries instance matrices, so it is one instance here too. The TLAS
      // must not hold geometry the raster never draws.
      required += (object.instanceData != nullptr && !object.noShading)
        ? object.instanceData->size() : size_t(1);
    }

    if (required == 0)
      return;

    uint32_t capacity = EnsureCapacity(ctx, slot, required);
    if (capacity == 0)
      return;

#ifdef YA_EDITOR
    const bool frameSlot = frameIndex != m_BakeSlot;
#else
    const bool frameSlot = true;
#endif

    if (required > capacity && frameSlot && !b_CapacityWarned)
    {
      b_CapacityWarned = true;
      YA_LOG_WARN("Render",
        "TLAS instance capacity capped at %u while %llu was requested, the excess instances are dropped",
        capacity, (unsigned long long)required);
    }

    auto* instances = static_cast<VkAccelerationStructureInstanceKHR*>(slot.instances.GetMapped());
    auto* records = static_cast<RayTracingInstanceRecord*>(slot.records.GetMapped());

    uint32_t cursor = 0;
    for (const RenderObject& object : snapshot.objects)
    {
      const VulkanVertexBuffer* vertexBuffer = ResolveGeometry(object, meshes, materials);
      if (vertexBuffer == nullptr)
        continue;

      const bool instanced = object.instanceData != nullptr && !object.noShading;
      const uint32_t count = instanced ? uint32_t(object.instanceData->size()) : 1u;
      if (cursor + count > capacity)
        break;

      VkGeometryInstanceFlagsKHR instanceFlags = 0;
      if (object.doubleSided)
        instanceFlags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
      // Alpha-test surfaces are deliberately left non-opaque so a future any-hit shader
      // gets to run the cutout. The bottom level geometry carries no opaque flag for the
      // same reason, which is what leaves the decision to the instance.
      if (!object.isAlphaTest)
        instanceFlags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;

      uint32_t recordFlags = 0;
      if (object.isAlphaTest)   recordFlags |= RT_INSTANCE_ALPHA_TEST;
      if (object.isTransparent) recordFlags |= RT_INSTANCE_TRANSPARENT;
      if (object.isTerrain)     recordFlags |= RT_INSTANCE_TERRAIN;
      if (object.noShading)     recordFlags |= RT_INSTANCE_UNLIT;
      if (object.doubleSided)   recordFlags |= RT_INSTANCE_DOUBLE_SIDED;

      // Every instance of one object shares its geometry and its material, so the record
      // is built once and only the custom index it is stored at moves.
      RayTracingInstanceRecord record {
        .vertexAddress = vertexBuffer->GetVertexAddress(),
        .indexAddress = vertexBuffer->GetIndexAddress(),
        .attributeOffset = uint32_t(vertexBuffer->GetAttribOffset()),
        .materialIndex = object.material.index,
        .flags = recordFlags,
        ._pad0 = 0,
      };
      // The same for every instance too: prevWorld * offset * inverse(world * offset) cancels
      // the static instance offset down to prevWorld * inverse(world).
      WriteWorldToPrevWorld(object, record.worldToPrevWorld);

      const VkDeviceAddress bottomLevel = vertexBuffer->GetBottomLevel().GetDeviceAddress();
      const uint32_t mask = object.isTransparent ? RT_MASK_TRANSPARENT : RT_MASK_OPAQUE;

      for (uint32_t n = 0; n < count; n++)
      {
        // The instance matrix is a mesh-local transform that the object transform is
        // applied on top of: mesh.vert computes pc.world * instances[gl_InstanceIndex],
        // and the indirect shadow path premultiplies dc.worldTransform * instances[n] on
        // the CPU. Same order here, or instanced geometry would land somewhere else than
        // the raster drew it.
        const glm::mat4 world = instanced
          ? object.worldTransform * (*object.instanceData)[n]
          : object.worldTransform;

        // Assembled on the stack and copied in: the instance buffer is write-combined,
        // and filling the packed bit-fields in place would read it back. Field by field
        // rather than with designated initializers, which a bit-field member cannot take
        // a non-constant value through without a narrowing diagnostic.
        VkAccelerationStructureInstanceKHR instance {};
        instance.transform = ToTransformMatrix(world);
        instance.instanceCustomIndex = cursor;
        instance.mask = mask;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = instanceFlags;
        instance.accelerationStructureReference = bottomLevel;

        instances[cursor] = instance;
        records[cursor] = record;
        cursor++;
      }
    }

    slot.instanceCount = cursor;
    if (cursor == 0)
      return;

    // Sized for the whole instance capacity, not for this frame's count, so the structure
    // is recreated exactly when the buffers grow and survives every frame in between.
    if (capacity != slot.sizedForCount)
    {
      const VkAccelerationStructureBuildSizesInfoKHR sizes =
        VulkanAccelerationStructure::GetTopLevelBuildSizes(ctx, capacity);

      slot.sizedForCount = 0;
      if (!slot.structure.CreateTopLevel(ctx, sizes.accelerationStructureSize))
        return;

      slot.scratch.Destroy(ctx);
      slot.scratch = AccelerationStructureScratch::Create(ctx, sizes.buildScratchSize);
      slot.sizedForCount = capacity;
    }

    // Both barriers below have to reach every consumer of the structure and the records, and
    // every one is a shader binding table pipeline in the ray tracing stages. The stage bit is
    // legal here because raytracingSupported is exactly what enabled VK_KHR_ray_tracing_pipeline.
    const VkPipelineStageFlags traceStages = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;

    // The instance array and the records were just written through a host mapping.
    // vkQueueSubmit makes host writes visible on its own, but the two are read at
    // different points - the build reads the instances, a tracing pass reads the
    // records - and one barrier covers both scopes.
    VkMemoryBarrier hostBarrier {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | traceStages,
      0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

    slot.structure.CmdBuildTopLevel(ctx, cmd, slot.instanceAddress, cursor,
      slot.scratch.deviceAddress);

    // The build writes the structure; what traces it is a pass later in the same command
    // buffer, so the write has to be made available to that pass's shader reads.
    VkMemoryBarrier buildBarrier {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      traceStages, 0, 1, &buildBarrier, 0, nullptr, 0, nullptr);

    slot.built = true;

    if (frameSlot && !b_FirstBuildLogged)
    {
      b_FirstBuildLogged = true;
      YA_LOG_INFO("Render",
        "TLAS built: %u instances over %zu objects, %llu record bytes, %llu structure bytes",
        slot.instanceCount, snapshot.objects.size(),
        (unsigned long long)(slot.instanceCount * sizeof(RayTracingInstanceRecord)),
        (unsigned long long)slot.structure.GetSize());
    }
  }

  bool TlasBuilder::IsValid(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() && m_Slots[frameIndex].built;
  }

  VkAccelerationStructureKHR TlasBuilder::Get(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].structure.Get() : VK_NULL_HANDLE;
  }

  VkBuffer TlasBuilder::GetRecordBuffer(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].records.Get() : VK_NULL_HANDLE;
  }

  VkDeviceSize TlasBuilder::GetRecordBufferSize(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].records.GetSize() : 0;
  }

  uint32_t TlasBuilder::GetInstanceCount(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].instanceCount : 0;
  }

  bool TlasBuilder::RecordBufferChanged(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() && m_Slots[frameIndex].recordBufferChanged;
  }
}
