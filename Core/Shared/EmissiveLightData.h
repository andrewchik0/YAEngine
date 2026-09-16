#ifdef __cplusplus
#pragma once
#define uint uint32_t
#define vec4 glm::vec4
namespace YAEngine {
#endif

// EmissiveLightRecord::flags. The instance is transparent but not glass to the path tracer - no
// transmission mode, or the PT Glass switch off - and has no TLAS instance, only its instance record,
// so a BSDF sample can never reach it and next event estimation is its only strategy: MIS weight one.
// Glass a bounce ray would cross straight is the same case, decided per vertex in estimateDirectLight.
#define EMISSIVE_LIGHT_NEE_ONLY 0x01u

// The emissive light table next event estimation samples emitting geometry from. TlasBuilder rebuilds
// it every frame, in the same pass and from the same snapshot transforms as the instance records,
// which is what lets a moving emitter light the scene where it is now. One std430 buffer: the header,
// then one record per emissive instance record, addressed by RayTracingInstanceRecord::emissiveIndex.
//
// std430 layout, identical on both sides:
//  - the header holds four scalars, aligns to 4 and is 16 bytes, so the record array - whose element
//    aligns to 16 because of its vec4 rows - starts right after it at offset 16;
//  - a record keeps its four scalars at 0-15, the three rows at 16, 32 and 48, and the alias pair and
//    two padding words at 64-79: 80 bytes, a multiple of its 16 byte alignment, so the stride is 80.
struct EmissiveLightTableHeader
{
  uint count;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

struct EmissiveLightRecord
{
  // The RayTracingInstanceRecord slot: positions, indices and the material come from there.
  uint instanceIndex;
  // Index count / 3 of the interleaved stream the bottom level structure was built over.
  uint triangleCount;
  uint flags;
  // Probability that one draw from the alias table below yields this record, recomputed from the
  // stored thresholds so it describes the table the shader actually samples.
  float pmf;
  // Top three rows of the row-major object to world matrix, the convention of
  // RayTracingInstanceRecord::worldToPrevWorld.
  vec4 objectToWorld[3];
  // Vose alias table: slot i yields itself for a uniform below aliasThreshold, aliasIndex otherwise.
  float aliasThreshold;
  uint aliasIndex;
  uint _pad0;
  uint _pad1;
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#undef uint
#endif
