#define TONEMAP_ACES 0
#define TONEMAP_AGX  1

// Debug view ids. Utils/DebugViews.cpp is the table that names, groups and orders them for
// the viewport toolbar, frame capture and the agent bridge. Only the views a shader or one of
// the shared macros below names need to be spelled out; the rest stay positional.
#define DEBUG_VIEW_AO                5
#define DEBUG_VIEW_SSR               6
#define DEBUG_VIEW_TAA_DELTA         8
#define DEBUG_VIEW_AMBIENT_ONLY     10
#define DEBUG_VIEW_AMBIENT_DIFFUSE  11
#define DEBUG_VIEW_AMBIENT_SPECULAR 12
#define DEBUG_VIEW_PROBE_INDEX      13
#define DEBUG_VIEW_PROBE_FALLBACK   14
#define DEBUG_VIEW_VOLUME_COVERAGE  15
#define DEBUG_VIEW_SSGI_VALIDITY    16
#define DEBUG_VIEW_SSGI_SCREEN      17
#define DEBUG_VIEW_SSGI_FALLBACK    18
#define DEBUG_VIEW_DIRECT_ONLY      19
// The path tracer's own two views, both written by pt_main.rgen. Noisy is this frame's
// single sample; Reference is the running mean the same pass keeps while the camera and the
// scene hold still. Both carry HDR scene radiance, so the tonemap pass runs them through the
// normal exposure and tone mapping operator.
#define DEBUG_VIEW_PT_NOISY         20
#define DEBUG_VIEW_PT_REFERENCE     21
// The three guide buffers ray reconstruction consumes, in one 2x2 tiled image: the two
// demodulation albedos on top, the world normal and the roughness underneath. Written by
// pt_guides.comp, which only runs while the path tracing render path is effective.
#define DEBUG_VIEW_PT_GUIDES        22
// Where the path tracer's energy comes from, on a log scale, written into PathTraceNoisy in
// place of radiance while PathTraceConstants::debugMode is not zero. Three separate questions,
// one per view: which bounce produced the largest single contribution and how large it was,
// how much next event estimation added in total, and how much the environment added on a
// bounce miss. Values are measured BEFORE the firefly clamp, which is the whole point - the
// clamp is what hides the number that has to be read.
#define DEBUG_VIEW_PT_MAX_CONTRIB   23
#define DEBUG_VIEW_PT_NEE           24
#define DEBUG_VIEW_PT_ENVIRONMENT   25
// Where the first NaN or Inf entered the path, as a PT_NF_* code rather than a magnitude.
// The three views above answer "how much"; this one answers "from which expression", which
// is the question a value that has already become NaN can no longer be asked. It is the only
// path tracing view whose image is NOT on the log ramp - see PT_NF_* below.
#define DEBUG_VIEW_PT_NONFINITE     26

// The magnitude of the final HDR colour, on the same logarithmic grey scale as the three views
// above and sampled from the same resolved image the tone map consumes. It therefore works in
// BOTH render paths, which is the entire point: switching Raster and Path Tracing on one
// camera reads two numbers off one scale and answers how much energy each path puts on a
// surface. Comparing a path traced diagnostic against a rasterized image by eye cannot.
#define DEBUG_VIEW_HDR_MAGNITUDE    27

// The specular motion vectors pt_main.rgen writes for ray reconstruction, on the Velocity view's
// scale. Like PT Guides it only exists while the path tracing render path is effective.
#define DEBUG_VIEW_PT_SPECULAR_MOTION 28

// The spacing level of the brick the innermost contributing irradiance volume was sampled in,
// finest red to coarsest blue as the placement brick gizmo, black = skybox. Written by deferred
// lighting like Volume Coverage.
#define DEBUG_VIEW_VOLUME_LEVEL     29

// PathTraceConstants::debugMode, and the order the views above map onto it.
#define PT_DEBUG_OFF          0
#define PT_DEBUG_MAX_CONTRIB  1
#define PT_DEBUG_NEE          2
#define PT_DEBUG_ENVIRONMENT  3
#define PT_DEBUG_NONFINITE    4

// PT_DEBUG_NONFINITE codes. The tracer stores the FIRST one that fired in R of the noisy
// image and the bounce it fired on in G, both as raw integers so a capture reads them back
// exactly; G is -1 where nothing fired. Every test is an explicit isnan/isinf, never a
// comparison - a comparison against NaN is false, which is how PT Max Contribution used to
// go blind to the very value it was written to find.
//
//   0  nothing, the path stayed finite
//   1  estimateDirectLight return
//   2  importanceSampleGGX half vector H
//   3  NdotL      | in sampleBrdfDirection's GGX branch, all three recorded BEFORE the
//   4  NdotH      | sign test that would reject them, because a NaN passes that test
//   5  VdotH      |
//   6  geometrySmith G
//   7  fresnelSchlick F
//   8  the assembled GGX weight F * G * VdotH / max(NdotV * NdotH, 1e-4)
//   9  the weight sampleBrdfDirection returned - without 8, it came from the delta or the
//      diffuse branch, or from the division by the lobe probability
//  10  the direction L sampleBrdfDirection returned
//  11  throughput after the weight was multiplied in
//  12  the environment contribution on a bounce miss
//  13  the emissive contribution on an emissive texel
#define PT_NF_NONE          0
#define PT_NF_DIRECT_LIGHT  1
#define PT_NF_GGX_H         2
#define PT_NF_GGX_NDOTL     3
#define PT_NF_GGX_NDOTH     4
#define PT_NF_GGX_VDOTH     5
#define PT_NF_GGX_G         6
#define PT_NF_GGX_F         7
#define PT_NF_GGX_WEIGHT    8
#define PT_NF_BRDF_WEIGHT   9
#define PT_NF_BRDF_L        10
#define PT_NF_THROUGHPUT    11
#define PT_NF_ENVIRONMENT   12
#define PT_NF_EMISSIVE      13
#define PT_NF_COUNT         14

