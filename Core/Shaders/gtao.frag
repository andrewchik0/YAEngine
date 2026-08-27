layout(location = 0) in vec2 uv;
layout(location = 0) out float outAO;
layout(location = 1) out float outEdges;
// SSGI outputs, present in both permutations so the attachment set matches:
// screen-gathered irradiance rgb + volume fallback weight in alpha, and the
// mean unoccluded direction in octahedral encoding. The base permutation
// writes zeros / the geometric normal; deferred lighting only reads them
// while SSGI is enabled.
layout(location = 2) out vec4 outGI;
layout(location = 3) out vec2 outBentNormal;

#include "common.glsl"
#include "octahedron.glsl"
#include "gtao_common.glsl"

layout(set = 1, binding = 1) uniform sampler2D depthTexture;
layout(set = 1, binding = 2) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 3) uniform sampler2D hilbertLUT;

#ifdef SSGI
layout(set = 1, binding = 4) uniform sampler2D ssgiRadianceTexture;
#include "ssgi_bitmask.glsl"
#endif

// Hilbert-curve-driven R2 sequence: spatially it is close to blue noise, and offsetting the
// curve index by the frame counter walks the sequence so consecutive frames stay decorrelated
// and TAA can integrate them. The reference offsets by 288 per frame, found by experiment.
vec2 SpatioTemporalNoise(ivec2 pixCoord, int temporalIndex)
{
  float index = texelFetch(hilbertLUT, pixCoord % GTAO_HILBERT_WIDTH, 0).r;
  index += 288.0 * float(temporalIndex % 64);
  return fract(0.5 + index * vec2(0.75487766624669276, 0.56984029099805327));
}

void main()
{
  ivec2 pixCoord = ivec2(gl_FragCoord.xy);
  ivec2 maxCoord = ivec2(u_Frame.screenWidth, u_Frame.screenHeight) - 1;
  vec2 normalizedScreenPos = (vec2(pixCoord) + 0.5) * u_GTAO.viewportPixelSize;

  float viewspaceZ = texelFetch(depthTexture, pixCoord, 0).r;
  float pixLZ = texelFetch(depthTexture, min(pixCoord + ivec2(-1, 0), maxCoord), 0).r;
  float pixRZ = texelFetch(depthTexture, min(pixCoord + ivec2( 1, 0), maxCoord), 0).r;
  float pixTZ = texelFetch(depthTexture, min(pixCoord + ivec2( 0, -1), maxCoord), 0).r;
  float pixBZ = texelFetch(depthTexture, min(pixCoord + ivec2( 0, 1), maxCoord), 0).r;

  outEdges = gtaoPackEdges(gtaoCalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ));

  vec3 worldNormal = octDecode(texelFetch(gbuffer1Texture, pixCoord, 0).rg * 2.0 - 1.0);

  // Sky texels linearize to the fp16 ceiling in the prefilter, which is the cheapest sky test
  // available here and avoids binding the raw depth buffer a second time.
  if (viewspaceZ >= GTAO_DEPTH_CLAMP || dot(worldNormal, worldNormal) < 1e-6)
  {
    outAO = 1.0 / float(GTAO_OCCLUSION_TERM_SCALE);
    // Fallback weight 1: were the sky ever composed, it would fall through to the
    // volume path untouched. The bent normal encodes an arbitrary valid direction.
    outGI = vec4(0.0, 0.0, 0.0, 1.0);
    outBentNormal = vec2(0.5);
    return;
  }

  // Engine view space mirrored into GTAO space by diag(1, 1, -1), matching the positions
  // gtaoViewspacePosition builds. See the note in gtao_common.glsl.
  vec3 engineViewNormal = mat3(u_Frame.view) * normalize(worldNormal);
  vec3 viewspaceNormal = vec3(engineViewNormal.x, engineViewNormal.y, -engineViewNormal.z);

  // Nudge the center towards the camera so a sample landing on the center pixel's own surface
  // does not self-occlude through fp16 depth quantisation.
  viewspaceZ *= 0.99920;

  vec3 pixCenterPos = gtaoViewspacePosition(normalizedScreenPos, viewspaceZ);
  vec3 viewVec = normalize(-pixCenterPos);

  float effectRadius = u_GTAO.effectRadius * u_GTAO.radiusMultiplier;
  float sampleDistributionPower = u_GTAO.sampleDistributionPower;
  float thinOccluderCompensation = u_GTAO.thinOccluderCompensation;
  float falloffRange = u_GTAO.effectFalloffRange * effectRadius;
  float falloffFrom = effectRadius * (1.0 - u_GTAO.effectFalloffRange);
  float falloffMul = -1.0 / falloffRange;
  float falloffAdd = falloffFrom / falloffRange + 1.0;

