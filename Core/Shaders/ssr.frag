#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform PerFrameUBO {
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
  float near;
  float far;
  float fov;
  int screenWidth;
  int screenHeight;
  int ssaoEnabled;
  int ssrEnabled;
  int taaEnabled;
  float jitterX;
  float jitterY;
} u_Data;

layout(set = 1, binding = 0) uniform sampler2D frame;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;
layout(set = 1, binding = 2) uniform sampler2D normalTexture;
layout(set = 1, binding = 3) uniform sampler2D materialTexture;
layout(set = 1, binding = 4) uniform sampler2D albedoTexture;
layout(set = 1, binding = 5) uniform sampler2D ssaoTexture;

// --- Configuration constants ---
const int MAX_STEPS = 256;
const int REFINEMENT_STEPS = 8;
const float MAX_RAY_DISTANCE = 30.0;
const float SELF_INTERSECT_DIST = 0.1;
const float NORMAL_BIAS = 0.02;
const float MAX_ROUGHNESS = 0.6;
const float EDGE_FADE_START = 0.8;
const float DEPTH_EPSILON = 0.9999;
const float SAME_SURFACE_THRESHOLD = 0.99;
const float MAX_THICKNESS = 2.0;

// --- Helper functions ---

vec3 reconstructViewPos(vec2 screenUV, float depth)
{
  vec2 ndc = screenUV * 2.0 - 1.0;
  vec4 clipPos = vec4(ndc, depth, 1.0);
  vec4 viewPos = u_Data.invProj * clipPos;
  return viewPos.xyz / viewPos.w;
}

float linearizeDepth(float depth)
{
  float num = u_Data.invProj[2][2] * depth + u_Data.invProj[3][2];
  float den = u_Data.invProj[2][3] * depth + u_Data.invProj[3][3];
  return -num / den;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
  float t = clamp(1.0 - cosTheta, 0.0, 1.0);
  float t2 = t * t;
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) * t2 * t2 * t;
}

// --- Main ---

