#pragma once

#include "Pch.h"
#include "FrameUniformBuffer.h"
#include "LightData.h"
#include "PathTraceData.h"
#include "PipelineCache.h"
#include "ProbeBakeData.h"
#include "VulkanBuffer.h"
#include "VulkanDescriptorSet.h"
#include "Utils/SphericalHarmonics.h"

namespace YAEngine
{
  class Render;
  struct RenderContext;

  static_assert(sizeof(ProbeBakeConstants) == 48,
    "ProbeBakeConstants no longer matches its push constant layout");
  static_assert(sizeof(ProbeBakePoint) == 32 && offsetof(ProbeBakePoint, closeHitDistance) == 12
    && offsetof(ProbeBakePoint, seedKey) == 16,
    "ProbeBakePoint no longer matches its std430 layout");
  static_assert(sizeof(ProbeBakeRecord) == 96 && offsetof(ProbeBakeRecord, nearestBackfaceDirection) == 64
    && offsetof(ProbeBakeRecord, closeHitCount) == 80,
    "ProbeBakeRecord no longer matches its std430 layout");

  struct ProbeGeometryResult
  {
    // RayTracedProbeBaker::NO_HIT where no ray hit anything.
    float nearestHitDistance = 0.0f;
    // Back-face hits over rays traced. A double sided instance has no back face.
    float backfaceFraction = 0.0f;
    // NO_HIT, with a zero direction, where no ray hit a back face.
    float nearestBackfaceDistance = 0.0f;
    glm::vec3 nearestBackfaceDirection { 0.0f };
  };

  struct ProbeIntegrateDesc
  {
    // Clamped to BakeLimits::RT_PROBE_MAX_SAMPLES_PER_PROBE.
    uint32_t samplesPerProbe = 0;
    // Most samples one pass traces per probe; the rest follow in further passes, the last one
    // tracing the remainder. Lowered to BakeLimits::RT_PROBE_MAX_SAMPLES_PER_PASS and to what
    // RT_PROBE_MAX_RAYS_PER_POINT_PASS allows at maxBounces, which only adds passes.
    uint32_t samplesPerPass = 0;
    int32_t maxBounces = 3;
    // 0 = off, as for the path tracer.
    float fireflyClamp = 10.0f;
    // The PT Glass switch, then the refractive events a path may take and how glass is met, as for the
    // path tracer: PT_GLASS_OVERFLOW_*, and PT_GLASS_* for every ray of a baked path.
    bool glass = false;
    int32_t maxTransmissionDepth = PT_DEFAULT_TRANSMISSION_DEPTH;
    int32_t glassOverflow = PT_GLASS_OVERFLOW_STRAIGHT;
    int32_t secondaryGlass = PT_GLASS_STRAIGHT;
  };

  struct ProbeIntegrateResult
  {
    SHL1RGB coefficients {};
    // As ProbeGeometryResult::nearestHitDistance.
    float nearestHitDistance = 0.0f;
    float backfaceFraction = 0.0f;
    float nearestBackfaceDistance = 0.0f;
    glm::vec3 nearestBackfaceDirection { 0.0f };
    // Rays whose first hit lies nearer than the probe's ProbeBakePoint::closeHitDistance, over rays
    // traced.
    float closeHitFraction = 0.0f;
  };

  // Irradiance probes baked by ray tracing, and the geometry queries that decide where a probe may
  // stand. The radiance past every first hit - with PT Glass on, along the probe ray itself - is
  // pt_path.glsl's estimator, so with the firefly clamp
  // off a baked probe converges on the irradiance the path tracer gathers at that point; a clamp
  // darkens the bake where it bites, see probe_bake.rgen. The runtime keeps reading baked L1 SH and
  // never traces.
  //
  // Points come in as ProbeBakePoint, a world position and an integer seed key. The seed key, the
  // pass index and the sample index are all a sample depends on, and every point's passes are
  // merged in pass order, so identical input bakes identical output however the request was
  // ordered and split into blocks and chunks.
  //
  // PRECONDITION of both calls: Render::BuildRayTracingBakeScene has filled the TLAS and material
  // table bake slots from the snapshot being baked. Only their validity is checked here - a bake
  // slot left over from an earlier snapshot is traced as it is. A build that grows either slot
  // replaces its structure and buffers outright, so every call writes set 1 bindings 0-2 and the
  // emissive light table at 7 afresh from the slots' current handles rather than keeping what an
  // earlier call bound.
  class RayTracedProbeBaker
  {
  public:

