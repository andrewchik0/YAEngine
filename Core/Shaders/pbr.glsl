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

// The denominator is written as (1 - xi) + a^2 * xi, not the textbook 1 + (a^2 - 1) * xi.
// The two are algebraically the same expression, so a converged image does not move - but the
// textbook one subtracts nearly equal numbers and all that survives the cancellation is
// a^2 * xi. At the small alphas the path tracer reaches just above its delta lobe threshold
// that remainder drops below half an ulp of 1.0, the quotient rounds to exactly 1, and the old
// sinTheta = sqrt(1 - cosTheta * cosTheta) took the square root of exactly zero - which is NaN
// wherever the driver lowers sqrt to x * inversesqrt(x). Reading cos^2 and sin^2 off the same
// denominator instead makes cos^2 <= 1 and sin^2 >= 0 true by construction, and for xi < 1 the
// denominator is a sum of two non-negative terms that are never both zero.
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
  float a = roughness * roughness;
  float a2Xi = a * a * Xi.y;
  float den = (1.0 - Xi.y) + a2Xi;

  float phi = 2.0 * PI * Xi.x;
  float cos2Theta = (1.0 - Xi.y) / den;
  float sin2Theta = a2Xi / den;

  // Xi.y == 0 puts sin^2 at exactly zero - 2^-24 of the samples, and the first sample of every
  // Hammersley sequence - and sqrt is no more trustworthy on a zero here than it was above.
  float cosTheta = cos2Theta > 0.0 ? sqrt(cos2Theta) : 0.0;
  float sinTheta = sin2Theta > 0.0 ? sqrt(sin2Theta) : 0.0;

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

// Cosine-weighted hemisphere sample around N, in the same tangent frame
// importanceSampleGGX builds so the two lobes of a path tracer cannot end up in different
// bases. The pdf is NdotL / PI, which is exactly what the Lambert term albedo / PI * NdotL
// cancels against - a caller sampling this way multiplies by the albedo and nothing else.
vec3 sampleCosineHemisphere(vec2 Xi, vec3 N)
{
  float phi = 2.0 * PI * Xi.x;
  float cosTheta = sqrt(1.0 - Xi.y);
  float sinTheta = sqrt(Xi.y);

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

float geometrySchlickGGX(float k, float cosTheta)
{
  float numerator = max(cosTheta, 0.0);

  float denominator = max(cosTheta, 0.0) * (1.0 - k) + k;
  denominator = max(denominator, 1e-5);

  return numerator / denominator;
}

float geometrySmith(float k, float NdotV, float NdotL)
{
  return geometrySchlickGGX(k, NdotV) * geometrySchlickGGX(k, NdotL);
}

// Diffuse AO is the cosine-weighted average of visibility over the whole hemisphere, so it
// over-occludes the narrow specular lobe on smooth surfaces and under-occludes it on rough
// ones. Empirical remap from "Moving Frostbite to PBR" (Lagarde & de Rousiers, 2014) that
// widens the aperture with roughness and grazing angles.
float computeSpecularOcclusion(float NdotV, float ao, float roughness)
{
  return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

// Pure visibility darkens too much because it ignores light bouncing back out of the
// occluded region, and how much comes back depends on how bright the surface is. Albedo-fitted
// polynomial from Jimenez et al., "Practical Real-Time Strategies for Accurate Indirect
// Occlusion" - the same paper GTAO itself comes from.
vec3 gtaoMultiBounce(float visibility, vec3 albedo)
{
  vec3 a =  2.0404 * albedo - 0.3324;
  vec3 b = -4.7951 * albedo + 0.6417;
  vec3 c =  2.7552 * albedo + 0.6903;
  return clamp(visibility * (a * visibility * visibility + b * visibility + c),
    vec3(visibility), vec3(1.0));
}

vec3 evaluateDirectLightSplit(
  vec3 N, vec3 V, vec3 L, vec3 radiance,
  vec3 albedo, float metallic, float roughness, float alpha, vec3 f0, float NdotV,
  out vec3 outDiffuse, out vec3 outSpecular)
{
  vec3 H = normalize(V + L);
  float NdotL = max(dot(N, L), 0.0);
  float NdotH = max(dot(N, H), 0.0);
  float HdotV = max(dot(H, V), 0.0);

  float NDF = normalDistributionGGX(alpha, NdotH);
  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  float G = geometrySmith(k, NdotV, NdotL);
  vec3 F = fresnelSchlick(HdotV, f0);

  vec3 numerator = NDF * G * F;
  float denominator = 4.0 * NdotV * NdotL + 0.0001;
  vec3 spec = numerator / denominator;

  vec3 kD = (1.0 - F) * (1.0 - metallic);
  outDiffuse = kD * albedo / PI * radiance * NdotL;
  outSpecular = spec * radiance * NdotL;
  return outDiffuse + outSpecular;
}

vec3 evaluateDirectLight(
  vec3 N, vec3 V, vec3 L, vec3 radiance,
  vec3 albedo, float metallic, float roughness, float alpha, vec3 f0, float NdotV)
{
  vec3 diffuse;
  vec3 specular;
  return evaluateDirectLightSplit(N, V, L, radiance, albedo, metallic, roughness,
    alpha, f0, NdotV, diffuse, specular);
}
