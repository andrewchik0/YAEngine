const float PI = 3.14159265359;

// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float radicalInverseVdC(uint bits)
{
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 hammersley(uint i, uint N)
{
  return vec2(float(i) / float(N), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
  float a = roughness * roughness;

  float phi = 2.0 * PI * Xi.x;
  float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
  float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

  vec3 H;
  H.x = cos(phi) * sinTheta;
  H.y = sin(phi) * sinTheta;
  H.z = cosTheta;

  vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 tangent = normalize(cross(up, N));
  vec3 bitangent = cross(N, tangent);

  vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
  return normalize(sampleVec);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0)
{
  float t = clamp(1.0 - cosTheta, 0.0, 1.0);
  float t2 = t * t;
  return f0 + (vec3(1.0) - f0) * (t2 * t2 * t);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
{
  float t = clamp(1.0 - cosTheta, 0.0, 1.0);
  float t2 = t * t;
  return f0 + (max(vec3(1.0 - roughness), f0) - f0) * (t2 * t2 * t);
}

float normalDistributionGGX(float alpha, float NdotH)
{
  float alpha2 = alpha * alpha;

  float denominator = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
  denominator *= denominator * PI;
  denominator = max(denominator, 1e-5);

  return alpha2 / denominator;
}

float geometrySchlickGGX(float alpha, float cosTheta)
{
  float numerator = max(cosTheta, 0.0);
  float k = alpha / 2.0;

  float denominator = max(cosTheta, 0.0) * (1.0 - k) + k;
  denominator = max(denominator, 1e-5);

  return numerator / denominator;
}

float geometrySmith(float alpha, float NdotV, float NdotL)
{
  return geometrySchlickGGX(alpha, NdotV) * geometrySchlickGGX(alpha, NdotL);
}
