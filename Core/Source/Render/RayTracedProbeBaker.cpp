#ifdef YA_EDITOR

#include "RayTracedProbeBaker.h"

#include "BakeLimits.h"
#include "BindlessTextureRegistry.h"
#include "PathTraceData.h"
#include "Render.h"
#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    constexpr uint32_t GEOMETRY_RAYS_PER_SAMPLE = 1;

    // How many light candidate evaluations the submit caps count as one ray. An evaluation is a few
    // dozen scalar operations on one light record, a ray at least a BVH traversal plus a closest
    // hit that resolves a material; 8 sits at the low end of that ratio, so the light cost is
    // overstated rather than missed.
    constexpr uint32_t LIGHT_EVALUATIONS_PER_RAY = 8;
    constexpr uint32_t MAX_LIGHT_CANDIDATES = 1 + MAX_POINT_LIGHTS + MAX_SPOT_LIGHTS;

    SHL1Channel ToChannel(const glm::dvec4& values)
    {
      return SHL1Channel { .l0 = float(values.x), .l1x = float(values.y), .l1y = float(values.z), .l1z = float(values.w) };
    }

    float DistanceOrNoHit(float recorded)
    {
      return recorded > 0.0f ? recorded : RayTracedProbeBaker::NO_HIT;
    }

    // Zero is "nothing recorded" on both sides, see ProbeBakeRecord.
    bool IsNearer(float distance, float recorded)
    {
      return distance > 0.0f && (recorded == 0.0f || distance < recorded);
    }

    // One point's pass records merged over all its passes.
    struct BackfaceTotals
    {
      uint64_t rayCount = 0;
      uint64_t backfaceCount = 0;
      float nearestBackfaceDistance = 0.0f;
      glm::vec3 nearestBackfaceDirection { 0.0f };

      void Add(const ProbeBakeRecord& record)
      {
        rayCount += record.rayCount;
        backfaceCount += record.backfaceCount;

        if (IsNearer(record.nearestBackfaceDistance, nearestBackfaceDistance))
        {
          nearestBackfaceDistance = record.nearestBackfaceDistance;
          nearestBackfaceDirection = record.nearestBackfaceDirection;
        }
      }

      float BackfaceFraction() const
      {
        return rayCount > 0 ? float(double(backfaceCount) / double(rayCount)) : 0.0f;
      }
    };

    // In double: a float sum over many passes drops a dim sample next to a bright sky long before
    // the sample count stops growing.
    struct RadianceTotals
    {
      glm::dvec4 shR { 0.0 };
      glm::dvec4 shG { 0.0 };
      glm::dvec4 shB { 0.0 };
      double weightSum = 0.0;

      void Add(const ProbeBakeRecord& record)
      {
        shR += glm::dvec4(record.shR);
        shG += glm::dvec4(record.shG);
        shB += glm::dvec4(record.shB);
        weightSum += record.weightSum;
      }
    };

    // What the rays hit, reported by both calls.
    struct HitTotals
    {
      BackfaceTotals backfaces;
      float nearestHitDistance = 0.0f;

      void Add(const ProbeBakeRecord& record)
      {
        backfaces.Add(record);
        if (IsNearer(record.nearestHitDistance, nearestHitDistance))
          nearestHitDistance = record.nearestHitDistance;
      }
    };

    struct IntegrateTotals
    {
      HitTotals hits;
      RadianceTotals radiance;
      uint64_t closeHitCount = 0;

      void Add(const ProbeBakeRecord& record)
      {
        hits.Add(record);
        radiance.Add(record);
        closeHitCount += record.closeHitCount;
      }

      float CloseHitFraction() const
      {
        const uint64_t rayCount = hits.backfaces.rayCount;
        return rayCount > 0 ? float(double(closeHitCount) / double(rayCount)) : 0.0f;
      }
    };

    ProbeGeometryResult FinalizeGeometry(const HitTotals& totals)
    {
      return ProbeGeometryResult {
        .nearestHitDistance = DistanceOrNoHit(totals.nearestHitDistance),
        .backfaceFraction = totals.backfaces.BackfaceFraction(),
        .nearestBackfaceDistance = DistanceOrNoHit(totals.backfaces.nearestBackfaceDistance),
        .nearestBackfaceDirection = totals.backfaces.nearestBackfaceDirection,
      };
    }

    ProbeIntegrateResult FinalizeIntegration(const IntegrateTotals& totals)
    {
      // Every sample weighs one, so for uniformly distributed directions Finalize's 4 pi / N
      // normalization is the Monte Carlo estimator. The division by N happens here in double and
      // Finalize gets the means with a unit weight, which keeps its float fields away from both
      // the raw sums and the sample count.
      SHL1Accumulator accumulator;
      if (totals.radiance.weightSum > 0.0)
      {
        const double inverseWeight = 1.0 / totals.radiance.weightSum;
        accumulator.coefficients = SHL1RGB {
          .r = ToChannel(totals.radiance.shR * inverseWeight),
          .g = ToChannel(totals.radiance.shG * inverseWeight),
          .b = ToChannel(totals.radiance.shB * inverseWeight),
        };
        accumulator.totalSolidAngle = 1.0f;
      }

      return ProbeIntegrateResult {
        .coefficients = accumulator.Finalize(),
        .nearestHitDistance = DistanceOrNoHit(totals.hits.nearestHitDistance),
        .backfaceFraction = totals.hits.backfaces.BackfaceFraction(),
        .nearestBackfaceDistance = DistanceOrNoHit(totals.hits.backfaces.nearestBackfaceDistance),
        .nearestBackfaceDirection = totals.hits.backfaces.nearestBackfaceDirection,
        .closeHitFraction = totals.CloseHitFraction(),
      };
    }

    uint32_t ClampToLimit(uint32_t requested, uint32_t limit, const char* what)
    {
      if (requested <= limit)
        return requested;

      YA_LOG_WARN("Render", "Ray traced probe bake: %u %s is above the limit, clamped to %u",
        requested, what, limit);
      return limit;
    }

    // One invocation traces all samples of a point's pass, and nothing the chunk scheduler does can
    // split that, so a pass is held to both per-pass limits. Fewer samples per pass only means more
    // passes.
    uint32_t SamplesPerPass(uint32_t requested, uint32_t totalSamples, uint32_t raysPerSample)
    {
      const uint32_t byRays = std::max(BakeLimits::RT_PROBE_MAX_RAYS_PER_POINT_PASS / raysPerSample, 1u);
      return std::min({ requested, totalSamples, BakeLimits::RT_PROBE_MAX_SAMPLES_PER_PASS, byRays });
    }

    // Traces a path segment takes where it meets glass crossed straight: the closest hit, the trace
    // past the glass and the transmittance query, see traceSegment in pt_path.glsl.
    constexpr uint32_t GLASS_SEGMENT_TRACES = 3;

    // Worst case per sample. Without glass: the primary ray, a shadow ray at every path vertex and a
    // continuation at every bounce. With glass the probe ray is followed once more as a path segment,
    // every segment may meet glass, and a path that refracts at Solid glass traces one more segment per
    // refractive event it may spend.
    uint32_t IntegrateRaysPerSample(int32_t maxBounces, bool glass, int32_t maxTransmissionDepth,
      int32_t secondaryGlass)
    {
      if (!glass)
        return uint32_t(2 * maxBounces + 2);

      const uint32_t vertices = uint32_t(maxBounces + 1);
      const uint32_t refractions = secondaryGlass == PT_GLASS_REFRACT ? uint32_t(maxTransmissionDepth) : 0;
      const uint32_t segments = 1 + uint32_t(maxBounces) + refractions;
      return 1 + GLASS_SEGMENT_TRACES * segments + vertices;
    }

    // Worst case in the rays the submit caps count. selectLight walks every candidate twice at each
    // of a sample's maxBounces + 1 path vertices, the directional slot included whether lit or not,
    // and each vertex draws PT_EMISSIVE_CANDIDATES emissive candidates on top, counted one evaluation
    // apiece whether the table holds anything or not.
    uint32_t IntegrateWorkPerSample(uint32_t raysPerSample, int32_t maxBounces, const LightBuffer& lights)
    {
      const uint32_t candidates = 1
        + uint32_t(std::clamp(lights.pointLightCount, 0, MAX_POINT_LIGHTS))
        + uint32_t(std::clamp(lights.spotLightCount, 0, MAX_SPOT_LIGHTS));
      const uint32_t evaluations = uint32_t(maxBounces + 1) * (2 * candidates + PT_EMISSIVE_CANDIDATES);
      return raysPerSample + (evaluations + LIGHT_EVALUATIONS_PER_RAY - 1) / LIGHT_EVALUATIONS_PER_RAY;
    }

    // Fisher-Yates driven by splitmix64 from a fixed seed, so a request is always traced in the
    // same order and its timing reproduces too.
    std::vector<uint32_t> ShuffledOrder(size_t count)
    {
      std::vector<uint32_t> order(count);
      for (size_t i = 0; i < count; i++)
        order[i] = uint32_t(i);

      uint64_t state = 0;
      for (size_t i = count; i > 1; i--)
      {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        std::swap(order[i - 1], order[size_t(z % i)]);
      }

      return order;
    }
  }

  // One point's pass has to fit into a single submit. A pass traces at most
  // RT_PROBE_MAX_RAYS_PER_POINT_PASS rays, and a sample evaluates light candidates at most rays x
  // (MAX_LIGHT_CANDIDATES + PT_EMISSIVE_CANDIDATES / 2) times (two walks, the emissive draws and two
  // rays per vertex), which the emissive term below overstates; the rest covers the rays themselves
  // and the rounding.
  static_assert(uint64_t(BakeLimits::RT_PROBE_MAX_RAYS_PER_POINT_PASS)
    * (2 + (MAX_LIGHT_CANDIDATES + PT_EMISSIVE_CANDIDATES) / LIGHT_EVALUATIONS_PER_RAY + 1)
    <= BakeLimits::RT_PROBE_MAX_RAYS_PER_SUBMIT,
    "One point's pass, light evaluations included, has to fit into a single submit");

  void RayTracedProbeBaker::Init(Render& render)
  {
    m_Render = &render;
    m_Ctx = &render.GetContext();
    const RenderContext& ctx = *m_Ctx;

    // The path tracer's gate: every hit resolves its material through the bindless table, and
    // the estimator has no permutation without it.
    const bool bindless = ctx.bindlessTextures != nullptr && ctx.bindlessTextures->IsValid();
    if (!ctx.raytracingSupported || !ctx.rayTracing.IsPipelineLoaded() || !bindless)
      return;

    m_FrameUBO.Init(ctx);

    const VkShaderStageFlags stages = VK_SHADER_STAGE_RAYGEN_BIT_KHR
      | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
      | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
      | VK_SHADER_STAGE_MISS_BIT_KHR;

    SetDescription setDesc = {
      .set = 1,
      .bindings = {
        // The scene, laid out as every ray tracing pass lays it out
        { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, stages },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages },
        // Sky and lights, then the points in and the records of a pass out
        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stages },
        { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages },
        { 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages },
        { 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages },
        // The bake slot's emissive light table
        { 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages },
      }
    };
    m_DescriptorSet.Init(ctx, setDesc);

    // Zeroed so a geometry query, which binds it without reading it, never binds garbage.
    m_LightBuffer = VulkanBuffer::CreateMapped(ctx, sizeof(LightBuffer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    std::memset(m_LightBuffer.GetMapped(), 0, sizeof(LightBuffer));

    RaytracingPipelineCreateInfo info;
    info.raygenShaderFile = "probe_bake.rgen";
    // Placed by index so the list cannot drift from the numbers pt_path.glsl traces with.
    info.missShaderFiles.resize(2);
    info.missShaderFiles[PT_PRIMARY_MISS_INDEX] = "pathtrace.rmiss";
    info.missShaderFiles[PT_SHADOW_MISS_INDEX] = "pt_shadow.rmiss";
    info.hitGroups.resize(2);
    info.hitGroups[PT_PATH_HIT_GROUP] = RaytracingHitGroup {
      .closestHitShaderFile = "pathtrace.rchit", .anyHitShaderFile = "pathtrace.rahit" };
    info.hitGroups[PT_SHADOW_HIT_GROUP] = RaytracingHitGroup { .anyHitShaderFile = "pt_shadow.rahit" };
    info.sets = {
      m_FrameUBO.GetLayout(),
      m_DescriptorSet.GetLayout(),
      ctx.bindlessTextures->GetLayout(),
    };
    info.pushConstantSize = sizeof(ProbeBakeConstants);

    m_Pipeline = render.m_PSOCache.RegisterRayTracing(ctx, info, ctx.pipelineCache);
  }

  void RayTracedProbeBaker::Destroy()
  {
    if (!m_Ctx) return;

    ReleaseBlockBuffers();
    m_LightBuffer.Destroy(*m_Ctx);
    m_DescriptorSet.Destroy();
    m_FrameUBO.Destroy(*m_Ctx);
    m_Pipeline = {};

    m_Ctx = nullptr;
    m_Render = nullptr;
  }

  void RayTracedProbeBaker::ReleaseBlockBuffers()
  {
    m_PointBuffer.Destroy(*m_Ctx);
    m_RecordBuffer.Destroy(*m_Ctx);
    m_ReadbackBuffer.Destroy(*m_Ctx);
  }

  bool RayTracedProbeBaker::CheckReady(const char* call) const
  {
    if (!IsAvailable())
    {
      YA_LOG_ERROR("Render", "Ray traced probe %s: unavailable, it needs the ray tracing pipeline and the bindless texture table",
        call);
      return false;
    }

    const TlasBuilder& tlas = m_Render->m_TlasBuilder;
    const RayTracingMaterialTable& materials = m_Render->m_MaterialTable;
    if (!tlas.IsValid(tlas.GetBakeSlot()) || !materials.IsValid(materials.GetBakeSlot()))
    {
      YA_LOG_ERROR("Render", "Ray traced probe %s: the bake slot holds no traceable scene - Render::BuildRayTracingBakeScene has to run first",
        call);
      return false;
    }

    return true;
  }

  void RayTracedProbeBaker::WriteFrameUniforms()
  {
    // Only the estimator's albedo decode reads this block, but all of it has to be valid, so it is
    // neutral like OffscreenRenderer's with every effect off. Gamma is the exception: the G-buffer,
    // forward shading and the path tracer all decode base color with the live setting, and a bake
    // decoding with any other value would bounce differently coloured light than the frame shows.
    FrameUniforms& uniforms = m_FrameUBO.uniforms;
    uniforms = FrameUniforms {};

    const glm::mat4 identity(1.0f);
    uniforms.view = identity;
    uniforms.proj = identity;
    uniforms.invProj = identity;
    uniforms.prevView = identity;
    uniforms.prevProj = identity;
    uniforms.invView = identity;
    uniforms.unjitteredProj = identity;
    uniforms.nearPlane = 0.01f;
    uniforms.farPlane = 1000.0f;
    uniforms.fov = glm::radians(90.0f);
    uniforms.screenWidth = 1;
    uniforms.screenHeight = 1;
    uniforms.outputWidth = 1;
    uniforms.outputHeight = 1;
    uniforms.tileCountX = 1;
    uniforms.tileCountY = 1;
    uniforms.gamma = m_Render->GetGamma();
    uniforms.exposure = 1.0f;

    m_FrameUBO.SetUp(0);
  }

  void RayTracedProbeBaker::Dispatch(std::span<const ProbeBakePoint> points, const DispatchDesc& desc,
    const PassCollector& collect)
  {
    const RenderContext& ctx = *m_Ctx;
    const size_t pointCount = points.size();
    const uint32_t blockCapacity = uint32_t(std::min<size_t>(pointCount, BakeLimits::RT_PROBE_MAX_BLOCK_POINTS));
    const uint32_t blockCount = uint32_t((pointCount + blockCapacity - 1) / blockCapacity);
    const VkDeviceSize pointBytes = VkDeviceSize(blockCapacity) * sizeof(ProbeBakePoint);
    const VkDeviceSize recordBytes = VkDeviceSize(blockCapacity) * sizeof(ProbeBakeRecord);

    // Released by scope: every submit below throws on failure.
    struct BlockBufferScope
    {
      RayTracedProbeBaker& baker;
      ~BlockBufferScope() { baker.ReleaseBlockBuffers(); }
    } blockBufferScope { *this };

    m_PointBuffer = VulkanBuffer::CreateMapped(ctx, pointBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    // Device local: the shader writes it throughout a pass, and the host needs it once per pass,
    // through the readback copy.
    m_RecordBuffer = VulkanBuffer::CreateGpuOnly(ctx, recordBytes,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    m_ReadbackBuffer = VulkanBuffer::CreateReadback(ctx, recordBytes);

    WriteFrameUniforms();

    const TlasBuilder& tlas = m_Render->m_TlasBuilder;
    const RayTracingMaterialTable& materials = m_Render->m_MaterialTable;
    const uint32_t tlasSlot = tlas.GetBakeSlot();
    const uint32_t materialSlot = materials.GetBakeSlot();

    // Rewritten per request, see the precondition in the header; the skybox may have changed
    // since the last request too.
    m_DescriptorSet.Writer()
      .WriteAccelerationStructure(0, tlas.Get(tlasSlot))
      .WriteStorageBuffer(1, tlas.GetRecordBuffer(tlasSlot), tlas.GetRecordBufferSize(tlasSlot))
      .WriteStorageBuffer(2, materials.GetBuffer(materialSlot), materials.GetBufferSize(materialSlot))
      .WriteCombinedImageSampler(3, m_Render->m_SkyboxView, m_Render->m_SkyboxSampler)
      .WriteStorageBuffer(4, m_LightBuffer.Get(), sizeof(LightBuffer))
      .WriteStorageBuffer(5, m_PointBuffer.Get(), pointBytes)
      .WriteStorageBuffer(6, m_RecordBuffer.Get(), recordBytes)
      .WriteStorageBuffer(7, tlas.GetEmissiveBuffer(tlasSlot), tlas.GetEmissiveBufferSize(tlasSlot))
      .Flush();

    VulkanRaytracingPipeline& pipeline = m_Render->m_PSOCache.GetRayTracing(m_Pipeline);
    const VkDescriptorSet frameSet = m_FrameUBO.GetDescriptorSet(0);
    const VkDescriptorSet bindlessSet = ctx.bindlessTextures->GetSet();

    // Traced in a shuffled order so that every chunk is a fair sample of the whole request. A
    // lattice is spatially coherent: a run of buried or open sky points can be tens of times
    // cheaper than the lit interior next to it, and a chunk grown on the first would hold the GPU
    // for seconds on the second.
    const std::vector<uint32_t> order = ShuffledOrder(pointCount);

    // Steered in point-samples rather than points, so the shorter last pass keeps the same rate.
    const double maxChunkSamples = std::min(
      double(BakeLimits::RT_PROBE_MAX_RAYS_PER_SUBMIT / desc.workPerSample),
      double(blockCapacity) * desc.samplesPerPass);
    double chunkSamples = std::min({
      double(INITIAL_CHUNK_POINTS) * desc.samplesPerPass,
      double(INITIAL_CHUNK_RAYS / desc.workPerSample),
      maxChunkSamples });

    std::array<double, COST_HISTORY_SUBMITS> recentCosts {};
    size_t nextCost = 0;
    uint32_t underTargetStreak = 0;
    bool slowPointWarned = false;

    uint32_t minChunk = UINT32_MAX;
    uint32_t maxChunk = 0;
    uint32_t submitCount = 0;

    const uint64_t totalPointSamples = uint64_t(pointCount) * desc.totalSamples;
    uint64_t donePointSamples = 0;

    const double startTime = glfwGetTime();
    double lastLogTime = startTime;

    for (uint32_t block = 0; block < blockCount; block++)
    {
      const size_t blockStart = size_t(block) * blockCapacity;
      const uint32_t blockPoints = uint32_t(std::min<size_t>(blockCapacity, pointCount - blockStart));
      const std::span<const uint32_t> blockOrder(order.data() + blockStart, blockPoints);

      ProbeBakePoint* mappedPoints = static_cast<ProbeBakePoint*>(m_PointBuffer.GetMapped());
      for (uint32_t i = 0; i < blockPoints; i++)
        mappedPoints[i] = points[blockOrder[i]];

      for (uint32_t pass = 0; pass < desc.passCount; pass++)
      {
        const uint32_t passSamples = pass + 1 < desc.passCount
          ? desc.samplesPerPass
          : desc.totalSamples - desc.samplesPerPass * pass;

        // Worst-case rays per submit, light evaluations included, whatever the measurements below say.
        const uint32_t capPoints = uint32_t(std::clamp<uint64_t>(
          BakeLimits::RT_PROBE_MAX_RAYS_PER_SUBMIT / (uint64_t(passSamples) * desc.workPerSample), 1, blockPoints));

        for (uint32_t first = 0; first < blockPoints; )
        {
          const uint32_t plannedPoints = std::min(capPoints,
            uint32_t(std::clamp(chunkSamples / passSamples, 1.0, double(blockCapacity))));
          const uint32_t count = std::min(plannedPoints, blockPoints - first);

          const ProbeBakeConstants constants {
            .mode = desc.mode,
            .firstPoint = first,
            .pointCount = count,
            .passIndex = pass,
            .samplesPerPass = passSamples,
            .maxBounces = desc.maxBounces,
            .fireflyClamp = desc.fireflyClamp,
            .maxTransmissionDepth = desc.maxTransmissionDepth,
            .glassOverflow = desc.glassOverflow,
            .secondaryGlass = desc.secondaryGlass,
            .glassEnabled = desc.glass ? 1 : 0,
          };

          const double submitStart = glfwGetTime();

          VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();
          pipeline.Bind(cmd);
          pipeline.BindDescriptorSets(cmd, { frameSet }, 0);
          pipeline.BindDescriptorSets(cmd, { m_DescriptorSet.Get() }, 1);
          pipeline.BindDescriptorSets(cmd, { bindlessSet }, BindlessTextureRegistry::BINDLESS_TEXTURE_SET);
          pipeline.PushConstants(cmd, &constants);
          pipeline.TraceRays(cmd, count, 1);
          ctx.commandBuffer->EndSingleTimeCommands(cmd);

          const double submitSeconds = std::max(glfwGetTime() - submitStart, 1e-4);

          submitCount++;
          minChunk = std::min(minChunk, count);
          maxChunk = std::max(maxChunk, count);
          first += count;
          donePointSamples += uint64_t(count) * passSamples;

          const bool overTarget = submitSeconds > TARGET_SUBMIT_SECONDS;
          if (overTarget && count == 1 && !slowPointWarned)
          {
            slowPointWarned = true;
            YA_LOG_WARN("Render", "%s: a single point took %.0f ms for %u samples, above the %.0f ms submit target - BakeLimits::RT_PROBE_MAX_RAYS_PER_POINT_PASS is too high for this scene",
              desc.label, submitSeconds * 1000.0, passSamples, TARGET_SUBMIT_SECONDS * 1000.0);
          }

          // A block's short tail spreads the fixed submit overhead over few points and would read
          // as slow, so it only steers when it really is over the target.
          if (count == plannedPoints || overTarget)
          {
            recentCosts[nextCost++ % COST_HISTORY_SUBMITS] = submitSeconds / (double(count) * passSamples);
            const double worstCost = *std::max_element(recentCosts.begin(), recentCosts.end());

            underTargetStreak = overTarget ? 0 : underTargetStreak + 1;
            const bool grow = underTargetStreak >= GROW_AFTER_UNDER_TARGET_SUBMITS;
            if (grow)
              underTargetStreak = 0;

            // Never past what the slowest recent rate fits into the target, which is also the
            // proportional shrink after an overshoot. Never below one point's pass either: after
            // an outlier, growth steps from far below it would plan the same single point for a
            // while.
            chunkSamples = std::max(std::min({
              grow ? chunkSamples * MAX_CHUNK_GROWTH : chunkSamples,
              TARGET_SUBMIT_SECONDS / worstCost,
              maxChunkSamples }), double(passSamples));
          }

          const double now = glfwGetTime();
          if (now - lastLogTime >= PROGRESS_LOG_INTERVAL_SECONDS)
          {
            lastLogTime = now;
            const double elapsed = now - startTime;
            const double eta = elapsed * double(totalPointSamples - donePointSamples) / double(donePointSamples);
            YA_LOG_INFO("Render", "%s: block %u/%u, pass %u/%u, %llu/%llu point samples, %.1f s elapsed, ETA %.1f s, chunk %u points",
              desc.label, block + 1, blockCount, pass + 1, desc.passCount,
              (unsigned long long)donePointSamples, (unsigned long long)totalPointSamples, elapsed, eta, count);
          }
        }

        VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();
        VkBufferCopy region {
          .srcOffset = 0,
          .dstOffset = 0,
          .size = VkDeviceSize(blockPoints) * sizeof(ProbeBakeRecord),
        };
        vkCmdCopyBuffer(cmd, m_RecordBuffer.Get(), m_ReadbackBuffer.Get(), 1, &region);
        ctx.commandBuffer->EndSingleTimeCommands(cmd);

        collect(pass, blockOrder, std::span<const ProbeBakeRecord>(
          static_cast<const ProbeBakeRecord*>(m_ReadbackBuffer.GetMapped()), blockPoints));
      }
    }

    const double totalSeconds = glfwGetTime() - startTime;
    const uint64_t primarySamples = uint64_t(pointCount) * desc.totalSamples;
    YA_LOG_INFO("Render", "%s done: %zu points x %u samples in %u pass(es) of %u, %.2f s, %llu primary samples (%.0f/s), %u submits, chunk min %u / max %u points",
      desc.label, pointCount, desc.totalSamples, desc.passCount, desc.samplesPerPass, totalSeconds,
      (unsigned long long)primarySamples, double(primarySamples) / std::max(totalSeconds, 1e-6),
      submitCount, minChunk, maxChunk);
  }

  bool RayTracedProbeBaker::GeometryQuery(std::span<const ProbeBakePoint> points, uint32_t raysPerPoint,
    std::vector<ProbeGeometryResult>& outResults)
  {
    outResults.clear();

    if (!CheckReady("geometry query"))
      return false;

    if (raysPerPoint == 0)
    {
      YA_LOG_ERROR("Render", "Ray traced probe geometry query: zero rays per point");
      return false;
    }

    outResults.resize(points.size());
    if (points.empty())
      return true;

    const uint32_t rays = ClampToLimit(raysPerPoint, BakeLimits::RT_PROBE_MAX_SAMPLES_PER_PROBE, "rays per point");
    const uint32_t raysPerPass = SamplesPerPass(rays, rays, GEOMETRY_RAYS_PER_SAMPLE);

    const DispatchDesc dispatch {
      .mode = PROBE_BAKE_MODE_GEOMETRY,
      .samplesPerPass = raysPerPass,
      .totalSamples = rays,
      .passCount = (rays - 1) / raysPerPass + 1,
      .workPerSample = GEOMETRY_RAYS_PER_SAMPLE,
      .label = "Probe geometry query",
    };

    // Merged in block slot order, which walks the totals sequentially, and scattered to the
    // request's order once the block's last pass is in.
    const uint32_t lastPass = dispatch.passCount - 1;
    std::vector<HitTotals> blockTotals;
    Dispatch(points, dispatch, [&blockTotals, &outResults, lastPass](uint32_t pass,
      std::span<const uint32_t> pointIndices, std::span<const ProbeBakeRecord> records)
    {
      if (pass == 0)
        blockTotals.assign(records.size(), HitTotals {});

      for (size_t i = 0; i < records.size(); i++)
        blockTotals[i].Add(records[i]);

      if (pass != lastPass)
        return;

      for (size_t i = 0; i < records.size(); i++)
        outResults[pointIndices[i]] = FinalizeGeometry(blockTotals[i]);
    });

    return true;
  }

  bool RayTracedProbeBaker::Integrate(std::span<const ProbeBakePoint> probes, const LightBuffer& lights,
    const ProbeIntegrateDesc& desc, const IntegrateCollector& collect)
  {
    if (!CheckReady("integration"))
      return false;

    if (desc.samplesPerProbe == 0 || desc.samplesPerPass == 0)
    {
      YA_LOG_ERROR("Render", "Ray traced probe integration: %u samples per probe in passes of %u, both have to be above zero",
        desc.samplesPerProbe, desc.samplesPerPass);
      return false;
    }

    if (probes.empty())
      return true;

    const uint32_t samplesPerProbe = ClampToLimit(desc.samplesPerProbe, BakeLimits::RT_PROBE_MAX_SAMPLES_PER_PROBE,
      "samples per probe");
    const int32_t maxBounces = std::clamp(desc.maxBounces, PT_MIN_BOUNCES, PT_MAX_BOUNCES);

    std::memcpy(m_LightBuffer.GetMapped(), &lights, sizeof(LightBuffer));

    DispatchDesc dispatch {
      .mode = PROBE_BAKE_MODE_INTEGRATE,
      .totalSamples = samplesPerProbe,
      .maxBounces = maxBounces,
      .fireflyClamp = std::clamp(desc.fireflyClamp, PT_MIN_FIREFLY_CLAMP, PT_MAX_FIREFLY_CLAMP),
      .glass = desc.glass,
      .maxTransmissionDepth = std::clamp(desc.maxTransmissionDepth, PT_MIN_TRANSMISSION_DEPTH,
        PT_MAX_TRANSMISSION_DEPTH),
      .glassOverflow = std::clamp(desc.glassOverflow, PT_GLASS_OVERFLOW_TERMINATE, PT_GLASS_OVERFLOW_STRAIGHT),
      .secondaryGlass = std::clamp(desc.secondaryGlass, PT_GLASS_REFRACT, PT_GLASS_STRAIGHT),
      .label = "Probe integration",
    };
    // Sized from the clamped glass settings above, so a bake with glass takes fewer samples per pass and
    // smaller chunks.
    const uint32_t raysPerSample = IntegrateRaysPerSample(dispatch.maxBounces, dispatch.glass,
      dispatch.maxTransmissionDepth, dispatch.secondaryGlass);
    dispatch.samplesPerPass = SamplesPerPass(desc.samplesPerPass, samplesPerProbe, raysPerSample);
    dispatch.passCount = (samplesPerProbe - 1) / dispatch.samplesPerPass + 1;
    dispatch.workPerSample = IntegrateWorkPerSample(raysPerSample, dispatch.maxBounces, lights);

    // Merged and scattered by block as in GeometryQuery.
    const uint32_t lastPass = dispatch.passCount - 1;
    std::vector<IntegrateTotals> blockTotals;
    Dispatch(probes, dispatch, [&blockTotals, &collect, lastPass](uint32_t pass,
      std::span<const uint32_t> pointIndices, std::span<const ProbeBakeRecord> records)
    {
      if (pass == 0)
        blockTotals.assign(records.size(), IntegrateTotals {});

      for (size_t i = 0; i < records.size(); i++)
        blockTotals[i].Add(records[i]);

      if (pass != lastPass)
        return;

      for (size_t i = 0; i < records.size(); i++)
        collect(pointIndices[i], FinalizeIntegration(blockTotals[i]));
    });

    return true;
  }
}

#endif
