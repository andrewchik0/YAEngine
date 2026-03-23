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
} u_Data;

layout(set = 1, binding = 0) uniform sampler2D frame;
layout(set = 1, binding = 1) uniform sampler2D depthTexture;
layout(set = 1, binding = 2) uniform sampler2D normalTexture;
layout(set = 1, binding = 3) uniform sampler2D materialTexture;
layout(set = 1, binding = 4) uniform sampler2D albedoTexture;

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
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// --- Main ---

void main()
{
  // Read G-buffer
  float depth = texture(depthTexture, uv).r;
  vec3 worldNormal = normalize(texture(normalTexture, uv).rgb);
  float roughness = texture(materialTexture, uv).r;
  float metallic = texture(materialTexture, uv).g;
  vec3 albedo = texture(albedoTexture, uv).rgb;
  vec3 originalColor = texture(frame, uv).rgb;

  // --- SSR Debug: full-screen modes (before early exits) ---
  if (u_Data.currentTexture == 10) // Raw depth buffer
  {
    outColor = vec4(vec3(depth), 1.0);
    return;
  }
  if (u_Data.currentTexture == 11) // World normal from G-buffer
  {
    if (depth >= DEPTH_EPSILON) { outColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    outColor = vec4(worldNormal * 0.5 + 0.5, 1.0);
    return;
  }

  // Early exits
  if (depth >= DEPTH_EPSILON)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  if (length(worldNormal) < 0.001)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  if (roughness > MAX_ROUGHNESS)
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

  // --- SSR Debug: reflection modes ---
  if (u_Data.currentTexture == 12) // View-space normal
  {
    outColor = vec4(viewNormal * 0.5 + 0.5, 1.0);
    return;
  }
  if (u_Data.currentTexture == 13) // Reflection direction
  {
    outColor = vec4(reflectDir * 0.5 + 0.5, 1.0);
    return;
  }

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

  // Debug: track first raw hit (before rejection checks)
  bool rawHit = false;
  vec2 rawHitUV = vec2(0.0);
  float rawHitT = 0.0;

  float originLinearDepth = linearizeDepth(depth);

  for (int i = 1; i <= steps; i++)
  {
    float t = float(i) / float(steps);
    vec2 sampleUV = mix(startScreen, endScreen, t);

    // Screen bounds check
    if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
      break;

    // Linearly interpolated ray depth in NDC (perspective-correct in screen space)
    float rayDepth = mix(startDepthNDC, endDepthNDC, t);

    float sampleDepth = texture(depthTexture, sampleUV).r;
    if (sampleDepth >= DEPTH_EPSILON)
      continue;

    float diff = rayDepth - sampleDepth;
    if (diff > 0.0)
    {
      // Track first raw intersection for debug
      if (!rawHit)
      {
        rawHit = true;
        rawHitUV = sampleUV;
        rawHitT = t;
      }

      float sampleLinearDepth = linearizeDepth(sampleDepth);

      // Reject close self-intersections by depth
      if (abs(sampleLinearDepth - originLinearDepth) < SELF_INTERSECT_DIST)
        continue;

      // Reject hits where ray is too far behind the surface (edge artifacts)
      float rayLinearDepth = linearizeDepth(rayDepth);
      if (rayLinearDepth - sampleLinearDepth > MAX_THICKNESS)
        continue;

      // Reject same-surface hits by normal similarity
      vec3 sampleNormal = normalize(texture(normalTexture, sampleUV).rgb);
      if (dot(sampleNormal, worldNormal) > SAME_SURFACE_THRESHOLD)
        continue;

      hit = true;
      hitUV = sampleUV;
      hitT = t;
      hitStep = i;
      break;
    }
  }

  // --- SSR Debug: ray march result modes ---
  if (u_Data.currentTexture == 14) // Hit map: green=hit, red=miss
  {
    outColor = hit ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);
    return;
  }
  if (u_Data.currentTexture == 15) // Hit UV
  {
    outColor = hit ? vec4(hitUV, 0.0, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  if (u_Data.currentTexture == 16) // hitT: distance along ray (filtered hit)
  {
    outColor = hit ? vec4(vec3(hitT), 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  if (u_Data.currentTexture == 17) // Raw hit map (NO rejection checks)
  {
    outColor = rawHit ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);
    return;
  }
  if (u_Data.currentTexture == 18) // Screen-space ray direction
  {
    vec2 dir = normalize(endScreen - startScreen);
    outColor = vec4(dir * 0.5 + 0.5, 0.0, 1.0);
    return;
  }
  if (u_Data.currentTexture == 19) // Raw hitT (unfiltered, before rejections)
  {
    outColor = rawHit ? vec4(vec3(rawHitT), 1.0) : vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  if (u_Data.currentTexture == 20) // Depth values at raw hit: R=rayDepth, G=sampleDepth
  {
    if (rawHit)
    {
      float rawRayDepth = mix(startDepthNDC, endDepthNDC, rawHitT);
      float rawSampleDepth = texture(depthTexture, rawHitUV).r;
      outColor = vec4(rawRayDepth, rawSampleDepth, 0.0, 1.0);
    }
    else
      outColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }
  if (u_Data.currentTexture == 21) // Roundtrip test: viewPos -> proj -> screenUV vs uv
  {
    vec4 reprojClip = u_Data.proj * vec4(viewPos, 1.0);
    vec2 reprojUV = reprojClip.xy / reprojClip.w * 0.5 + 0.5;
    outColor = vec4(abs(reprojUV - uv) * 500.0, 0.0, 1.0);
    return;
  }
  if (u_Data.currentTexture == 22) // Simple UV-shift reflection (texture sanity check)
  {
    vec3 shiftedColor = texture(frame, uv + vec2(0.0, -0.2)).rgb;
    outColor = vec4(shiftedColor, 1.0);
    return;
  }
  if (u_Data.currentTexture == 23) // DEPTH roundtrip: proj(viewPos).z/w vs depth buffer
  {
    vec4 reprojClip = u_Data.proj * vec4(viewPos, 1.0);
    float reprojDepth = reprojClip.z / reprojClip.w;
    float diff = abs(reprojDepth - depth);
    outColor = vec4(vec3(diff * 10000.0), 1.0);
    return;
  }
  if (u_Data.currentTexture == 26) // Normal deviation from (0,1,0) x2
  {
    vec3 ideal = vec3(0.0, 1.0, 0.0);
    vec3 diff = abs(worldNormal - ideal) * 2.0;
    outColor = vec4(diff, 1.0);
    return;
  }
  if (u_Data.currentTexture == 27) // Angle from (0,1,0) in degrees / 90
  {
    float cosAngle = dot(worldNormal, vec3(0.0, 1.0, 0.0));
    float angle = acos(clamp(cosAngle, -1.0, 1.0));
    float normalized = angle / 1.5708; // 0=flat, 1=90 degrees
    outColor = vec4(vec3(normalized), 1.0);
    return;
  }
  if (u_Data.currentTexture == 24 || u_Data.currentTexture == 25) // Minimal SSR
  {
    vec3 rd = reflectDir;

    // Mode 25: override with analytically correct mirror reflection for Y=0 floor
    // Bypasses G-buffer normals entirely
    if (u_Data.currentTexture == 25)
    {
      // Convert viewDir to world space, flip Y, convert back to view space
      vec3 worldViewDir = transpose(mat3(u_Data.view)) * viewDir;
      vec3 worldReflect = vec3(worldViewDir.x, -worldViewDir.y, worldViewDir.z);
      rd = normalize(mat3(u_Data.view) * worldReflect);
    }

    vec3 ro = viewPos;
    vec3 re = ro + rd * 10.0;

    vec4 sc = u_Data.proj * vec4(ro, 1.0);
    vec4 ec = u_Data.proj * vec4(re, 1.0);

    if (ec.w <= 0.0) { outColor = vec4(0.0, 0.0, 0.0, 1.0); return; }

    vec2 ss = sc.xy / sc.w * 0.5 + 0.5;
    vec2 es = ec.xy / ec.w * 0.5 + 0.5;
    float sz = sc.z / sc.w;
    float ez = ec.z / ec.w;

    for (int i = 1; i <= 200; i++)
    {
      float t = float(i) / 200.0;
      vec2 suv = mix(ss, es, t);
      if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;

      float rz = mix(sz, ez, t);
      float sd = texture(depthTexture, suv).r;

      if (sd < 0.9999 && rz > sd && rz - sd < 0.005)
      {
        outColor = vec4(texture(frame, suv).rgb, 1.0);
        return;
      }
    }
    outColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  if (!hit)
  {
    outColor = vec4(originalColor, 1.0);
    return;
  }

  // Binary refinement in screen space
  float lo = float(hitStep - 1) / float(steps);
  float hi = float(hitStep) / float(steps);

  for (int j = 0; j < REFINEMENT_STEPS; j++)
  {
    float mid = (lo + hi) * 0.5;
    vec2 midScreen = mix(startScreen, endScreen, mid);

    if (midScreen.x < 0.0 || midScreen.x > 1.0 || midScreen.y < 0.0 || midScreen.y > 1.0)
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
  float roughnessFade = 1.0 - roughness * roughness;

  // Final SSR mask
  vec3 ssrMask = clamp(F * edgeFade * distanceFade * backwardFade * roughnessFade, vec3(0.0), vec3(1.0));

  outColor = vec4(originalColor * (1.0 - ssrMask) + reflectedColor * ssrMask, 1.0);
}
