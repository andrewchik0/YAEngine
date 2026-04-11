layout(set = 0, binding = 0) uniform samplerCube uCubemap;

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

void main()
{
  vec3 N = normalize(vDir);
  vec3 irradiance = vec3(0.0);

  vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
  vec3 right = normalize(cross(up, N));
  up         = normalize(cross(N, right));

  float sampleDelta = 0.025;
  float nrSamples = 0.0;

  float srcRes = float(textureSize(uCubemap, 0).x);
  float saTexel = 4.0 * PI / (6.0 * srcRes * srcRes);

  for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
  {
    for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
    {
      float sinTheta = sin(theta);
      float cosTheta = cos(theta);

      vec3 tangentSample = vec3(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta
      );

      vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

      // Sample from pre-filtered mip to avoid aliasing from bright HDR spots
      float saSample = sampleDelta * sampleDelta * sinTheta;
      float mipLevel = 0.5 * log2(max(saSample / saTexel, 1.0)) + 1.0;

      irradiance += textureLod(uCubemap, sampleVec, mipLevel).rgb * cosTheta * sinTheta;
      nrSamples += 1.0;
    }
  }

  irradiance = PI * irradiance / nrSamples;

  outColor = vec4(irradiance, 1.0);
}