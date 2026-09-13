#ifdef __cplusplus
#pragma once
namespace YAEngine {
#endif

// Bounce budget the editor slider offers. One bounce is direct light plus a single
// indirect gather; three is where an interior stops changing visibly per extra bounce.
#define PT_MIN_BOUNCES 1
#define PT_MAX_BOUNCES 8

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

// The bounce Russian roulette starts at. Killing paths before it costs more variance than
// the traversal it saves - the first two bounces carry most of the energy of the path.
#define PT_RR_START_BOUNCE 2

// Indices into the path tracing pipeline's miss list. Render.Pipelines.cpp fills it in
// exactly this order and the raygen shader traces against these numbers, so the two sides
// of the shader binding table agree in one place instead of two.
#define PT_PRIMARY_MISS_INDEX 0
#define PT_SHADOW_MISS_INDEX  1

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
};

#ifdef __cplusplus
} // namespace YAEngine
#endif