#ifdef SSGI
  // The march covers the wider of the two radii; AO and GI take their own
  // integrals from the same samples through their own falloff weights, so the
  // contact occlusion does not stretch out just because the bounce reaches far.
  float marchRadius = max(effectRadius, u_GTAO.ssgiRadius);
  float giFalloffRange = u_GTAO.effectFalloffRange * u_GTAO.ssgiRadius;
  float giFalloffMul = -1.0 / giFalloffRange;
  float giFalloffAdd = u_GTAO.ssgiRadius * (1.0 - u_GTAO.effectFalloffRange) / giFalloffRange + 1.0;
#else
  float marchRadius = effectRadius;
#endif

  vec2 localNoise = SpatioTemporalNoise(pixCoord, u_GTAO.noiseIndex);
  float noiseSlice = localNoise.x;
  float noiseSample = localNoise.y;

  // Below this screen-space offset a sample lands on the center pixel itself and carries no
  // information, so every slice starts at least this far out.
  const float pixelTooCloseThreshold = 1.3;

  vec2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ * u_GTAO.ndcToViewMulPixelSize;
  float screenspaceRadius = marchRadius / pixelDirRBViewspaceSizeAtCenterZ.x;

  float visibility = 0.0;
  // Fade the effect out where the radius covers only a few pixels: there is not enough
  // screen-space information left to integrate, and the result would just be noise.
  visibility += clamp((10.0 - screenspaceRadius) / 100.0, 0.0, 1.0) * 0.5;

  float minS = pixelTooCloseThreshold / screenspaceRadius;

  float sliceCount = float(u_GTAO.sliceCount);
  float stepsPerSlice = float(u_GTAO.stepsPerSlice);

#ifdef SSGI
  vec3 giRadiance = vec3(0.0);
  float giOccluded = 0.0;
  float giInvalid = 0.0;
  float giWeightSum = 0.0;
  vec3 bentAccum = vec3(0.0);
