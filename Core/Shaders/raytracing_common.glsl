// Everything every ray tracing shader in the engine shares: the per-frame scene bindings,
// the mesh streams an instance record points at, and how a hit resolves into geometry.
//
// No version or extension directives here on purpose. Tools/CompileShaders pastes an include
// into its consumer, then prepends its own version line after cutting the expanded text at the
// first version directive that opens a line - with everything ahead of that line, so a real one
// here would silently drop the consumer's extension directives and every declaration before the
// include. A mention after a line comment marker opens no line and is harmless, but one opening
// a line inside a block comment is still misread, so prose keeps not spelling the directive
// out. Extension directives are not scanned for at all: enabling them is the consumer's job.
// Every consumer enables GL_EXT_buffer_reference, GL_EXT_buffer_reference2,
// GL_EXT_shader_explicit_arithmetic_types_int64 and GL_EXT_ray_tracing itself, plus
// GL_EXT_nonuniform_qualifier when it defines RT_BINDLESS.

#include "common.glsl"
#include "../Shared/RayTracingInstanceData.h"
#include "../Shared/RayTracingMaterialData.h"

// Set 1: the per-frame ray tracing scene. Bindings 0-2 are what every ray tracing pass
// reads and are laid out the same everywhere; bindings from 3 up belong to the pass.
layout(set = 1, binding = 0) uniform accelerationStructureEXT u_Tlas;

layout(std430, set = 1, binding = 1) readonly buffer RayTracingInstanceSSBO
{
  RayTracingInstanceRecord u_Instances[];
};

layout(std430, set = 1, binding = 2) readonly buffer RayTracingMaterialSSBO
{
  RayTracingMaterialRecord u_Materials[];
};

#ifdef RT_BINDLESS
// Set 2: the global bindless texture table. Never rebuilt between frames, so it is bound
// once and left alone.
layout(set = 2, binding = 0) uniform sampler2D u_BindlessTextures[];
#endif

// Both mesh streams are fetched one scalar at a time. An std430 array of vec3 carries a
// 16 byte stride, and the interleaved stream packs its positions tightly at 12. The same
// reference serves the attribute block further into the buffer, which is why it is named
// for the buffer and not for the positions.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer VertexStream
{
  float data[];
};

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer IndexStream
{
  uint data[];
};

// The cutout threshold the raster alpha-test path uses, so an any-hit shader keeps or
// drops exactly the texels the G-buffer pass would have.
const float RT_ALPHA_CUTOFF = 0.5;

vec3 fetchPosition(VertexStream vertices, uint vertexIndex)
{
  uint base = vertexIndex * 3u;
  return vec3(vertices.data[base], vertices.data[base + 1u], vertices.data[base + 2u]);
}

// The attribute block is {vec2 tex, vec3 normal, vec4 tangent} at a 36 byte stride,
// starting attributeOffset bytes into the same buffer. attributeOffset is 12 * vertexCount
// and so always a multiple of four, which is what lets the float indexing below be exact.
vec2 fetchTexCoord(VertexStream vertices, uint attributeOffset, uint vertexIndex)
{
  uint base = attributeOffset / 4u + vertexIndex * 9u;
  return vec2(vertices.data[base], vertices.data[base + 1u]);
}

// Indices are always 32-bit and a triangle is always three consecutive ones.
uvec3 fetchTriangle(IndexStream indices, uint primitiveIndex)
{
  uint firstIndex = primitiveIndex * 3u;
  return uvec3(indices.data[firstIndex], indices.data[firstIndex + 1u], indices.data[firstIndex + 2u]);
}

vec2 interpolateTexCoord(VertexStream vertices, uint attributeOffset, uvec3 triIndices, vec2 barycentrics)
{
  vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
  return weights.x * fetchTexCoord(vertices, attributeOffset, triIndices.x)
       + weights.y * fetchTexCoord(vertices, attributeOffset, triIndices.y)
       + weights.z * fetchTexCoord(vertices, attributeOffset, triIndices.z);
}

// The camera ray for one pixel, using the unprojection the sky path of
// deferred_lighting.frag uses: under reversed-Z the near plane is NDC z = 1, and z = 0 is
// infinity and unprojects to w = 0. The camera jitter baked into proj is left in.
void primaryRay(ivec2 pixel, out vec3 rayOrigin, out vec3 rayDirection)
{
  vec2 uv = (vec2(pixel) + 0.5) / vec2(u_Frame.screenWidth, u_Frame.screenHeight);
  vec2 ndc = uv * 2.0 - 1.0;

  vec4 viewPos = u_Frame.invProj * vec4(ndc, 1.0, 1.0);
  rayDirection = normalize(mat3(u_Frame.invView) * viewPos.xyz);
  rayOrigin = u_Frame.cameraPosition;
}

