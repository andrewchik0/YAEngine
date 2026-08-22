layout(location = 0) in vec2 uv;
layout(location = 0) out float outAO;
layout(location = 1) out float outEdges;

#include "common.glsl"
#include "octahedron.glsl"
#include "gtao_common.glsl"

layout(set = 1, binding = 1) uniform sampler2D depthTexture;
layout(set = 1, binding = 2) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 3) uniform sampler2D hilbertLUT;

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

  vec2 localNoise = SpatioTemporalNoise(pixCoord, u_GTAO.noiseIndex);
  float noiseSlice = localNoise.x;
  float noiseSample = localNoise.y;

  // Below this screen-space offset a sample lands on the center pixel itself and carries no
  // information, so every slice starts at least this far out.
  const float pixelTooCloseThreshold = 1.3;

  vec2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ * u_GTAO.ndcToViewMulPixelSize;
  float screenspaceRadius = effectRadius / pixelDirRBViewspaceSizeAtCenterZ.x;

  float visibility = 0.0;
  // Fade the effect out where the radius covers only a few pixels: there is not enough
  // screen-space information left to integrate, and the result would just be noise.
  visibility += clamp((10.0 - screenspaceRadius) / 100.0, 0.0, 1.0) * 0.5;

  float minS = pixelTooCloseThreshold / screenspaceRadius;

  float sliceCount = float(u_GTAO.sliceCount);
  float stepsPerSlice = float(u_GTAO.stepsPerSlice);

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

    float horizonCos0 = lowHorizonCos0;
    float horizonCos1 = lowHorizonCos1;

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

      shc0 = mix(lowHorizonCos0, shc0, weight0);
      shc1 = mix(lowHorizonCos1, shc1, weight1);

      horizonCos0 = max(horizonCos0, shc0);
      horizonCos1 = max(horizonCos1, shc1);
    }

    // Fudge from the reference against slight over-darkening on steep slopes.
    projectedNormalVecLength = mix(projectedNormalVecLength, 1.0, 0.05);

    float h0 = -gtaoFastAcos(horizonCos1);
    float h1 = gtaoFastAcos(horizonCos0);

    // Closed-form cosine-weighted visibility integral between the two horizons - this is what
    // separates GTAO from a plain horizon-based occlusion estimate.
    float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
    float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
    visibility += projectedNormalVecLength * (iarc0 + iarc1);
  }

  visibility /= sliceCount;
  visibility = pow(visibility, u_GTAO.finalValuePower);
  // A visible pixel is never fully occluded, and a hard zero would survive the denoise.
  visibility = max(0.03, visibility);

  outAO = clamp(visibility / float(GTAO_OCCLUSION_TERM_SCALE), 0.0, 1.0);
}
