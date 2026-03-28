layout(set = 0, binding = 0) uniform samplerCube skyboxSampler;

layout(location = 0) in vec3 inCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
  vec3 hdrColor = texture(skyboxSampler, normalize(inCoord)).rgb;
  fragColor = vec4(hdrColor, 1.0);
}
