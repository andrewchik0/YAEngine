#ifdef __cplusplus
#pragma once
#define uint uint32_t
#define vec4 glm::vec4
namespace YAEngine {
#endif

// Ray mask bits. An instance is added to exactly one of them, and a trace only sees the
// instances whose mask it shares a bit with. v1 rays trace RT_MASK_OPAQUE; transparent
// surfaces are kept addressable so a later pass can opt into them without a second TLAS.
#define RT_MASK_OPAQUE      0x01u
#define RT_MASK_TRANSPARENT 0x02u

// RayTracingInstanceRecord::flags. These mirror the RenderObject booleans a raster draw
// resolves into pipeline state - a hit has no pipeline to read them from.
#define RT_INSTANCE_ALPHA_TEST   0x01u
#define RT_INSTANCE_TRANSPARENT  0x02u
#define RT_INSTANCE_TERRAIN      0x04u
#define RT_INSTANCE_UNLIT        0x08u
#define RT_INSTANCE_DOUBLE_SIDED 0x10u

// Everything a hit needs that the acceleration structure itself does not carry. One
// record per TLAS instance, in TLAS instance order, addressed by the instance's
// instanceCustomIndex (gl_InstanceCustomIndexEXT, or
// rayQueryGetIntersectionInstanceCustomIndexEXT for a ray query).
//
// GEOMETRY LAYOUT the two addresses point at - C++ and GLSL must agree on this exactly.
// Both name the mesh's INTERLEAVED stream, which is what its bottom level structure was
// built over; the welded arena stream indexes differently and must never be fetched with
// these indices. Inside the vertex buffer the mesh is split, not interleaved:
//
//   vertexAddress + 12 * index                     -> vec3 position
//   vertexAddress + attributeOffset + 36 * index   -> vec2 tex, vec3 normal, vec4 tangent
//
// attributeOffset is the byte offset of the attribute block and equals 12 * vertexCount.
// Zero means the mesh carries no separate attribute block, so positions alone are
// fetchable. Indices are always 32-bit and always start at indexAddress.
//
// A GLSL consumer must enable GL_EXT_shader_explicit_arithmetic_types_int64 for the two
// addresses, and reaches the data through GL_EXT_buffer_reference.
struct RayTracingInstanceRecord
{
  uint64_t vertexAddress;
  uint64_t indexAddress;
  uint attributeOffset;
  // Slot index of the MaterialHandle this instance draws with, which is also its index
  // into the RayTracingMaterialRecord table (Shared/RayTracingMaterialData.h) - that table
  // is addressed by material slot precisely so this field serves as both.
  uint materialIndex;
  uint flags;
  uint _pad0;
  // Maps a point of this instance from this frame's world space to where it was last frame,
  // as the top three rows of a row-major affine matrix - VkTransformMatrixKHR's layout. Rows
  // of vec4 rather than vec3 columns, which std430 would pad to a 16 byte stride anyway.
  // Offsets 32/48/64 keep the whole record at 80 bytes, its std430 stride.
  vec4 worldToPrevWorld[3];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#undef uint
#endif
