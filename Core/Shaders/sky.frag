layout(set = 0, binding = 0) uniform samplerCube skyboxSampler;

layout(location = 0) in vec3 inCoord;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec2 outMaterial;
layout(location = 3) out vec4 outAlbedo;
layout(location = 4) out vec2 outVelocity;

void main()
{
  vec3 hdrColor = texture(skyboxSampler, normalize(inCoord)).rgb;
  fragColor = vec4(hdrColor, 1.0);
  outNormal = vec4(0.0);
  outMaterial = vec2(1.0, 0.0);
  outAlbedo = vec4(0.0);
  outVelocity = vec2(0.0);
}