// Bounds of the diagnostic scale, as base-10 exponents. Centred on one rather than starting
// there: a healthy contribution IS around one, so the scale has to show both what is far
// below it and what is far above. The floor is what the tracer clamps to before taking the
// logarithm, so it and the low bound have to agree.
#define PT_DEBUG_LOG_FLOOR 1e-4
#define PT_DEBUG_LOG_MIN   (-4.0)
#define PT_DEBUG_LOG_MAX   4.0

// What PT Max Contribution reports for a contribution that is NaN or infinite. Such a value
// loses every comparison it takes part in, so it has to be swapped for a finite one to be
// seen at all. Far above the top of the ramp on purpose: it saturates on screen and reads
// back as exactly 9 on the log scale, which no real contribution reaches.
#define PT_DEBUG_NONFINITE_MAGNITUDE 1e9

// Shared by the shaders and by Render, which has to switch off everything that
// would modify these values on their way to the screen. Despite the name the test
// is not about indirect light - it is about a view whose value must reach the
// screen untouched, which is why DEBUG_VIEW_DIRECT_ONLY belongs here too.
// The SSGI views are deliberately NOT in this list: it switches TAA off, and the
// TAA history is an input of SSGI - the views would then show a pipeline state
// the engine never actually runs.
#define IS_INDIRECT_DEBUG_VIEW(view) ( \
     (view) == DEBUG_VIEW_AMBIENT_ONLY \
  || (view) == DEBUG_VIEW_AMBIENT_DIFFUSE \
  || (view) == DEBUG_VIEW_AMBIENT_SPECULAR \
  || (view) == DEBUG_VIEW_PROBE_INDEX \
  || (view) == DEBUG_VIEW_PROBE_FALLBACK \
  || (view) == DEBUG_VIEW_VOLUME_COVERAGE \
  || (view) == DEBUG_VIEW_DIRECT_ONLY \
  || (view) == DEBUG_VIEW_VOLUME_LEVEL)

// Views whose source is written by a pass the path tracing render path switches off: the
// AO chain, the screen space effects, the temporal resolve and every deferred lighting
// diagnostic. Selecting one while that path is effective would display a buffer nothing
// wrote this frame, so Render falls the view back to the final image the same way it does
// for a path traced view whose pass has not run yet.
#define IS_RASTER_ONLY_DEBUG_VIEW(view) ( \
     (view) == DEBUG_VIEW_AO \
  || (view) == DEBUG_VIEW_SSR \
  || (view) == DEBUG_VIEW_TAA_DELTA \
  || ((view) >= DEBUG_VIEW_AMBIENT_ONLY && (view) <= DEBUG_VIEW_DIRECT_ONLY) \
  || (view) == DEBUG_VIEW_VOLUME_LEVEL)

#ifdef __cplusplus
#pragma once
#define vec2 glm::vec2
#define vec3 glm::vec3
#define vec4 glm::vec4
#define mat4 glm::mat4
namespace YAEngine {
#endif

struct FrameUniforms
{
  mat4 view;
  mat4 proj;
  mat4 invProj;
  mat4 prevView;
  mat4 prevProj;
  vec3 cameraPosition;
  float time;
  vec3 cameraDirection;
  float gamma;
  float exposure;
  int currentTexture;
  float nearPlane;
  float farPlane;
  float fov;
  int screenWidth;
  int screenHeight;
  int aoEnabled;
  int ssrEnabled;
  int taaEnabled;
  float jitterX;
  float jitterY;
  int hizMipCount;
  int frameIndex;
  int tileCountX;
  int tileCountY;
  mat4 invView;
  int tonemapMode;
  float bloomIntensity;
  // Artistic fades applied where AO is consumed, not where it is computed: the GTAO
  // parameters proper live in GTAOConstants.h.
  float aoStrength;
  float aoSpecularStrength;
  float aoMultiBounce;
  int fogEnabled;
  float fogDensity;
  float fogHeightFalloff;
  vec3 fogColor;
  float fogStartDistance;
  float fogMaxOpacity;
  float taaClampSigma;
  float ssrIntensity;
  // Meters the diffuse sample point is pushed along the normal before it is
  // looked up in an irradiance volume. Appended at the end so no existing
  // member offset moves.
  float irradianceNormalBias;
  // Appended at the end for the same reason. Selects the SSGI composition path
  // in deferred lighting; the pass permutation itself is chosen on the CPU.
  int ssgiEnabled;
  // Display resolution. screenWidth/screenHeight above is the resolution the scene is
  // rasterized and shaded at; the two only differ while a DLSS upscale mode is active,
  // and only passes downstream of the upscaler may use these.
  int outputWidth;
  int outputHeight;
  // proj without the camera jitter folded in. Passes downstream of the temporal
  // resolve (editor gizmos) draw over an already stabilized image and must use
  // this one. padding0 keeps the mat4 on the 16 byte boundary std140 expects.
  int padding0;
  mat4 unjitteredProj;
  // The grade applied on top of whichever tone mapping curve is selected, in display encoded
  // space: power is contrast, saturation pulls each channel away from the pixel's luma.
  // Both are 1.0 for the curve on its own. Appended at the end so no member offset moves.
  float tonemapPower;
  float tonemapSaturation;
  // std140 rounds the block up to a multiple of 16 bytes; the two floats above leave it 8
  // short, and the buffer has to be allocated at the size the GPU sees.
  float padding1[2];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec2
#undef vec3
#undef vec4
#undef mat4
#endif
