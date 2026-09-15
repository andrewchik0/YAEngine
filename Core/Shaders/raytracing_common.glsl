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

vec3 fetchNormal(VertexStream vertices, uint attributeOffset, uint vertexIndex)
{
  uint base = attributeOffset / 4u + vertexIndex * 9u;
  return vec3(vertices.data[base + 2u], vertices.data[base + 3u], vertices.data[base + 4u]);
}

vec4 fetchTangent(VertexStream vertices, uint attributeOffset, uint vertexIndex)
{
  uint base = attributeOffset / 4u + vertexIndex * 9u;
  return vec4(vertices.data[base + 5u], vertices.data[base + 6u], vertices.data[base + 7u],
    vertices.data[base + 8u]);
}

// Zero for a vector with no usable length - zero, infinite or NaN - rather than the NaN normalize
// would make of it.
vec3 normalizeOrZero(vec3 v)
{
  float length2 = dot(v, v);
  return length2 > 0.0 && !isinf(length2) ? v * inversesqrt(length2) : vec3(0.0);
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
// carries nothing but how much of the light it reached - and a glass transmittance query nothing but
// how much of a segment gets through. Both skip the closest hit shader, so the only shaders that can
// ever write this are the shadow miss one, which says the far end was reached at all, and
// pt_shadow.rahit, which is deliberately NOT skipped: it is the alpha cutout - without it every
// foliage card would cast a solid shadow - and it attenuates the ray through every dielectric it
// crosses. The ray generation shader resets every field before each trace.
struct ShadowRayPayload
{
  // Zero unless the shadow miss shader ran.
  uint visible;
  // Nonzero once the ray crossed the boundary of a solid medium.
  uint crossedSolid;
  // What the dielectrics crossed let through at their interfaces.
  vec3 transmittance;
  // Absorption through solid media, order independent: a crossing into a medium subtracts
  // absorption * t and one out of it adds the same, so any traversal order sums to the absorption
  // over the length travelled inside.
  vec3 opticalDepth;
};

// Everything a hit turns into before anything shades it. The lazy part is deliberate: the
// texture coordinate costs an interpolation nothing but a textured surface wants, so the
// stream and the offset it needs travel in the record and hitTexCoord() fetches it on
// demand.
struct RayHitGeometry
{
  vec3 position;
  // Geometric, and already flipped to face the incoming ray. Ray offsets, facing and the test
  // for a direction below the surface use this one whatever shading uses.
  vec3 normal;
  // The attribute block's normal and tangent frame, interpolated - see interpolateShadingFrame -
  // and not flipped: what the material's normal map is decoded in. vertexNormal is the geometric
  // normal, and the tangent frame zero, where the mesh has no attribute block.
  vec3 vertexNormal;
  vec3 tangent;
  vec3 bitangent;
  // vertexNormal on the side of normal: the shading normal of a surface without a normal map.
  vec3 shadingNormal;
  uvec3 triIndices;
  uint64_t vertexAddress;
  uint attributeOffset;
  uint materialIndex;
  uint flags;
  // World space area of the hit triangle, which is what an emitter hit weighs its MIS with.
  float area;
  // Whether the ray arrived at the raster front face (isRasterFrontFace), judged before the
  // normal above is flipped toward the ray.
  bool frontFace;
};

// The face raster draws a single sided mesh from. Every mesh pipeline keeps COUNTER_CLOCKWISE
// front faces and culls the back ones, the projection is right-handed with Y flipped for Vulkan,
// and nothing compensates for a transform with a negative determinant. So the front is the side
// cross(p1 - p0, p2 - p0) of the WORLD space vertices points to - for a mirrored instance too,
// which raster then shows from its authored back - and a ray arrives at it when it runs against
// that cross. Not gl_HitKindEXT, which judges winding in object space; see RayTracingPayload.
bool isRasterFrontFace(vec3 worldCross, vec3 rayDirection)
{
  return dot(worldCross, rayDirection) < 0.0;
}

// Emission is one sided the way raster shows it: a single sided instance emits from its front
// face only, one raster draws without culling from both - double sided materials, and alpha
// tested and unlit ones, whose G-buffer pipelines always cull nothing. The back face is still
// geometry - it occludes and reflects like any surface. Traced hits and emissive light samples
// both decide through here, so the two halves of an emitter's MIS weight always describe the
// same emitter.
bool emitsFromFace(uint instanceFlags, bool frontFace)
{
  const uint twoSided = RT_INSTANCE_DOUBLE_SIDED | RT_INSTANCE_ALPHA_TEST | RT_INSTANCE_UNLIT;
  return frontFace || (instanceFlags & twoSided) != 0u;
}

// A point through the top three rows of a row-major affine matrix, the layout of
// RayTracingInstanceRecord::worldToPrevWorld and of the emissive light table's objectToWorld.
vec3 transformPointByRows(vec4 rows[3], vec3 point)
{
  vec4 homogeneous = vec4(point, 1.0);
  return vec3(dot(rows[0], homogeneous), dot(rows[1], homogeneous), dot(rows[2], homogeneous));
}

// transpose(inverse(m)) scaled by |det m|: the cofactor matrix with the determinant's sign. Every
// normal carried through it is normalized afterwards, so the scale is free and nothing is divided.
mat3 normalMatrixOf(mat3 m)
{
  mat3 cofactor = mat3(cross(m[1], m[2]), cross(m[2], m[0]), cross(m[0], m[1]));
  return dot(m[0], cofactor[0]) < 0.0 ? -cofactor : cofactor;
}

// The attribute block's vertex normals through normalMatrix, each normalized, then interpolated -
// the normal half of interpolateShadingFrame. Not normalized.
vec3 interpolateVertexNormal(VertexStream vertices, uint attributeOffset, uvec3 triIndices,
  vec3 weights, mat3 normalMatrix)
{
  vec3 normal = vec3(0.0);
  for (int i = 0; i < 3; i++)
    normal += weights[i] * normalizeOrZero(normalMatrix * fetchNormal(vertices, attributeOffset, triIndices[i]));
  return normal;
}

// A hit's normal and tangent frame, built the way mesh.vert builds them for the G-buffer: each
// vertex's normal and tangent through the normal matrix, the tangent made orthogonal to the normal,
// the bitangent from their cross product and the tangent's handedness - and then interpolated. The
// G-buffer decodes the normal map in this frame, so a traced surface bends its normal as the
// rasterized one does. None of the three is normalized.
void interpolateShadingFrame(VertexStream vertices, uint attributeOffset, uvec3 triIndices,
  vec3 weights, mat3 normalMatrix, out vec3 normal, out vec3 tangent, out vec3 bitangent)
{
  normal = vec3(0.0);
  tangent = vec3(0.0);
  bitangent = vec3(0.0);

  for (int i = 0; i < 3; i++)
  {
    vec3 vertexNormal = normalizeOrZero(normalMatrix * fetchNormal(vertices, attributeOffset, triIndices[i]));
    vec4 vertexTangent = fetchTangent(vertices, attributeOffset, triIndices[i]);
    vec3 worldTangent = normalizeOrZero(normalMatrix * vertexTangent.xyz);
    worldTangent = normalizeOrZero(worldTangent - vertexNormal * dot(worldTangent, vertexNormal));

    normal += weights[i] * vertexNormal;
    tangent += weights[i] * worldTangent;
    bitangent += weights[i] * cross(vertexNormal, worldTangent) * vertexTangent.w;
  }
}

// A hit's interpolated vertex normal in world space, unflipped, or its geometric normal where the
// mesh has no attribute block: the part of resolveHitGeometry an any-hit shader needs for an angle.
// normalMatrix is any multiple of the instance's inverse transpose - an any-hit shader has
// transpose(mat3(gl_WorldToObjectEXT)) without inverting anything. The geometric normal is crossed
// in object space and carried through it, so no position is transformed; its sign may flip with the
// multiple, which an angle does not see.
vec3 hitVertexNormal(uint recordIndex, uint primitiveIndex, mat3 normalMatrix, vec2 barycentrics)
{
  RayTracingInstanceRecord instance = u_Instances[recordIndex];
  VertexStream vertices = VertexStream(instance.vertexAddress);
  uvec3 triIndices = fetchTriangle(IndexStream(instance.indexAddress), primitiveIndex);

  if (instance.attributeOffset != 0u)
  {
    vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    vec3 normal = normalizeOrZero(interpolateVertexNormal(vertices, instance.attributeOffset, triIndices,
      weights, normalMatrix));
    if (dot(normal, normal) > 0.0)
      return normal;
  }

  vec3 p0 = fetchPosition(vertices, triIndices.x);
  vec3 p1 = fetchPosition(vertices, triIndices.y);
  vec3 p2 = fetchPosition(vertices, triIndices.z);
  return normalizeOrZero(normalMatrix * cross(p1 - p0, p2 - p0));
}

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
  vec3 crossed = cross(p1 - p0, p2 - p0);
  vec3 normal = normalize(crossed);
  bool frontFace = isRasterFrontFace(crossed, rayDirection);
  if (dot(normal, rayDirection) > 0.0)
    normal = -normal;

  vec3 weights = vec3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

  RayHitGeometry hit;
  hit.position = weights.x * p0 + weights.y * p1 + weights.z * p2;
  hit.normal = normal;
  hit.vertexNormal = normal;
  hit.tangent = vec3(0.0);
  hit.bitangent = vec3(0.0);
  hit.shadingNormal = normal;

  if (instance.attributeOffset != 0u)
  {
    mat3 normalMatrix = normalMatrixOf(mat3(objectToWorld));
    // The tangent frame exists to decode a normal map, so a material without one never builds it.
    bool normalMapped = instance.materialIndex < uint(u_Materials.length())
      && (u_Materials[instance.materialIndex].textureMask & RT_MATERIAL_NORMAL) != 0u;

    vec3 vertexNormal;
    if (normalMapped)
      interpolateShadingFrame(vertices, instance.attributeOffset, triIndices, weights, normalMatrix,
        vertexNormal, hit.tangent, hit.bitangent);
    else
      vertexNormal = interpolateVertexNormal(vertices, instance.attributeOffset, triIndices, weights,
        normalMatrix);
    vertexNormal = normalizeOrZero(vertexNormal);

    // An unset attribute normal, or a singular transform, keeps the geometric one.
    if (dot(vertexNormal, vertexNormal) > 0.0)
    {
      hit.vertexNormal = vertexNormal;
      hit.shadingNormal = dot(vertexNormal, normal) < 0.0 ? -vertexNormal : vertexNormal;
    }
  }
  hit.triIndices = triIndices;
  hit.vertexAddress = instance.vertexAddress;
  hit.attributeOffset = instance.attributeOffset;
  hit.materialIndex = instance.materialIndex;
  hit.flags = instance.flags;
  hit.area = 0.5 * length(crossed);
  hit.frontFace = frontFace;
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

#ifdef RT_BINDLESS
// True where pathtrace.rahit discards the candidate: the texel does not exist for a ray. Shared with
// the path tracer's emissive light sampling, which must not light the scene from a texel no ray hits.
bool isAlphaCutout(RayTracingInstanceRecord instance, uint primitiveIndex, vec2 barycentrics)
{
  // A mesh with no attribute block carries positions alone, so there is no texture
  // coordinate to cut out with, and an out-of-range material slot cannot be read at all.
  if (instance.attributeOffset == 0u || instance.materialIndex >= uint(u_Materials.length()))
    return false;

  RayTracingMaterialRecord material = u_Materials[instance.materialIndex];
  // Without a base color map the alpha is the material's own, which the cutout path never
  // uses to discard - accepting the candidate is what the raster does there too.
  if ((material.textureMask & RT_MATERIAL_BASE_COLOR) == 0u)
    return false;

  IndexStream indices = IndexStream(instance.indexAddress);
  VertexStream vertices = VertexStream(instance.vertexAddress);

  uvec3 triIndices = fetchTriangle(indices, primitiveIndex);
  vec2 texCoord = interpolateTexCoord(vertices, instance.attributeOffset, triIndices, barycentrics);

  // Mip 0 explicitly: a ray tracing invocation has no derivatives, so an implicit LOD
  // would be undefined here.
  float alpha = textureLod(u_BindlessTextures[nonuniformEXT(material.baseColorIndex)],
    texCoord * material.uvScale, 0.0).a;

  return alpha < RT_ALPHA_CUTOFF;
}
#endif