#endif

  for (int sliceIndex = 0; sliceIndex < u_GTAO.sliceCount; sliceIndex++)
  {
    float slice = float(sliceIndex);
    float sliceK = (slice + noiseSlice) / sliceCount;
    float phi = sliceK * GTAO_PI;
    float cosPhi = cos(phi);
    float sinPhi = sin(phi);

    // Screen-space march direction in pixels. The y sign is flipped against directionVec
    // because GTAO space has Y up while the screen v axis points down.
    vec2 omega = vec2(cosPhi, -sinPhi) * screenspaceRadius;

    vec3 directionVec = vec3(cosPhi, sinPhi, 0.0);
    vec3 orthoDirectionVec = directionVec - dot(directionVec, viewVec) * viewVec;
    vec3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

    vec3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

    float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
    float projectedNormalVecLength = length(projectedNormalVec);
    float cosNorm = clamp(dot(projectedNormalVec, viewVec) / projectedNormalVecLength, 0.0, 1.0);
    float n = signNorm * gtaoFastAcos(cosNorm);

    // Starting horizons sit at the edge of the normal-oriented hemisphere rather than at -1:
    // a sample below the horizon carries a different weight depending on the normal.
    float lowHorizonCos0 = cos(n + GTAO_PI_HALF);
    float lowHorizonCos1 = cos(n - GTAO_PI_HALF);

#ifndef SSGI
    float horizonCos0 = lowHorizonCos0;
    float horizonCos1 = lowHorizonCos1;
#else
    float sinN = signNorm * sqrt(max(1.0 - cosNorm * cosNorm, 0.0));
    // Full-arc cosine-weighted measure: what the horizon integral evaluates to on
    // an empty scene. Every sector of the mask carries sliceTotal / 32 of it.
    float sliceTotal = n * sinN + cosNorm;
    float invTotal = 1.0 / max(sliceTotal, 1e-4);
    float cdfMin = ssgiArcCDF(n - GTAO_PI_HALF, n, sinN, cosNorm);

    // The reference applies this fudge after the loop; here every sample already
    // weighs in with it, so it moves in front.
    float projNLAdj = mix(projectedNormalVecLength, 1.0, 0.05);

    vec3 sliceTangent = normalize(orthoDirectionVec);
    uint maskAO = 0u;
    uint maskGI = 0u;

    bentAccum += projNLAdj * ssgiFullArcDir(sinN, cosNorm, viewVec, sliceTangent);
#endif

    for (int stepIndex = 0; stepIndex < u_GTAO.stepsPerSlice; stepIndex++)
    {
      float stepF = float(stepIndex);
      // R1 sequence, decorrelating the step offsets between slices.
      float stepBaseNoise = (slice + stepF * stepsPerSlice) * 0.6180339887498948;
      float stepNoise = fract(noiseSample + stepBaseNoise);

      float s = (stepF + stepNoise) / stepsPerSlice;
      s = pow(s, sampleDistributionPower);
      s += minS;

      vec2 sampleOffset = s * omega;
      float sampleOffsetLength = length(sampleOffset);

      float mipLevel = clamp(log2(sampleOffsetLength) - u_GTAO.depthMipSamplingOffset,
        0.0, float(GTAO_DEPTH_MIP_LEVELS - 1));

      // Snapping to whole pixels keeps the sampled depth and the direction the slope is
      // computed from referring to the same texel.
      sampleOffset = round(sampleOffset) * u_GTAO.viewportPixelSize;

      vec2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
      vec2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;

      float SZ0 = textureLod(depthTexture, sampleScreenPos0, mipLevel).r;
      float SZ1 = textureLod(depthTexture, sampleScreenPos1, mipLevel).r;

      vec3 samplePos0 = gtaoViewspacePosition(sampleScreenPos0, SZ0);
      vec3 samplePos1 = gtaoViewspacePosition(sampleScreenPos1, SZ1);

      vec3 sampleDelta0 = samplePos0 - pixCenterPos;
      vec3 sampleDelta1 = samplePos1 - pixCenterPos;
      float sampleDist0 = length(sampleDelta0);
      float sampleDist1 = length(sampleDelta1);

      vec3 sampleHorizonVec0 = sampleDelta0 / sampleDist0;
      vec3 sampleHorizonVec1 = sampleDelta1 / sampleDist1;

      // Thickness heuristic: the depth buffer only records the front surface, so a sample
      // sitting far behind the center is likely the back of an object that never occluded
      // anything. Stretching the depth axis discards those sooner.
      float falloffBase0 = length(vec3(sampleDelta0.xy, sampleDelta0.z * (1.0 + thinOccluderCompensation)));
      float falloffBase1 = length(vec3(sampleDelta1.xy, sampleDelta1.z * (1.0 + thinOccluderCompensation)));
      float weight0 = clamp(falloffBase0 * falloffMul + falloffAdd, 0.0, 1.0);
      float weight1 = clamp(falloffBase1 * falloffMul + falloffAdd, 0.0, 1.0);

      float shc0 = dot(sampleHorizonVec0, viewVec);
      float shc1 = dot(sampleHorizonVec1, viewVec);

#ifndef SSGI
      shc0 = mix(lowHorizonCos0, shc0, weight0);
      shc1 = mix(lowHorizonCos1, shc1, weight1);

      horizonCos0 = max(horizonCos0, shc0);
      horizonCos1 = max(horizonCos1, shc1);
#else
      float giWeight0 = clamp(falloffBase0 * giFalloffMul + giFalloffAdd, 0.0, 1.0);
      float giWeight1 = clamp(falloffBase1 * giFalloffMul + giFalloffAdd, 0.0, 1.0);

      // Honest occluder extent instead of a horizon: the surface occupies the arc
      // between its front face and that face pushed back by the thickness. The
      // back direction can degenerate when the pushed point crosses the pixel,
      // hence the length guard.
      vec3 backDelta0 = sampleDelta0 - viewVec * u_GTAO.ssgiThickness;
      vec3 backDelta1 = sampleDelta1 - viewVec * u_GTAO.ssgiThickness;
      float backLen0 = length(backDelta0);
      float backLen1 = length(backDelta1);
      float backCos0 = backLen0 > 1e-5 ? dot(backDelta0, viewVec) / backLen0 : 1.0;
      float backCos1 = backLen1 > 1e-5 ? dot(backDelta1, viewVec) / backLen1 : 1.0;

      // Side 0 lies on the +tangent side (positive angles), side 1 mirrors it.
      float thetaFront0 = gtaoFastAcos(clamp(shc0, -1.0, 1.0));
      float thetaBack0 = gtaoFastAcos(clamp(backCos0, -1.0, 1.0));
      float thetaFront1 = -gtaoFastAcos(clamp(shc1, -1.0, 1.0));
      float thetaBack1 = -gtaoFastAcos(clamp(backCos1, -1.0, 1.0));

      vec2 interval;
      uint bitsAO0 = ssgiBitsForAngles(thetaFront0, thetaBack0, weight0, n, sinN, cosNorm, cdfMin, invTotal, interval);
      uint bitsGI0 = ssgiBitsForAngles(thetaFront0, thetaBack0, giWeight0, n, sinN, cosNorm, cdfMin, invTotal, interval);
      maskAO |= bitsAO0;

      // Only bits not yet in the mask carry radiance: the screen knows the
      // radiance of exactly the geometry that occludes, and each sector must be
      // claimed once, by its nearest occluder.
      uint newBits0 = bitsGI0 & ~maskGI;
      maskGI |= bitsGI0;
      if (newBits0 != 0u)
      {
        float share = projNLAdj * sliceTotal * ssgiMaskFraction(newBits0);
        vec4 rad = textureLod(ssgiRadianceTexture, sampleScreenPos0, mipLevel);
        giRadiance += rad.rgb * rad.a * share;
        giInvalid += (1.0 - rad.a) * share;
        giOccluded += share;
        bentAccum -= ssgiSliceDir(0.5 * (interval.x + interval.y), viewVec, sliceTangent) * share;
      }

      uint bitsAO1 = ssgiBitsForAngles(thetaFront1, thetaBack1, weight1, n, sinN, cosNorm, cdfMin, invTotal, interval);
      uint bitsGI1 = ssgiBitsForAngles(thetaFront1, thetaBack1, giWeight1, n, sinN, cosNorm, cdfMin, invTotal, interval);
      maskAO |= bitsAO1;

      uint newBits1 = bitsGI1 & ~maskGI;
      maskGI |= bitsGI1;
      if (newBits1 != 0u)
      {
        float share = projNLAdj * sliceTotal * ssgiMaskFraction(newBits1);
        vec4 rad = textureLod(ssgiRadianceTexture, sampleScreenPos1, mipLevel);
        giRadiance += rad.rgb * rad.a * share;
        giInvalid += (1.0 - rad.a) * share;
        giOccluded += share;
        bentAccum -= ssgiSliceDir(0.5 * (interval.x + interval.y), viewVec, sliceTangent) * share;
      }
#endif
    }

#ifndef SSGI
    // Fudge from the reference against slight over-darkening on steep slopes.
    projectedNormalVecLength = mix(projectedNormalVecLength, 1.0, 0.05);

    float h0 = -gtaoFastAcos(horizonCos1);
    float h1 = gtaoFastAcos(horizonCos0);

    // Closed-form cosine-weighted visibility integral between the two horizons - this is what
    // separates GTAO from a plain horizon-based occlusion estimate.
    float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
    float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
    visibility += projectedNormalVecLength * (iarc0 + iarc1);
#else
    // Sectors carry equal measure, so unoccluded visibility is a popcount away
    // and reproduces the closed-form arc integral exactly on an empty mask.
    visibility += projNLAdj * sliceTotal * (1.0 - ssgiMaskFraction(maskAO));
    giWeightSum += projNLAdj * sliceTotal;
#endif
  }

  visibility /= sliceCount;
  // finalValuePower is an artistic darkener for the AO output alone - the GI
  // accumulators below stay linear, or the two composition halves would no
  // longer sum to a full hemisphere.
  visibility = pow(visibility, u_GTAO.finalValuePower);
  // A visible pixel is never fully occluded, and a hard zero would survive the denoise.
  visibility = max(0.03, visibility);

  outAO = clamp(visibility / float(GTAO_OCCLUSION_TERM_SCALE), 0.0, 1.0);

#ifndef SSGI
  outGI = vec4(0.0);
  outBentNormal = octEncode(normalize(worldNormal)) * 0.5 + 0.5;
#else
  // Normalized against the actual integrated measure, so that with no screen
  // geometry the fallback weight is EXACTLY one and composition reduces to the
  // volume path untouched - the acceptance invariant of the whole plan.
  float invW = 1.0 / max(giWeightSum, 1e-4);
  vec3 screenPart = giRadiance * invW * u_GTAO.ssgiIntensity;
  float validOccluded = clamp((giOccluded - giInvalid) * invW, 0.0, 1.0);
  outGI = vec4(screenPart, 1.0 - validOccluded);

  // GTAO space back to world: undo the diag(1, 1, -1) mirror, then the view rotation.
  float bentLen = length(bentAccum);
  vec3 bentGTAO = bentLen > 1e-5 ? bentAccum / bentLen : viewspaceNormal;
  vec3 bentWorld = normalize(mat3(u_Frame.invView) * vec3(bentGTAO.x, bentGTAO.y, -bentGTAO.z));
  outBentNormal = octEncode(bentWorld) * 0.5 + 0.5;
#endif
}