void main()
{
  // Read G-buffer
  float depth = texture(depthTexture, uv).r;
  vec3 worldNormal = texture(normalTexture, uv).rgb;
  vec2 material = texture(materialTexture, uv).rg;
  float roughness = material.r;
  float metallic = material.g;
  vec3 albedo = texture(albedoTexture, uv).rgb;
  vec3 originalColor = texture(frame, uv).rgb;
  float ao = texture(ssaoTexture, uv).r;

  if (u_Data.ssaoEnabled == 0)
    ao = 1.0;

  // SSR-only debug: zero out base color to show only reflections
  if (u_Data.currentTexture == 6)
    originalColor = vec3(0.0);
  else
    originalColor *= ao;

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

  if (u_Data.ssrEnabled == 0 || roughness > MAX_ROUGHNESS)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  // Reconstruct view-space position
  vec3 viewPos = reconstructViewPos(uv, depth);

  // Transform normal to view space
  vec3 viewNormal = normalize(mat3(u_Data.view) * worldNormal);

  // Reflection direction
  vec3 viewDir = normalize(viewPos);
  vec3 reflectDir = reflect(viewDir, viewNormal);

  // Backward fade for reflections pointing towards camera
  float backwardFade = 1.0 - clamp(reflectDir.z, 0.0, 1.0);

  // --- Screen-space ray march ---
  vec3 rayOrigin = viewPos + viewNormal * NORMAL_BIAS;
  vec3 rayEnd = rayOrigin + reflectDir * MAX_RAY_DISTANCE;

  // Project start and end to clip space
  vec4 startClip = u_Data.proj * vec4(rayOrigin, 1.0);
  vec4 endClip = u_Data.proj * vec4(rayEnd, 1.0);

  // If end is behind camera, clip the ray to stay in front
  if (endClip.w <= 0.0)
  {
    float tClip = (startClip.w - 0.01) / (startClip.w - endClip.w);
    rayEnd = rayOrigin + reflectDir * MAX_RAY_DISTANCE * tClip;
    endClip = u_Data.proj * vec4(rayEnd, 1.0);
  }

  // Screen-space coordinates
  vec2 startScreen = startClip.xy / startClip.w * 0.5 + 0.5;
  vec2 endScreen = endClip.xy / endClip.w * 0.5 + 0.5;
  float startDepthNDC = startClip.z / startClip.w;
  float endDepthNDC = endClip.z / endClip.w;

  // Determine step count from screen-space pixel distance
  vec2 screenDelta = (endScreen - startScreen) * vec2(float(u_Data.screenWidth), float(u_Data.screenHeight));
  int steps = clamp(int(max(abs(screenDelta.x), abs(screenDelta.y))), 1, MAX_STEPS);

  // March in screen space
  bool hit = false;
  vec2 hitUV = vec2(0.0);
  float hitT = 0.0;
  int hitStep = 0;

  float originLinearDepth = linearizeDepth(depth);

  float invSteps = 1.0 / float(steps);

  for (int i = 1; i <= steps; i++)
  {
    float t = float(i) * invSteps;
    vec2 sampleUV = mix(startScreen, endScreen, t);

    // Screen bounds check
    if (any(notEqual(clamp(sampleUV, 0.0, 1.0), sampleUV)))
      break;

    // Linearly interpolated ray depth in NDC (perspective-correct in screen space)
    float rayDepth = mix(startDepthNDC, endDepthNDC, t);

    float sampleDepth = texture(depthTexture, sampleUV).r;
    if (sampleDepth >= DEPTH_EPSILON)
      continue;

    float diff = rayDepth - sampleDepth;
    if (diff > 0.0)
    {
      float sampleLinearDepth = linearizeDepth(sampleDepth);

      // Reject close self-intersections by depth
      if (abs(sampleLinearDepth - originLinearDepth) < SELF_INTERSECT_DIST)
        continue;

      // Reject hits where ray is too far behind the surface (edge artifacts)
      float rayLinearDepth = linearizeDepth(rayDepth);
      if (rayLinearDepth - sampleLinearDepth > MAX_THICKNESS)
        continue;

      // Reject same-surface hits by normal similarity
      vec3 sampleNormalRaw = texture(normalTexture, sampleUV).rgb;
      if (dot(sampleNormalRaw, sampleNormalRaw) < 0.000001)
        continue;
      vec3 sampleNormal = normalize(sampleNormalRaw);
      if (dot(sampleNormal, worldNormal) > SAME_SURFACE_THRESHOLD)
        continue;

      hit = true;
      hitUV = sampleUV;
      hitT = t;
      hitStep = i;
      break;
    }
  }

  if (!hit)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  // Binary refinement in screen space
  float lo = float(hitStep - 1) * invSteps;
  float hi = float(hitStep) * invSteps;

  for (int j = 0; j < REFINEMENT_STEPS; j++)
  {
    float mid = (lo + hi) * 0.5;
    vec2 midScreen = mix(startScreen, endScreen, mid);

    if (any(notEqual(clamp(midScreen, 0.0, 1.0), midScreen)))
      break;

    float midRayDepth = mix(startDepthNDC, endDepthNDC, mid);
    float midSampleDepth = texture(depthTexture, midScreen).r;

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

  // Contribution calculation
  vec3 reflectedColor = texture(frame, hitUV).rgb;

  // Fresnel
  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  float NdotV = clamp(dot(-viewDir, viewNormal), 0.01, 1.0);
  vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

  // Screen edge fade
  vec2 edgeCoords = hitUV * 2.0 - 1.0;
  float edgeFade = (1.0 - smoothstep(EDGE_FADE_START, 1.0, abs(edgeCoords.x)))
                 * (1.0 - smoothstep(EDGE_FADE_START, 1.0, abs(edgeCoords.y)));

  // Distance fade
  float distanceFade = 1.0 - smoothstep(0.0, 1.0, hitT);

  // Roughness fade
  float roughnessFade = 1.0 - smoothstep(0.0, MAX_ROUGHNESS, roughness);

  // Final SSR mask
  vec3 ssrMask = clamp(F * edgeFade * distanceFade * backwardFade * roughnessFade, vec3(0.0), vec3(1.0));

  outColor = vec4(originalColor * (1.0 - ssrMask) + reflectedColor * ssrMask, 1.0);
}
