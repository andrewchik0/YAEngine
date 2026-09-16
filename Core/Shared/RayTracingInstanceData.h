#ifdef __cplusplus
#pragma once
#define uint uint32_t
#define vec4 glm::vec4
namespace YAEngine {
#endif

// Ray mask bits. An instance is added to exactly one of the first three, and a trace only sees the
// instances whose mask it shares a bit with. The two glass bits hold only the transparent surfaces
// whose material is PT-transmissive, split by what the path tracer may do with them: Sheet and
// ThinWalled never bend and sit in RT_MASK_GLASS_STRAIGHT, Solid can refract and sits in
// RT_MASK_GLASS_REFRACTIVE, so one trace can leave out exactly the classes a segment crosses
// straight. A transparent material without transmission is raster-only and has no TLAS instance at
// all - only its instance record, which the emissive light table may name.
// RT_MASK_MOVING is added on top of RT_MASK_OPAQUE for an opaque instance whose transform changed since
// the previous frame, so the reflector lookup can trace the moving instances alone.
#define RT_MASK_OPAQUE           0x01u
#define RT_MASK_GLASS_STRAIGHT   0x02u
#define RT_MASK_GLASS_REFRACTIVE 0x08u
#define RT_MASK_MOVING           0x10u
#define RT_MASK_GLASS (RT_MASK_GLASS_STRAIGHT | RT_MASK_GLASS_REFRACTIVE)
// What path and shadow rays trace.
#define RT_MASK_PATH (RT_MASK_OPAQUE | RT_MASK_GLASS)

// RayTracingInstanceRecord::flags. These mirror the RenderObject booleans a raster draw
// resolves into pipeline state - a hit has no pipeline to read them from.
#define RT_INSTANCE_ALPHA_TEST   0x01u
#define RT_INSTANCE_TRANSPARENT  0x02u
#define RT_INSTANCE_TERRAIN      0x04u
#define RT_INSTANCE_UNLIT        0x08u
#define RT_INSTANCE_DOUBLE_SIDED 0x10u
// A PT-transmissive surface, met by the path tracer as a perfectly smooth dielectric - see
// TransmissionMode: a sheet crossed as a slab, one wall of a closed thin vessel, or the boundary of
// a refracting medium. The first two are the straight classes.
#define RT_INSTANCE_SHEET_DIELECTRIC       0x20u
#define RT_INSTANCE_SOLID_DIELECTRIC       0x40u
#define RT_INSTANCE_THIN_WALLED_DIELECTRIC 0x80u
#define RT_INSTANCE_STRAIGHT_DIELECTRIC (RT_INSTANCE_SHEET_DIELECTRIC | RT_INSTANCE_THIN_WALLED_DIELECTRIC)
#define RT_INSTANCE_DIELECTRIC (RT_INSTANCE_STRAIGHT_DIELECTRIC | RT_INSTANCE_SOLID_DIELECTRIC)

// RayTracingInstanceRecord::emissiveIndex of an instance the emissive light table does not hold.
#define RT_INSTANCE_NOT_EMISSIVE 0xFFFFFFFFu

// Everything a hit needs that the acceleration structure itself does not carry. One
// record per TLAS instance, in TLAS instance order, addressed by the instance's
// instanceCustomIndex (gl_InstanceCustomIndexEXT).
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
  // This instance's slot in the emissive light table (Shared/EmissiveLightData.h), or
  // RT_INSTANCE_NOT_EMISSIVE.
  uint emissiveIndex;
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