    static constexpr float NO_HIT = std::numeric_limits<float>::infinity();

    // Receives one probe's finished result with its index into the request. Every probe arrives
    // exactly once, block by block, in no particular order within a block.
    using IntegrateCollector = std::function<void(uint32_t probeIndex, const ProbeIntegrateResult& result)>;

    void Init(Render& render);
    void Destroy();

    // False without the ray tracing pipeline or the bindless texture table, which is exactly what
    // the path tracer needs too. Both calls then log an error and fail.
    bool IsAvailable() const { return static_cast<bool>(m_Pipeline); }

    // Traces raysPerPoint rays from every point and reports what they hit, resolving no material.
    // Clamped to BakeLimits::RT_PROBE_MAX_SAMPLES_PER_PROBE and traced in as many passes as the
    // per-pass limits need.
    bool GeometryQuery(std::span<const ProbeBakePoint> points, uint32_t raysPerPoint,
      std::vector<ProbeGeometryResult>& outResults);

    // Integrates the radiance arriving at every probe into L1 SH. A lattice probe's seed key is its
    // integer lattice coordinate. lights is the LightBuffer BuildBakeSceneSnapshot filled together
    // with the snapshot the bake slot was built from. Results go to collect as each block finishes
    // rather than into an array, so a caller baking millions of probes keeps only what it needs.
    bool Integrate(std::span<const ProbeBakePoint> probes, const LightBuffer& lights,
      const ProbeIntegrateDesc& desc, const IntegrateCollector& collect);

  private:

    // The first chunk of a request, before any submit has been timed. Also capped in worst-case
    // rays, light candidate evaluations included, so a heavy pass starts with fewer points.
    static constexpr uint32_t INITIAL_CHUNK_POINTS = 256;
    static constexpr uint32_t INITIAL_CHUNK_RAYS = 1u << 20;
    // Wall time a submit is steered toward, wait included. Far enough under the 2 s Windows TDR
    // that a chunk overshooting it several times over still survives.
    static constexpr double TARGET_SUBMIT_SECONDS = 0.15;
    // Growth waits for several fast submits in a row and is capped per step, because a fast
    // measurement can be noise; shrinking is immediate, since a slow one is the actual risk.
    static constexpr double MAX_CHUNK_GROWTH = 1.5;
    static constexpr uint32_t GROW_AFTER_UNDER_TARGET_SUBMITS = 2;
    // Submits whose slowest rate the next chunk is sized by.
    static constexpr size_t COST_HISTORY_SUBMITS = 4;
    static constexpr double PROGRESS_LOG_INTERVAL_SECONDS = 2.0;

    struct DispatchDesc
    {
      uint32_t mode = PROBE_BAKE_MODE_GEOMETRY;
      uint32_t samplesPerPass = 0;
      // Per point over all passes.
      uint32_t totalSamples = 0;
      uint32_t passCount = 0;
      // Worst case, for the submit caps: rays plus light candidate evaluations counted as rays.
      uint32_t workPerSample = 1;
      int32_t maxBounces = 0;
      float fireflyClamp = 0.0f;
      bool glass = false;
      int32_t maxTransmissionDepth = 1;
      int32_t glassOverflow = PT_GLASS_OVERFLOW_STRAIGHT;
      int32_t secondaryGlass = PT_GLASS_STRAIGHT;
      const char* label = "";
    };

    // Receives one finished pass over one block: records[i] belongs to points[pointIndices[i]].
    // A block's passes arrive in order, 0 to passCount - 1, before the next block starts. Both
    // spans stay valid only for the call.
    using PassCollector = std::function<void(uint32_t pass, std::span<const uint32_t> pointIndices,
      std::span<const ProbeBakeRecord> records)>;

    bool CheckReady(const char* call) const;
    void WriteFrameUniforms();
    void Dispatch(std::span<const ProbeBakePoint> points, const DispatchDesc& desc,
      const PassCollector& collect);
    void ReleaseBlockBuffers();

    Render* m_Render = nullptr;
    const RenderContext* m_Ctx = nullptr;

    // Owned, and hot reloaded, by the render's pipeline cache.
    PipelineHandle m_Pipeline {};
    FrameUniformBuffer m_FrameUBO;
    VulkanDescriptorSet m_DescriptorSet;
    VulkanBuffer m_LightBuffer;
    // Sized to one block of the request and released when the request returns.
    VulkanBuffer m_PointBuffer;
    VulkanBuffer m_RecordBuffer;
    VulkanBuffer m_ReadbackBuffer;
  };
}
