#ifdef __cplusplus
#pragma once
namespace YAEngine {
#endif

// Bounce budget the editor slider offers. One bounce is direct light plus a single
// indirect gather; three is where an interior stops changing visibly per extra bounce.
#define PT_MIN_BOUNCES 1
#define PT_MAX_BOUNCES 8

// Budget of refractive events along one path - every reflection off, transmission through or
// ignored overlap at a Solid surface the path refracts at - which do not consume the bounce budget.
// Sheet and ThinWalled glass and Solid glass crossed straight never spend it. A liquid seen through
// its glass is two events per liquid surface, hence a separate, larger budget.
#define PT_MIN_TRANSMISSION_DEPTH 1
#define PT_MAX_TRANSMISSION_DEPTH 16
#define PT_DEFAULT_TRANSMISSION_DEPTH 8

// PathTraceConstants::secondaryGlass and ::glassReflectionGlass: how Solid glass is met on those
// rays. Refract follows it through Snell's law at one traced event per interface; Straight crosses it
// unbent, taking its Fresnel transmittance and absorption from one query per segment.
#define PT_GLASS_REFRACT  0
#define PT_GLASS_STRAIGHT 1

// PathTraceConstants::glassOverflow: what a path does once its refractive events are spent - end,
// or cross every further Solid straight.
#define PT_GLASS_OVERFLOW_TERMINATE 0
#define PT_GLASS_OVERFLOW_STRAIGHT  1

// Bounces of the reflection layer's path off a glass first vertex, up to PT_MAX_BOUNCES. Zero is
// what the reflected hit emits or the sky, plus next event estimation there.
#define PT_MIN_GLASS_REFLECTION_BOUNCES 0
#define PT_DEFAULT_GLASS_REFLECTION_BOUNCES 1

// Bounces of the reflection layer's mirror path off an opaque G-buffer vertex - a delta clear coat or
// a smooth dielectric - counted past the reflected surface as the glass knob above counts them: zero
// is that surface's emission and next event estimation. Capped one below PT_MAX_BOUNCES because the
// G-buffer vertex itself is bounce zero of that path.
#define PT_MIN_LAYER_REFLECTION_BOUNCES 0
#define PT_MAX_LAYER_REFLECTION_BOUNCES (PT_MAX_BOUNCES - 1)
#define PT_DEFAULT_LAYER_REFLECTION_BOUNCES 1

// Roughness above which a G-buffer vertex traces no specular hit distance probe: its ray
// reconstruction specular guides then place the reflection on the surface itself (hit distance 0,
// specular motion = surface motion). A lobe that wide blurs the reflected image past the point where
// its parallax is worth a ray per pixel. Mirrors and reflection layer vertices always trace it.
#define PT_DEFAULT_SPECULAR_GUIDE_MAX_ROUGHNESS 0.5f

// Range of the firefly clamp, shared by the editor slider, the scene save and the scene load.
// One definition because there used to be three disagreeing ones: the slider dragged to 100
// but let Ctrl+Click type past it, the save wrote whatever the field held, and the load clamped
// to 100 - so a scene came back from disk showing a different image than it was saved with.
// Zero is a valid setting and means the clamp is off, so the minimum cannot be raised.
#define PT_MIN_FIREFLY_CLAMP 0.0f
#define PT_MAX_FIREFLY_CLAMP 100.0f

// Floor on the probability of picking the specular lobe, and the roughness band it fades out
// over. Fresnel at normal incidence alone picks specular about four percent of the time on a
// dark smooth dielectric - Bistro's exterior glass - and pays 1/p for it, which is a stream
// of 25x spikes a temporal denoiser amplifies rather than integrates. Any probability in
// (0, 1) is unbiased, so this trades variance between the lobes and never moves the mean.
// The band ends well below the roughness where a lobe stops being tight enough to matter.
#define PT_SMOOTH_LOBE_FLOOR 0.25
#define PT_LOBE_FLOOR_ROUGHNESS_MIN 0.15
#define PT_LOBE_FLOOR_ROUGHNESS_MAX 0.4

// Points on emitting geometry drawn at every next event estimation vertex, resampled together with
// the one analytic light candidate down to the single sample the shadow ray is traced to. Each costs
// a few buffer fetches and a BRDF evaluation, never a ray.
#define PT_EMISSIVE_CANDIDATES 4

// The bounce Russian roulette starts at. Killing paths before it costs more variance than
// the traversal it saves - the first two bounces carry most of the energy of the path.
#define PT_RR_START_BOUNCE 2

// Indices into the path tracing pipeline's miss list. Render.Pipelines.cpp fills it in
// exactly this order and the raygen shader traces against these numbers, so the two sides
// of the shader binding table agree in one place instead of two.
#define PT_PRIMARY_MISS_INDEX 0
#define PT_SHADOW_MISS_INDEX  1

// The hit groups of the same pipelines, in the same spirit. Every TLAS instance names record 0,
// so the sbtRecordOffset a trace passes is the group: surface rays record the hit, shadow rays and
// glass transmittance queries accumulate the transmittance of the dielectrics they pass through.
#define PT_PATH_HIT_GROUP   0
#define PT_SHADOW_HIT_GROUP 1

// Solid media a path can be inside at once, see the medium stack in pt_path.glsl. A glass, the
// liquid overlapping its wall and an ice cube in the liquid is three.
#define PT_MEDIUM_STACK_SIZE 4

// Written into the alpha of the noisy output where the primary "ray" left the scene.
// Negative on purpose: a consumer that averages hit distances has to be able to reject the
// sky, and a huge positive value would quietly drag the average instead of being rejected.
#define PT_SKY_DISTANCE (-1.0)

// GGX alpha at or below which the specular lobe is treated as a perfect mirror rather than
// sampled. A delta lobe has no width to importance sample and no finite density to evaluate,
// so every term written for a finite lobe degenerates on it - which is why this used to be a
// FLOOR under alpha instead of a branch, and why that was wrong twice over:
//
//  - importanceSampleGGX already collapses correctly at alpha zero. Its cos^2 is
//    (1 - xi) / ((1 - xi) + a*a * xi), which at a = 0 is (1 - xi) / (1 - xi) = 1, so the
//    half vector IS the normal and the sampled direction IS the mirror. Flooring the
//    roughness replaced that exact answer with a cone about a tenth of a degree wide, and a
//    tenth of a degree of jitter on a reflected image is a lot of pixels.
//  - next event estimation on a delta lobe is identically zero, because a delta BRDF cannot
//    see a delta light. The floor turned that zero into a narrow spurious highlight and paid
//    a shadow ray for it.
//
// Below the threshold the tracer therefore mirrors the view vector analytically, weights the
// bounce by Fresnel alone (the delta's density cancels the cosine), and gives next event
// estimation the diffuse half only. Above it nothing is floored: alpha is roughness squared,
// which the branch guarantees is already larger than this.
//
// Deliberately NOT applied to the roughness pt_guides.comp writes into the ray
// reconstruction guide: the denoiser wants the material's true value.
#define PT_DELTA_MAX_ALPHA 1e-3

// Written to the specular hit distance guide where the first bounce left the scene. Ray
// reconstruction reads a large distance as "the virtual reflection sits at infinity", which
// is exactly what a sky reflection wants. Not PT_RAY_TMAX: the guide image is R16F and fp16
// tops out at 65504, so the trace's 100000 would be stored as infinity.
#define PT_SPECULAR_MISS_DISTANCE 60000.0

// How far from the depth-reconstructed primary point the camera ray that looks up the reflector
// instance may land and still count as that surface: distance * relative + absolute, in world
// units. Covers the depth reconstruction's precision; a rejection only degrades the specular
// motion vectors to the static-reflector assumption.
#define PT_REFLECTOR_LOOKUP_RELATIVE_TOLERANCE 0.005
#define PT_REFLECTOR_LOOKUP_ABSOLUTE_TOLERANCE 0.02

// Primary Surface Replacement: what ray reconstruction is told about a pixel whose first vertex
// is a metallic delta mirror (alpha <= PT_DELTA_MAX_ALPHA). RR cannot keep a reflection sharp
// under camera translation from the specular guides alone - measured, reflection detail fell to
// a tenth of the reference - but handles an ordinary surface fine, so the guides, depth and
// motion describe the first non-mirror surface seen through the mirror instead, as NVIDIA's
// vk_denoise_dlssrr sample does. Only what RR sees changes; the radiance estimator does not.
//
// Metals only: a smooth dielectric shows its own surface and the reflection at once, and
// replacing it would hide the first. Just under one so 8-bit and texture rounding of a
// metallic of 1 still qualifies.
#define PT_PSR_MIN_METALLIC 0.99
// Mirrors followed before the chain stops and the last one is described as itself. Also capped
// by the bounce budget, beyond which the estimator never reaches the surface behind.
#define PT_PSR_MAX_CHAIN_DEPTH 3

struct PathTraceConstants
{
  int maxBounces;
  // -1 switches accumulation off entirely, which is what the noisy view wants. Otherwise
  // it is the index of the sample this frame contributes to the running mean: 0 rewrites
  // the accumulation image outright, which IS the reset - nothing ever has to clear it.
  int sampleIndex;
  // Ceiling on the radiance a single bounce may add, 0 = off. The one deliberate bias in
  // the estimator, and the only reason a converged image is not ground truth.
  float fireflyClamp;
  // PT_DEBUG_* from FrameUniforms.h. Non-zero replaces the radiance stored in the noisy image
  // with a diagnostic, so the accumulation image is left alone while one is being read - a
  // diagnostic blended into the running mean would be meaningless. Measured before the
  // firefly clamp on purpose: the clamp is exactly what hides the value worth reading.
  int debugMode;
  // PT_MIN_TRANSMISSION_DEPTH..PT_MAX_TRANSMISSION_DEPTH.
  int maxTransmissionDepth;
  // PT_GLASS_OVERFLOW_*.
  int glassOverflow;
  // PT_GLASS_*: Solid glass on every ray after a vertex that is not a pure delta.
  int secondaryGlass;
  // PT_MIN_GLASS_REFLECTION_BOUNCES..PT_MAX_BOUNCES.
  int glassReflectionBounces;
  // PT_GLASS_*: Solid glass on the reflection layer's path until it reaches such a vertex.
  int glassReflectionGlass;
  // The PT Glass switch. Zero: TlasBuilder builds no instance as glass and the shaders trace nothing
  // for it - no camera glass trace, no glass query.
  int glassEnabled;
  // Nonzero: the forward transparent layer was drawn this frame and is laid over the sample, the one
  // ray reconstruction denoises and the one the running mean takes in.
  int transparentLayer;
  // Nonzero: a path segment that left a delta lobe can see the sun disk. Off for an environment map
  // that already holds the sun, which would otherwise be counted twice.
  int mirrorSun;
  // The span [sphereLightBegin, sphereLightEnd) of the flattened light candidates (0 is the sun) that
  // holds every sphere light a delta segment can run into, see analyticEmissionAlongRay in
  // pt_path.glsl. Empty where no light is one, which skips that test entirely.
  int sphereLightBegin;
  int sphereLightEnd;
  // PT_MIN_LAYER_REFLECTION_BOUNCES..PT_MAX_LAYER_REFLECTION_BOUNCES.
  int layerReflectionBounces;
  // See PT_DEFAULT_SPECULAR_GUIDE_MAX_ROUGHNESS.
  float specularGuideMaxRoughness;
  // Nonzero: some opaque TLAS instance moved since the previous frame (RT_MASK_MOVING), so the
  // reflector lookup has something to find. Zero skips it - every reflector is static.
  int reflectorLookup;
};

#ifdef __cplusplus
} // namespace YAEngine
#endif
