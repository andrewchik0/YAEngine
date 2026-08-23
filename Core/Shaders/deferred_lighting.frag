layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "octahedron.glsl"
#include "lighting_common.glsl"

// G-buffer (set 1)
layout(set = 1, binding = 0) uniform sampler2D gbuffer0Texture;
layout(set = 1, binding = 1) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 2) uniform sampler2D depthTexture;
layout(set = 1, binding = 3) uniform sampler2D aoTexture;

const float SKY_DEPTH = 0.0;

void main()
{
  float depth = texture(depthTexture, uv).r;

  if (depth <= SKY_DEPTH)
  {
    // Sky pixels never sample a probe. Black keeps them from reading as coverage;
    // the Ambient views leave the sky visible so the image stays recognisable.
    if (u_Frame.currentTexture == DEBUG_VIEW_PROBE_INDEX
      || u_Frame.currentTexture == DEBUG_VIEW_PROBE_FALLBACK
      || u_Frame.currentTexture == DEBUG_VIEW_VOLUME_COVERAGE)
    {
      outColor = vec4(0.0, 0.0, 0.0, 1.0);
      return;
    }

    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, 1.0, 1.0);
    vec4 viewPos = u_Frame.invProj * clipPos;
    vec3 worldDir = normalize(mat3(u_Frame.invView) * viewPos.xyz);
    vec3 skyColor = texture(skyboxCubemap, worldDir).rgb;

    if (u_Frame.fogEnabled != 0)
    {
      float fogAmount = computeHeightFog(u_Frame.cameraPosition, worldDir, u_Frame.farPlane);
      skyColor = mix(skyColor, u_Frame.fogColor, fogAmount);
    }

    outColor = vec4(skyColor, 1.0);
    return;
  }

  vec4 gb0 = texture(gbuffer0Texture, uv);
  vec4 gb1 = texture(gbuffer1Texture, uv);

  vec3 albedo = gb0.rgb;
  float metallic = gb0.a;

  vec3 normal = octDecode(gb1.rg * 2.0 - 1.0);
  float roughness = gb1.b;
  int shadingModel = decodeShadingModel(gb1.a);

  if (shadingModel == SHADING_UNLIT)
  {
    // Unlit bypasses the IBL path entirely, so every indirect view reads black here
    if (isIndirectDebugView(u_Frame.currentTexture))
    {
      outColor = vec4(0.0, 0.0, 0.0, 1.0);
      return;
    }

    outColor = vec4(albedo, 1.0);
    return;
  }

  vec3 viewPos = reconstructViewPos(uv, depth);
  vec3 worldPos = (u_Frame.invView * vec4(viewPos, 1.0)).xyz;

  if (shadingModel == SHADING_EMISSIVE)
  {
    // Emission has no indirect term of its own, so every indirect view reads black here
    if (isIndirectDebugView(u_Frame.currentTexture))
    {
      outColor = vec4(0.0, 0.0, 0.0, 1.0);
      return;
    }

    // Fog still applies, unlike Unlit: a distant sign has to sit behind the same haze as
    // the wall it is bolted to, or it punches a hole through the depth cue.
    vec3 emissive = decodeEmissive(gb0);

    if (u_Frame.fogEnabled != 0)
    {
      vec3 toPixel = worldPos - u_Frame.cameraPosition;
      float rayLength = length(toPixel);
      float fogAmount = computeHeightFog(u_Frame.cameraPosition, toPixel / rayLength, rayLength);
      emissive = mix(emissive, u_Frame.fogColor, fogAmount);
    }

    outColor = vec4(emissive, 1.0);
    return;
  }

  vec3 viewVec = normalize(u_Frame.cameraPosition - worldPos);
  float NdotV = clamp(abs(dot(normal, viewVec)), 0.01, 0.99);
  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  vec3 R = reflect(-viewVec, normal);

  vec3 ambientDiffuse;
  vec3 ambientSpecular;
  vec3 ambient = computeAmbientIBLSplit(worldPos, normal, R, roughness, NdotV,
    f0, albedo, metallic, ambientDiffuse, ambientSpecular);

  // Indirect debug views return the raw diagnostic value and stop here. AO, fog,
  // tone mapping, exposure and bloom are all downstream of this point, and SSR/TAA
  // are forced off by Render while one of these views is active, so what reaches
  // the screen is exactly what was computed above.
  if (u_Frame.currentTexture == DEBUG_VIEW_AMBIENT_ONLY)
  {
    outColor = vec4(ambient, 1.0);
    return;
  }
  // The two halves of the view above, in the same linear units, so they add back
  // up to it: irradiance volumes feed the first, reflection probes the second.
  if (u_Frame.currentTexture == DEBUG_VIEW_AMBIENT_DIFFUSE)
  {
    outColor = vec4(ambientDiffuse, 1.0);
    return;
  }
  if (u_Frame.currentTexture == DEBUG_VIEW_AMBIENT_SPECULAR)
  {
    outColor = vec4(ambientSpecular, 1.0);
    return;
  }
  if (u_Frame.currentTexture == DEBUG_VIEW_PROBE_INDEX)
  {
    outColor = vec4(probeDebugColor(g_ProbeDebugTopIndex), 1.0);
    return;
  }
  if (u_Frame.currentTexture == DEBUG_VIEW_PROBE_FALLBACK)
  {
    outColor = vec4(probeFallbackHeat(g_ProbeDebugRemaining), 1.0);
    return;
  }
  if (u_Frame.currentTexture == DEBUG_VIEW_VOLUME_COVERAGE)
  {
    // Black means no volume covered the pixel and diffuse came from the skybox
    outColor = vec4(probeDebugColor(g_VolumeDebugIndex), 1.0);
    return;
  }

  // AO is applied here, not after tone mapping: it is derived from the jittered depth buffer
  // and changes every frame, so it has to go through TAA or it flickers on fine geometry.
  // It only modulates the indirect terms. Direct light already carries its own exact
  // per-direction visibility from the shadow maps, and AO is the hemisphere average of that
  // same visibility, so multiplying it in there would count the occlusion twice.
  if (u_Frame.aoEnabled != 0)
  {
    float ao = texture(aoTexture, uv).r;

    vec3 diffuseOcclusion = mix(vec3(ao), gtaoMultiBounce(ao, albedo), u_Frame.aoMultiBounce);
    float specularOcclusion = computeSpecularOcclusion(NdotV, ao, roughness);

    ambientDiffuse *= mix(vec3(1.0), diffuseOcclusion, u_Frame.aoStrength);
    ambientSpecular *= mix(1.0, specularOcclusion, u_Frame.aoSpecularStrength);
  }

  vec3 Lo = computeDirectLighting(worldPos, viewPos, normal, viewVec, albedo, metallic, roughness, f0, NdotV, ivec2(gl_FragCoord.xy));

  vec3 resultColor = max(ambientDiffuse + ambientSpecular + Lo, vec3(0.0));

  if (u_Frame.fogEnabled != 0)
  {
    vec3 toPixel = worldPos - u_Frame.cameraPosition;
    float rayLength = length(toPixel);
    float fogAmount = computeHeightFog(u_Frame.cameraPosition, toPixel / rayLength, rayLength);
    resultColor = mix(resultColor, u_Frame.fogColor, fogAmount);
  }

  outColor = vec4(resultColor, 1.0);
}
