layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "common.glsl"
#include "utils.glsl"
#include "octahedron.glsl"
#include "pbr.glsl"
#include "noise.glsl"
#include "hiz_trace.glsl"

layout(set = 1, binding = 0) uniform sampler2D litColorTexture;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;
layout(set = 1, binding = 2) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 3) uniform sampler2D gbuffer0Texture;
layout(set = 1, binding = 4) uniform sampler2D hiZTexture;

// --- Configuration ---
// The cell-crossing traversal converges long before this; it only bounds runaway rays.
const int MAX_ITERATIONS = 64;
const float MAX_RAY_DISTANCE = 30.0;
const float NORMAL_BIAS = 0.001;
const float MAX_ROUGHNESS = 0.5;
const float EDGE_FADE_START = 0.8;
const float DEPTH_EPSILON = 1.0;
const float SAME_SURFACE_THRESHOLD = 0.99;
const float MAX_THICKNESS = 4.0;
// Rays whose reflection could not blend in more than this weight are not traced at all.
// The bound below is exact: every other factor in ssrMask only shrinks it further.
const float MIN_CONTRIBUTION = 0.04;
// A ray stuck crawling cell-by-cell behind geometry ends as a miss after this many
// consecutive thickness rejections.
const int MAX_CONSECUTIVE_REJECTS = 12;

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

  // How steeply the reflection leaves the surface (1.0 = perpendicular, 0.0 = grazing)
  float reflNormalDot = abs(dot(reflectDir, viewNormal));

  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  float NdotV = clamp(dot(-viewDir, viewNormal), 0.01, 1.0);
  vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
  float roughnessFade = 1.0 - smoothstep(0.0, MAX_ROUGHNESS, roughness);

  // Upper bound of the blend weight this reflection could reach. On this scene's terrain
  // (uniform roughness ~0.4) most of the frame traces rays whose result is invisible; the
  // profile showed ~70-90% of all traversal iterations spent under a few percent of weight.
  float contribution = max(max(F.r, F.g), F.b) * roughnessFade * u_Frame.ssrIntensity;
  if (contribution < MIN_CONTRIBUTION)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  HiZTraceResult trace = hiZTrace(
    hiZTexture,
    ivec2(u_Frame.screenWidth, u_Frame.screenHeight),
    u_Frame.hizMipCount - 1,
    MAX_ITERATIONS,
    vec3(startScreen, startDepthNDC),
    vec3(endScreen, endDepthNDC),
    MAX_THICKNESS,
    MAX_CONSECUTIVE_REJECTS);

  if (!trace.hit)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  vec2 hitUV = trace.uv;
  float hitT = trace.t;

  vec3 hitNormal = octDecode(texture(gbuffer1Texture, hitUV).rg * 2.0 - 1.0);

  // A grazing ray skims along its own surface, where nothing in screen space distinguishes a
  // reflection from the surface the ray started on.
  if (reflNormalDot < 0.5 && dot(hitNormal, worldNormal) > SAME_SURFACE_THRESHOLD)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

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

  vec2 edgeCoords = hitUV * 2.0 - 1.0;
  float edgeFade = (1.0 - smoothstep(EDGE_FADE_START, 1.0, abs(edgeCoords.x)))
                 * (1.0 - smoothstep(EDGE_FADE_START, 1.0, abs(edgeCoords.y)));

  float distanceFade = 1.0 - smoothstep(0.0, 1.0, hitT);

  // Hide the gate boundary: weights just above the cutoff fade in from zero
  float gateFade = smoothstep(MIN_CONTRIBUTION, MIN_CONTRIBUTION * 2.0, contribution);

  // Back-face rejection: fade hits where the surface faces away from the ray
  vec3 reflectDirWorld = mat3(u_Frame.invView) * reflectDir;
  float backfaceFade = 1.0 - smoothstep(-0.17, 0.0, dot(reflectDirWorld, hitNormal));

  vec3 ssrMask = clamp(F * edgeFade * distanceFade * backwardFade * roughnessFade * silhouetteFade * contactFade * backfaceFade
                       * gateFade * u_Frame.ssrIntensity, vec3(0.0), vec3(1.0));

  outColor = vec4(originalColor * (1.0 - ssrMask) + reflectedColor * ssrMask, 1.0);
}