// What a hit or miss shader hands back to the ray generation shader. Deliberately thin and
// deliberately unshaded: keeping the shading in the ray generation shader is what lets the
// path tracer grow its bounce loop there without a hit shader having to learn about it.
struct RayTracingPayload
{
  vec2 barycentrics;
  // instanceCustomIndex, which is the RayTracingInstanceRecord slot of the hit.
  uint instanceIndex;
  uint primitiveIndex;
  // Zero when the miss shader ran. Set by the closest hit shader and by nothing else, so
  // the ray generation shader has to clear it before every trace.
  uint hit;
  // Nonzero when the committed hit is on the side the raster pipeline treats as its front
  // face; meaningful only while hit is set. Facing is judged on object space winding, so a
  // mirrored instance keeps its authored front where the raster, judging after the
  // transform, sees the other side.
  uint frontFacing;
  // Parametric distance along the ray to the committed hit, gl_HitTEXT verbatim. Carried
  // separately from the barycentric position because a probe ray that only needs to know how
  // far it got should not have to fetch a triangle to find out.
  float hitT;
  mat4x3 objectToWorld;
};

// The second payload the path tracer traces with, at its own location, because a shadow ray
// carries nothing but "did I reach the light". It is traced with TerminateOnFirstHit and
// SkipClosestHitShader, so the only shader that can ever write this is the shadow miss one -
// which is exactly what makes the answer one bit. The any-hit is deliberately NOT skipped:
// it is the alpha cutout, and without it every foliage card would cast a solid shadow.
struct ShadowRayPayload
{
  // Zero unless the shadow miss shader ran, so the ray generation shader clears it before
  // every trace and reads it as the visibility term afterwards.
  uint visible;
};

// Everything a hit turns into before anything shades it. The lazy part is deliberate: the
// texture coordinate costs an interpolation nothing but a textured surface wants, so the
// stream and the offset it needs travel in the record and hitTexCoord() fetches it on
// demand.
struct RayHitGeometry
{
  vec3 position;
  // Geometric, and already flipped to face the incoming ray. A shading normal from the
  // attribute block would be a second field here, not a replacement for this one: the ray
  // offset and the shadow ray both need the geometric one whatever shading uses.
  vec3 normal;
  uvec3 triIndices;
  uint64_t vertexAddress;
  uint attributeOffset;
  uint materialIndex;
  uint flags;
};

// Resolves one committed triangle hit into world space from the instance record alone. The
// barycentrics are the fixed-function ones, so the interpolated position is the exact point
// traversal reported rather than origin + t * direction, which drifts with t.
RayHitGeometry resolveHitGeometry(uint recordIndex, uint primitiveIndex, mat4x3 objectToWorld,
  vec2 barycentrics, vec3 rayDirection)
{
  RayTracingInstanceRecord instance = u_Instances[recordIndex];
  VertexStream vertices = VertexStream(instance.vertexAddress);
  IndexStream indices = IndexStream(instance.indexAddress);

  uvec3 triIndices = fetchTriangle(indices, primitiveIndex);

  vec3 p0 = objectToWorld * vec4(fetchPosition(vertices, triIndices.x), 1.0);
  vec3 p1 = objectToWorld * vec4(fetchPosition(vertices, triIndices.y), 1.0);
  vec3 p2 = objectToWorld * vec4(fetchPosition(vertices, triIndices.z), 1.0);

  // Crossed after the transform rather than before it, so no inverse transpose is
  // needed and a non-uniform scale cannot skew the normal.
  vec3 normal = normalize(cross(p1 - p0, p2 - p0));
  if (dot(normal, rayDirection) > 0.0)
    normal = -normal;

  vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

  RayHitGeometry hit;
  hit.position = weights.x * p0 + weights.y * p1 + weights.z * p2;
  hit.normal = normal;
  hit.triIndices = triIndices;
  hit.vertexAddress = instance.vertexAddress;
  hit.attributeOffset = instance.attributeOffset;
  hit.materialIndex = instance.materialIndex;
  hit.flags = instance.flags;
  return hit;
}

// attributeOffset of zero means the mesh carries positions alone, so there is no texture
// coordinate to interpolate and nothing may be sampled with the result.
vec2 hitTexCoord(RayHitGeometry hit, vec2 barycentrics)
{
  if (hit.attributeOffset == 0u)
    return vec2(0.0);

  return interpolateTexCoord(VertexStream(hit.vertexAddress), hit.attributeOffset,
    hit.triIndices, barycentrics);
}
