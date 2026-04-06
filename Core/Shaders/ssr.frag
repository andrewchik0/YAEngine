layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "utils.glsl"
#include "octahedron.glsl"
#include "pbr.glsl"
#include "noise.glsl"

layout(set = 1, binding = 0) uniform sampler2D litColorTexture;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;
layout(set = 1, binding = 2) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 3) uniform sampler2D gbuffer0Texture;
layout(set = 1, binding = 4) uniform sampler2D ssaoTexture;
layout(set = 1, binding = 5) uniform sampler2D hiZTexture;

// --- Configuration ---
const int MAX_ITERATIONS = 128;
const int REFINEMENT_STEPS = 8;
const float MAX_RAY_DISTANCE = 30.0;
const float SELF_INTERSECT_DIST = 0.04;
const float NORMAL_BIAS = 0.02;
const float MAX_ROUGHNESS = 0.6;
const float EDGE_FADE_START = 0.8;
const float DEPTH_EPSILON = 1.0;
const float SAME_SURFACE_THRESHOLD = 0.99;
const float MAX_THICKNESS = 4;

// --- Main ---
void main()
{
  float depth = texture(depthTexture, uv).r;

  // Read G-buffer data
  vec4 gb1 = texture(gbuffer1Texture, uv);
  vec4 gb0 = texture(gbuffer0Texture, uv);
  vec3 worldNormal = octDecode(gb1.rg * 2.0 - 1.0);
  float roughness = gb1.b;
  float metallic = gb0.a;
  vec3 albedo = gb0.rgb;
  vec3 originalColor = texture(litColorTexture, uv).rgb;
  float ao = texture(ssaoTexture, uv).r;

  if (u_Frame.ssaoEnabled == 0)
    ao = 1.0;

  // SSR debug mode: zero out base color to show only reflections
  if (u_Frame.currentTexture == 6)
    originalColor = vec3(0.0);

  // Early exits
  if (depth >= DEPTH_EPSILON)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  if (dot(worldNormal, worldNormal) < 0.000001)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  worldNormal = normalize(worldNormal);

  // Apply AO to geometry only
  originalColor *= ao;

  if (u_Frame.ssrEnabled == 0 || roughness > MAX_ROUGHNESS)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  // Reconstruct view-space position
  vec3 viewPos = reconstructViewPos(uv, depth);

  // Transform normal to view space
  vec3 viewNormal = normalize(mat3(u_Frame.view) * worldNormal);

  // Reflection direction: stochastic GGX sampling for rough surfaces, perfect reflection for mirrors
  vec3 viewDir = normalize(viewPos);
  vec3 reflectDir = reflect(viewDir, viewNormal);

  // Stochastic GGX sampling (disabled - TAA can't converge without dedicated reflection denoiser)
  if (roughness >= 0.02)
  {
    vec2 spatialNoise = vec2(
      interleavedGradientNoise(gl_FragCoord.xy),
      interleavedGradientNoise(gl_FragCoord.xy + vec2(97.0, 53.0))
    );
    vec2 Xi = fract(hammersley(uint(u_Frame.frameIndex) % 64u, 64u) + spatialNoise);
    vec3 H = importanceSampleGGX(Xi, viewNormal, min(roughness, 0.15));
    reflectDir = reflect(viewDir, H);

    if (dot(reflectDir, viewNormal) <= 0.0)
      reflectDir = reflect(viewDir, viewNormal);
  }

  // Backward fade for reflections pointing towards camera
  float backwardFade = 1.0 - clamp(reflectDir.z, 0.0, 1.0);

  // --- Screen-space ray march setup ---
  vec3 rayOrigin = viewPos + viewNormal * NORMAL_BIAS;
  vec3 rayEnd = rayOrigin + reflectDir * MAX_RAY_DISTANCE;

  // Project start and end to clip space
  vec4 startClip = u_Frame.proj * vec4(rayOrigin, 1.0);
  vec4 endClip = u_Frame.proj * vec4(rayEnd, 1.0);

  // If end is behind camera, clip the ray to stay in front
  if (endClip.w <= 0.0)
  {
    float tClip = (startClip.w - 0.01) / (startClip.w - endClip.w);
    rayEnd = rayOrigin + reflectDir * MAX_RAY_DISTANCE * tClip;
    endClip = u_Frame.proj * vec4(rayEnd, 1.0);
  }

  // Screen-space coordinates
  vec2 startScreen = startClip.xy / startClip.w * 0.5 + 0.5;
  vec2 endScreen = endClip.xy / endClip.w * 0.5 + 0.5;
  float startDepthNDC = startClip.z / startClip.w;
  float endDepthNDC = endClip.z / endClip.w;

  vec2 screenSize = vec2(float(u_Frame.screenWidth), float(u_Frame.screenHeight));
  vec2 rayDelta = endScreen - startScreen;
  float rayScreenLen = length(rayDelta * screenSize);

  if (rayScreenLen < 1.0)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  float originLinearDepth = linearizeDepth(depth);

  // How steeply the reflection leaves the surface (1.0 = perpendicular, 0.0 = grazing)
  float reflNormalDot = abs(dot(reflectDir, viewNormal));
  int maxMip = u_Frame.hizMipCount - 1;

  // --- Hi-Z Ray March ---
  bool hit = false;
  vec2 hitUV = vec2(0.0);
  float hitT = 0.0;
  // Stochastic jitter: sub-pixel offset that varies per pixel per frame.
  float jitterOffset = temporalNoise(gl_FragCoord.xy, u_Frame.time * 7.23, vec2(131.0, 257.0)) / rayScreenLen;

  // Cap mip level to limit staircase size at silhouettes
  int effectiveMaxMip = min(maxMip, 4);
  int mipLevel = 0;
  float t = jitterOffset;

  for (int i = 0; i < MAX_ITERATIONS; i++)
  {
    t += max(1.0, float(1 << mipLevel)) / rayScreenLen;
    if (t >= 1.0) break;

    vec2 sampleUV = mix(startScreen, endScreen, t);

    // Screen bounds check
    if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
      break;

    float rayDepth = mix(startDepthNDC, endDepthNDC, t);
    float cellMinDepth = textureLod(hiZTexture, sampleUV, float(mipLevel)).r;

    if (cellMinDepth >= DEPTH_EPSILON)
    {
      mipLevel = min(mipLevel + 1, effectiveMaxMip);
      continue;
    }

    if (rayDepth > cellMinDepth)
    {
      if (mipLevel == 0)
      {
        float sampleLinearDepth = linearizeDepth(cellMinDepth);

        // Reject self-intersection
        if (abs(sampleLinearDepth - originLinearDepth) < SELF_INTERSECT_DIST)
          continue;

        // Reject too-thick hits
        float rayLinearDepth = linearizeDepth(rayDepth);
        if (rayLinearDepth - sampleLinearDepth > MAX_THICKNESS)
        {
          mipLevel = min(mipLevel + 1, effectiveMaxMip);
          continue;
        }

        // Reject same-surface hits
        if (reflNormalDot < 0.5)
        {
          vec4 sampleGB1 = texture(gbuffer1Texture, sampleUV);
          vec3 sampleNormal = octDecode(sampleGB1.rg * 2.0 - 1.0);
          if (dot(sampleNormal, worldNormal) > SAME_SURFACE_THRESHOLD)
            continue;
        }

        hit = true;
        hitUV = sampleUV;
        hitT = t;
        break;
      }
      else
      {
        // Go finer for more precision
        mipLevel--;
        t -= max(1.0, float(1 << (mipLevel + 1))) / rayScreenLen;
        t = max(t, 0.0);
      }
    }
    else
    {
      mipLevel = min(mipLevel + 1, effectiveMaxMip);
    }
  }

  if (!hit)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  // Binary refinement in screen space
  float lo = max(hitT - 2.0 / rayScreenLen, 0.0);
  float hi = hitT;

  for (int j = 0; j < REFINEMENT_STEPS; j++)
  {
    float mid = (lo + hi) * 0.5;
    vec2 midScreen = mix(startScreen, endScreen, mid);

    if (any(lessThan(midScreen, vec2(0.0))) || any(greaterThan(midScreen, vec2(1.0))))
      break;

    float midRayDepth = mix(startDepthNDC, endDepthNDC, mid);
    float midSampleDepth = textureLod(hiZTexture, midScreen, 0.0).r;

    if (midSampleDepth >= DEPTH_EPSILON)
      lo = mid;
    else if (midRayDepth > midSampleDepth)
      hi = mid;
    else
      lo = mid;
  }

  // Final hit position
  float finalT = (lo + hi) * 0.5;
  hitUV = mix(startScreen, endScreen, finalT);
  hitT = finalT;

  // Silhouette edge detection
  vec2 texelSize = 1.0 / screenSize;
  float hitLinearDepth = linearizeDepth(textureLod(hiZTexture, hitUV, 0.0).r);
  float dU = abs(hitLinearDepth - linearizeDepth(textureLod(hiZTexture, hitUV + vec2(texelSize.x, 0.0), 0.0).r));
  float dD = abs(hitLinearDepth - linearizeDepth(textureLod(hiZTexture, hitUV - vec2(texelSize.x, 0.0), 0.0).r));
  float dR = abs(hitLinearDepth - linearizeDepth(textureLod(hiZTexture, hitUV + vec2(0.0, texelSize.y), 0.0).r));
  float dL = abs(hitLinearDepth - linearizeDepth(textureLod(hiZTexture, hitUV - vec2(0.0, texelSize.y), 0.0).r));
  float maxDepthDiff = max(max(dU, dD), max(dR, dL));
  float silhouetteFade = 1.0 - smoothstep(0.02, 0.6, maxDepthDiff);

  // Contact fade
  float hitScreenDist = hitT * rayScreenLen;
  float contactFade = smoothstep(0.0, 32.0, hitScreenDist);

  vec3 reflectedColor = texture(litColorTexture, hitUV).rgb;

  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  float NdotV = clamp(dot(-viewDir, viewNormal), 0.01, 1.0);
  vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

  vec2 edgeCoords = hitUV * 2.0 - 1.0;
  float edgeFade = (1.0 - smoothstep(EDGE_FADE_START, 1.0, abs(edgeCoords.x)))
                 * (1.0 - smoothstep(EDGE_FADE_START, 1.0, abs(edgeCoords.y)));

  float distanceFade = 1.0 - smoothstep(0.0, 1.0, hitT);

  float roughnessFade = 1.0 - smoothstep(0.0, MAX_ROUGHNESS, roughness);

  // Back-face rejection: fade hits where the surface faces away from the ray
  vec3 hitNormal = octDecode(texture(gbuffer1Texture, hitUV).rg * 2.0 - 1.0);
  vec3 reflectDirWorld = mat3(u_Frame.invView) * reflectDir;
  float backfaceFade = 1.0 - smoothstep(-0.17, 0.0, dot(reflectDirWorld, hitNormal));

  vec3 ssrMask = clamp(F * edgeFade * distanceFade * backwardFade * roughnessFade * silhouetteFade * contactFade * backfaceFade, vec3(0.0), vec3(1.0));

  outColor = vec4(originalColor * (1.0 - ssrMask) + reflectedColor * ssrMask, 1.0);
}
