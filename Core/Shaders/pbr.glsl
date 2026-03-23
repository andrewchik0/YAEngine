const float PI = 3.1415926535;

// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
// efficient VanDerCorpus calculation.
float radical_inverse_VdC(uint bits)
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
  return vec2(float(i) / float(N), radical_inverse_VdC(i));
}

vec3 fresnel_schlick_roughness(float cosTheta, vec3 f0, float roughness)
{
  return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - cosTheta, 5.0);
}
vec3 fresnel_schlick(float cosTheta, vec3 f0)
{
  float t = 1.0 - max(cosTheta, 0.0);
  float t2 = t * t;
  return f0 + (vec3(1.0) - f0) * (t2 * t2 * t);
}

float normal_distribution_GGX(float alpha, float NdotH)
{
  float alpha2 = alpha * alpha;

  float denominator = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
  denominator *= denominator * PI;
  denominator = max(denominator, 1e-5);

  return alpha2 / denominator;
}
vec3 importance_sample_GGX(vec2 Xi, vec3 N, float roughness)
{
  float a = roughness*roughness;

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

float geometry_schlick_GGX(float alpha, float cosTheta)
{
  float numerator = max(cosTheta, 0.0);
  float k = alpha / 2.0;

  float denominator = max(cosTheta, 0.0) * (1.0 - k) + k;
  denominator = max(denominator, 1e-5);

  return numerator / denominator;
}
float geometry_smith(float alpha, float NdotV, float NdotL)
{
  return geometry_schlick_GGX(alpha, NdotV) * geometry_schlick_GGX(alpha, NdotL);
}

// Rendering Equation for one light source
vec3 PBR(
  vec3 lightDir, vec3 lightColor, vec3 halfWayVec, vec3 viewVec, vec3 normal, vec3 f0, float VdotN,
  float metallic, vec3 albedo, float alpha, vec3 emissivity)
{
  float VdotH = max(dot(viewVec, halfWayVec), 0.0);
  float LdotN = max(dot(lightDir, normal), 0.0);
  float HdotN = max(dot(halfWayVec, normal), 0.0);

  vec3 kSpecular = fresnel_schlick(VdotH, f0);
  vec3 kDiffuse = (vec3(1.0) - kSpecular) * (1.0 - metallic);

  vec3 lambert = albedo / PI;

  vec3 cookTorranceNumerator = normal_distribution_GGX(alpha, HdotN) * geometry_smith(alpha, VdotN, LdotN) * kSpecular;
  float cookTorranceDenominator = 4.0 * max(VdotN, 0.0) * max(LdotN, 0.0);
  cookTorranceDenominator = max(cookTorranceDenominator, 1e-5);
  vec3 cookTorrance = cookTorranceNumerator / cookTorranceDenominator;

  vec3 BRDF = kDiffuse * lambert + cookTorrance;

  vec3 outgoingLight = emissivity + BRDF * lightColor * max(LdotN, 0.0);

  return outgoingLight;
}