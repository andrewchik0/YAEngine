#include "TlasBuilder.h"

#include "MaterialUniforms.h"
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

    // The same three rows as vec4s, the layout the shared records carry transforms in.
    void WriteRows(const glm::mat4& matrix, glm::vec4 rows[3])
    {
      for (uint32_t row = 0; row < 3; row++)
        rows[row] = glm::vec4(matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]);
    }

    // RayTracingInstanceRecord::worldToPrevWorld for one object. Identity for a singular
    // transform rather than the inf or NaN its inverse would put into the motion vectors, and
    // exactly identity for one that did not move, which is nearly every object of a frame.
    void WriteWorldToPrevWorld(const RenderObject& object, glm::vec4 rows[3])
    {
      glm::mat4 delta(1.0f);
      if (object.prevWorldTransform != object.worldTransform
        && std::abs(glm::determinant(glm::mat3(object.worldTransform))) > 0.0f)
        delta = object.prevWorldTransform * glm::affineInverse(object.worldTransform);

      WriteRows(delta, rows);
    }

    // luminance() of utils.glsl, in float as the shader evaluates it: the membership test compares
    // it against the shader's own cutoff, so a value right at the cutoff has to round alike.
    float Luminance(const glm::vec3& color)
    {
      return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
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
    {
      EnsureCapacity(ctx, slot, INITIAL_INSTANCE_CAPACITY);
      EnsureEmissiveCapacity(ctx, slot, INITIAL_EMISSIVE_CAPACITY);
    }
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
      slot.emissive.Destroy(ctx);
    }

    m_Slots.clear();
  }

  VulkanVertexBuffer* TlasBuilder::ResolveGeometry(const RenderObject& object,
    MeshManager& meshes, MaterialManager& materials)
  {
    // The snapshot was taken before this frame started and an asset can have been
    // destroyed since, which moves the slot's generation past the handle's.
    if (!meshes.Has(object.mesh) || !materials.Has(object.material))
      return nullptr;

    VulkanVertexBuffer& vertexBuffer = meshes.GetVertexBuffer(object.mesh);
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

  void TlasBuilder::EnsureEmissiveCapacity(const RenderContext& ctx, FrameSlot& slot, uint64_t required)
  {
    const VkDeviceSize size = slot.emissive.GetSize();
    uint32_t capacity = size > sizeof(EmissiveLightTableHeader)
      ? uint32_t((size - sizeof(EmissiveLightTableHeader)) / sizeof(EmissiveLightRecord))
      : 0;

    uint32_t target = std::max(capacity, INITIAL_EMISSIVE_CAPACITY);
    while (target < required && target < MAX_INSTANCE_CAPACITY)
      target = std::min(target * 2, MAX_INSTANCE_CAPACITY);

    if (target <= capacity)
      return;

    // Replaced at once for the reason EnsureCapacity gives.
    slot.emissive.Destroy(ctx);
    slot.emissive = VulkanBuffer::CreateMapped(ctx,
      sizeof(EmissiveLightTableHeader) + VkDeviceSize(target) * sizeof(EmissiveLightRecord),
      RECORD_BUFFER_USAGE);

    const EmissiveLightTableHeader emptyHeader {};
    std::memcpy(slot.emissive.GetMapped(), &emptyHeader, sizeof(emptyHeader));
  }

  void TlasBuilder::BuildEmissiveAliasTable()
  {
    // Vose's alias method: every slot is split between itself and at most one other record, so
    // the shader draws an instance with two uniforms whatever the weights are.
    const uint32_t count = uint32_t(m_EmissiveStaging.size());

    double total = 0.0;
    for (double weight : m_EmissiveWeights)
      total += weight;

    m_AliasScaled.resize(count);
    m_AliasSmall.clear();
    m_AliasLarge.clear();

    for (uint32_t i = 0; i < count; i++)
    {
      m_AliasScaled[i] = m_EmissiveWeights[i] * double(count) / total;
      (m_AliasScaled[i] < 1.0 ? m_AliasSmall : m_AliasLarge).push_back(i);
    }

    while (!m_AliasSmall.empty() && !m_AliasLarge.empty())
    {
      const uint32_t underfull = m_AliasSmall.back();
      m_AliasSmall.pop_back();
      const uint32_t overfull = m_AliasLarge.back();
      m_AliasLarge.pop_back();

      m_EmissiveStaging[underfull].aliasThreshold = float(m_AliasScaled[underfull]);
      m_EmissiveStaging[underfull].aliasIndex = overfull;

      m_AliasScaled[overfull] = (m_AliasScaled[overfull] + m_AliasScaled[underfull]) - 1.0;
      (m_AliasScaled[overfull] < 1.0 ? m_AliasSmall : m_AliasLarge).push_back(overfull);
    }

    // Whatever is left sits at one up to rounding and keeps its whole slot.
    for (uint32_t i : m_AliasSmall)
    {
      m_EmissiveStaging[i].aliasThreshold = 1.0f;
      m_EmissiveStaging[i].aliasIndex = i;
    }
    for (uint32_t i : m_AliasLarge)
    {
      m_EmissiveStaging[i].aliasThreshold = 1.0f;
      m_EmissiveStaging[i].aliasIndex = i;
    }

    // The pmf the shader divides by is summed back out of the float thresholds it compares
    // against, so it describes the table as stored rather than the weights it was built from.
    std::fill(m_AliasScaled.begin(), m_AliasScaled.end(), 0.0);
    for (uint32_t i = 0; i < count; i++)
    {
      const EmissiveLightRecord& light = m_EmissiveStaging[i];
      m_AliasScaled[i] += double(light.aliasThreshold);
      if (light.aliasIndex != i)
        m_AliasScaled[light.aliasIndex] += 1.0 - double(light.aliasThreshold);
    }

    for (uint32_t i = 0; i < count; i++)
      m_EmissiveStaging[i].pmf = float(m_AliasScaled[i] / double(count));
  }

  void TlasBuilder::UploadEmissiveTable(const RenderContext& ctx, FrameSlot& slot)
  {
    // Never more entries than instances, and the instance count is capped at the same
    // ceiling the table grows to, so everything staged fits.
    const uint32_t count = uint32_t(m_EmissiveStaging.size());
    if (count == 0)
      return;

    EnsureEmissiveCapacity(ctx, slot, count);
    BuildEmissiveAliasTable();

    const EmissiveLightTableHeader header {
      .count = count,
      ._pad0 = 0,
      ._pad1 = 0,
      ._pad2 = 0,
    };

    auto* mapped = static_cast<uint8_t*>(slot.emissive.GetMapped());
    std::memcpy(mapped, &header, sizeof(header));
    std::memcpy(mapped + sizeof(header), m_EmissiveStaging.data(),
      size_t(count) * sizeof(EmissiveLightRecord));
    slot.emissiveCount = count;
  }

  void TlasBuilder::Build(const RenderContext& ctx, VkCommandBuffer cmd, uint32_t frameIndex,
    const SceneSnapshot& snapshot, MeshManager& meshes, MaterialManager& materials,
    bool glassEnabled)
  {
    if (frameIndex >= m_Slots.size() || !ctx.rayTracing.IsLoaded())
      return;

    FrameSlot& slot = m_Slots[frameIndex];
    // Cleared up front so every early return below leaves the slot marked untraceable,
    // which is what a consumer tests.
    slot.instanceCount = 0;
    slot.hasMovingInstances = false;
    slot.built = false;

    // Emptied up front for the same reason: the table stays a valid binding either way.
    slot.emissiveCount = 0;
    const EmissiveLightTableHeader emptyHeader {};
    std::memcpy(slot.emissive.GetMapped(), &emptyHeader, sizeof(emptyHeader));
    m_EmissiveStaging.clear();
    m_EmissiveWeights.clear();

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
    // Records are indexed by cursor, which every instance advances. Structure instances are
    // counted apart: a raster-only surface keeps its record and gets no instance.
    uint32_t tlasCount = 0;
    for (const RenderObject& object : snapshot.objects)
    {
      VulkanVertexBuffer* vertexBuffer = ResolveGeometry(object, meshes, materials);
      if (vertexBuffer == nullptr)
        continue;

      const bool instanced = object.instanceData != nullptr && !object.noShading;
      const uint32_t count = instanced ? uint32_t(object.instanceData->size()) : 1u;
      if (cursor + count > capacity)
        break;

      // A transparent surface is a dielectric for the path tracer when PT Glass is on and its material
      // has a transmission mode, and raster-only otherwise - the rule IsPathTraceTransmissive states, on
      // the snapshot's transparency.
      const Material& material = materials.Get(object.material);
      const bool dielectric = glassEnabled && object.isTransparent
        && material.transmissionMode != TransmissionMode::None;

      VkGeometryInstanceFlagsKHR instanceFlags = 0;
      if (object.doubleSided)
        instanceFlags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
      // Alpha-test surfaces are left non-opaque so the any-hit shaders run the cutout. So is glass: a
      // shadow ray or a glass transmittance query reads it through pt_shadow.rahit on its way. The
      // bottom level geometry carries no opaque flag, which is what leaves the decision to the
      // instance.
      if (!object.isAlphaTest && !dielectric)
        instanceFlags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;

      uint32_t recordFlags = 0;
      if (object.isAlphaTest)   recordFlags |= RT_INSTANCE_ALPHA_TEST;
      if (object.isTransparent) recordFlags |= RT_INSTANCE_TRANSPARENT;
      if (object.isTerrain)     recordFlags |= RT_INSTANCE_TERRAIN;
      if (object.noShading)     recordFlags |= RT_INSTANCE_UNLIT;
      if (object.doubleSided)   recordFlags |= RT_INSTANCE_DOUBLE_SIDED;
      if (dielectric)
      {
        switch (material.transmissionMode)
        {
          case TransmissionMode::Sheet: recordFlags |= RT_INSTANCE_SHEET_DIELECTRIC; break;
          case TransmissionMode::ThinWalled: recordFlags |= RT_INSTANCE_THIN_WALLED_DIELECTRIC; break;
          default: recordFlags |= RT_INSTANCE_SOLID_DIELECTRIC; break;
        }
      }

      // Every instance of one object shares its geometry and its material, so the record
      // is built once and only the custom index it is stored at moves.
      RayTracingInstanceRecord record {
        .vertexAddress = vertexBuffer->GetVertexAddress(),
        .indexAddress = vertexBuffer->GetIndexAddress(),
        .attributeOffset = uint32_t(vertexBuffer->GetAttribOffset()),
        .materialIndex = object.material.index,
        .flags = recordFlags,
        .emissiveIndex = RT_INSTANCE_NOT_EMISSIVE,
      };
      // The same for every instance too: prevWorld * offset * inverse(world * offset) cancels
      // the static instance offset down to prevWorld * inverse(world).
      WriteWorldToPrevWorld(object, record.worldToPrevWorld);

      // A raster-only transparent surface keeps its record, which the emissive light table below
      // may name, but no ray may see it, so it gets no structure instance at all. Glass that can
      // refract is kept apart from glass that never does, so a trace can leave out either, and a
      // moving opaque surface is marked for the reflector lookup.
      const bool rasterOnly = object.isTransparent && !dielectric;
      const bool moving = !object.isTransparent && object.prevWorldTransform != object.worldTransform;
      const uint32_t mask = !object.isTransparent ? (moving ? RT_MASK_OPAQUE | RT_MASK_MOVING : RT_MASK_OPAQUE)
        : material.transmissionMode == TransmissionMode::Solid ? RT_MASK_GLASS_REFRACTIVE
        : RT_MASK_GLASS_STRAIGHT;
      slot.hasMovingInstances = slot.hasMovingInstances || moving;

      // An instance whose any-hit runs - the non-opaque ones below - references the structure that
      // reports each triangle to it once (VulkanVertexBuffer::GetSingleAnyHitBottomLevel); FORCE_OPAQUE
      // geometry keeps the faster one.
      const bool anyHit = object.isAlphaTest || dielectric;
      VkDeviceAddress bottomLevel = 0;
      if (!rasterOnly)
      {
        const VulkanAccelerationStructure* structure = anyHit
          ? vertexBuffer->GetSingleAnyHitBottomLevel(ctx) : &vertexBuffer->GetBottomLevel();
        if (structure == nullptr)
          continue;
        bottomLevel = structure->GetDeviceAddress();
      }

      // Whether the object's instances enter the emissive light table is the material's call, so
      // it is made once; only the transform the selection weight scales with differs per instance.
      // The emission this reads is the one RayTracingMaterialTable::Update writes from the same
      // MaterialManager state this frame.
      //
      // Only a material that can produce an emissive texel enters, by gbuffer.frag's and
      // resolveEmissiveTexel's rule on the constant emission, which an emissive map in [0, 1] only
      // scales down. Imported materials routinely carry emission at or below the cutoff that no
      // shader ever shows, and an entry for one would only waste candidates.
      const float emissiveLuminance = Luminance(material.emissivity * material.emissiveIntensity);
      const uint32_t triangleCount = uint32_t(vertexBuffer->GetIndexCount() / 3);
      const bool emissive = material.emissive && emissiveLuminance > EMISSIVE_SHADING_CUTOFF
        && !object.isTerrain && triangleCount > 0;

      // A crude stand-in for the power an instance emits: its luminance times the surface area of
      // the mesh's object space bounding box, scaled to world space per instance below. It only
      // decides how often the instance becomes a candidate - the shader divides by the probability
      // it actually used - so a poor estimate costs variance and never bias.
      double boundsArea = 0.0;
      if (emissive)
      {
        const glm::dvec3 extent = glm::max(
          glm::dvec3(meshes.GetMaxBB(object.mesh)) - glm::dvec3(meshes.GetMinBB(object.mesh)), glm::dvec3(0.0));
        boundsArea = 2.0 * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
      }

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

        RayTracingInstanceRecord instanceRecord = record;
        if (emissive)
        {
          // Area grows with the square of a linear scale and |det| with its cube.
          const double areaScale = std::pow(std::abs(double(glm::determinant(glm::mat3(world)))), 2.0 / 3.0);
          const double weight = emissiveLuminance * boundsArea * areaScale;

          // A weight of zero would never be drawn, and leaving the instance out of the table instead
          // gives a hit on it the full weight nothing else competes for.
          if (weight > 0.0 && std::isfinite(weight))
          {
            instanceRecord.emissiveIndex = uint32_t(m_EmissiveStaging.size());

            EmissiveLightRecord light {
              .instanceIndex = cursor,
              .triangleCount = triangleCount,
              // Raster-only transparent instances have no structure instance for a ray to hit.
              .flags = rasterOnly ? EMISSIVE_LIGHT_NEE_ONLY : 0u,
              .pmf = 0.0f,
              .aliasThreshold = 0.0f,
              .aliasIndex = 0,
              ._pad0 = 0,
              ._pad1 = 0,
            };
            WriteRows(world, light.objectToWorld);

            m_EmissiveStaging.push_back(light);
            m_EmissiveWeights.push_back(weight);
          }
        }

        if (!rasterOnly)
          instances[tlasCount++] = instance;
        records[cursor] = instanceRecord;
        cursor++;
      }
    }

    slot.instanceCount = cursor;
    UploadEmissiveTable(ctx, slot);

    if (tlasCount == 0)
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

    // The instance array, the records and the emissive light table were just written through a
    // host mapping. vkQueueSubmit makes host writes visible on its own, but they are read at
    // different points - the build reads the instances, a tracing pass reads the rest - and one
    // barrier covers both scopes.
    VkMemoryBarrier hostBarrier {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | traceStages,
      0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

    slot.structure.CmdBuildTopLevel(ctx, cmd, slot.instanceAddress, tlasCount,
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
        "TLAS built: %u instances (%u records) over %zu objects, %u emissive, %llu record bytes, %llu structure bytes",
        tlasCount, slot.instanceCount, snapshot.objects.size(), slot.emissiveCount,
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

  bool TlasBuilder::HasMovingInstances(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() && m_Slots[frameIndex].hasMovingInstances;
  }

  VkBuffer TlasBuilder::GetEmissiveBuffer(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].emissive.Get() : VK_NULL_HANDLE;
  }

  VkDeviceSize TlasBuilder::GetEmissiveBufferSize(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].emissive.GetSize() : 0;
  }

  uint32_t TlasBuilder::GetEmissiveCount(uint32_t frameIndex) const
  {
    return frameIndex < m_Slots.size() ? m_Slots[frameIndex].emissiveCount : 0;
  }
}
