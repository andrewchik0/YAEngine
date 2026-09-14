#ifdef __cplusplus
#pragma once
#define uint uint32_t
#define ivec3 glm::ivec3
#define vec3 glm::vec3
#define vec4 glm::vec4
namespace YAEngine {
#endif

// ProbeBakeConstants::mode. Geometry traces and records what the rays hit without resolving a
// single material; integrate records the same and gathers radiance on top.
#define PROBE_BAKE_MODE_GEOMETRY  0u
#define PROBE_BAKE_MODE_INTEGRATE 1u

// One dispatch of probe_bake.rgen: one pass over a chunk of the points currently in the buffers.
struct ProbeBakeConstants
{
  uint mode;
  // The chunk is points [firstPoint, firstPoint + pointCount) of the current block. One trace
  // launch per point; gl_LaunchIDEXT.x is the offset from firstPoint.
  uint firstPoint;
  uint pointCount;
  // Seeds the pass's direction rotation, and with the sample index every sample's path stream,
  // together with each point's seed key.
  uint passIndex;
  uint samplesPerPass;
  int maxBounces;
  // 0 = off, see clampContribution in pt_path.glsl.
  float fireflyClamp;
  uint _pad0;
};

// One point to trace from, 32 bytes. The seed key and the pass index are all its random streams
// depend on, so a point bakes the same samples whatever it is dispatched together with: a lattice
// probe passes its integer lattice coordinate, a geometry query any integer triple of its own.
struct ProbeBakePoint
{
  vec3 position;
  // Rays whose first hit lies nearer than this are counted in ProbeBakeRecord::closeHitCount; zero
  // counts none.
  float closeHitDistance;
  ivec3 seedKey;
  int _pad0;
};

// What one point gathered in one pass, 96 bytes. Written afresh by every pass and read back after
// it: the CPU sums and min-merges the passes in double, so a float here never holds more than one
// pass's samples. A distance of zero means nothing was recorded - no real hit lies closer than
// PT_RAY_TMIN.
struct ProbeBakeRecord
{
  // Raw L1 projection sums per channel as l0, l1x, l1y, l1z: SHL1Accumulator::coefficients, basis
  // applied and cosine lobe not.
  vec4 shR;
  vec4 shG;
  vec4 shB;
  // SHL1Accumulator::totalSolidAngle. Every sample adds one: for directions uniformly distributed
  // on the sphere, Finalize's 4 pi / N is then exactly the Monte Carlo estimator.
  float weightSum;
  uint rayCount;
  // Hits on the back face of an instance that is not double sided.
  uint backfaceCount;
  // Of either facing.
  float nearestHitDistance;
  // Unit length, world space; zero while nothing was recorded.
  vec3 nearestBackfaceDirection;
  float nearestBackfaceDistance;
  // Rays whose first hit, of either facing, lies nearer than ProbeBakePoint::closeHitDistance.
  uint closeHitCount;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#undef vec3
#undef ivec3
#undef uint
#endif
