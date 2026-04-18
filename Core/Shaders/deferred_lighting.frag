layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

#include "octahedron.glsl"
#include "lighting_common.glsl"

// G-buffer (set 1)
layout(set = 1, binding = 0) uniform sampler2D gbuffer0Texture;
layout(set = 1, binding = 1) uniform sampler2D gbuffer1Texture;
layout(set = 1, binding = 2) uniform sampler2D depthTexture;

const float DEPTH_EPSILON = 1.0;
const int SHADING_PBR = 0;
const int SHADING_UNLIT = 1;

void main()
{
  float depth = texture(depthTexture, uv).r;

  if (depth >= DEPTH_EPSILON)
  {
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
  int shadingModel = int(gb1.a * 3.0 + 0.5);

  if (shadingModel == SHADING_UNLIT)
  {
    outColor = vec4(albedo, 1.0);
    return;
  }

  vec3 viewPos = reconstructViewPos(uv, depth);
  vec3 worldPos = (u_Frame.invView * vec4(viewPos, 1.0)).xyz;

  vec3 viewVec = normalize(u_Frame.cameraPosition - worldPos);
  float NdotV = clamp(abs(dot(normal, viewVec)), 0.01, 0.99);
  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  vec3 R = reflect(-viewVec, normal);

  vec3 ambient = computeAmbientIBL(worldPos, normal, R, roughness, NdotV, f0, albedo, metallic);
  vec3 Lo = computeDirectLighting(worldPos, viewPos, normal, viewVec, albedo, metallic, roughness, f0, NdotV, ivec2(gl_FragCoord.xy));

  vec3 resultColor = max(ambient + Lo, vec3(0.0));

  if (u_Frame.fogEnabled != 0)
  {
    vec3 toPixel = worldPos - u_Frame.cameraPosition;
    float rayLength = length(toPixel);
    float fogAmount = computeHeightFog(u_Frame.cameraPosition, toPixel / rayLength, rayLength);
    resultColor = mix(resultColor, u_Frame.fogColor, fogAmount);
  }

  outColor = vec4(resultColor, 1.0);
}
