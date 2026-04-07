// ACES filmic tone mapping (Narkowicz 2015)
vec3 acesFilm(vec3 x)
{
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((x*(a*x+b)) / (x*(c*x+d)+e), 0.0, 1.0);
}

// AgX tone mapping (Troy Sobotka / Blender)
// sRGB -> AgX log-encoded space
vec3 agxDefaultContrastApprox(vec3 x)
{
  vec3 x2 = x * x;
  vec3 x4 = x2 * x2;
  return + 15.5     * x4 * x2
         - 40.14    * x4 * x
         + 31.96    * x4
         - 6.868    * x2 * x
         + 0.4298   * x2
         + 0.1191   * x
         - 0.00232;
}

vec3 agxTonemap(vec3 color)
{
  const mat3 agxInsetMatrix = mat3(
    0.842479062253094,  0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772,  0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104
  );

  const mat3 agxOutsetMatrix = mat3(
     1.19687900512017,  -0.0528968517574562, -0.0529716355144438,
    -0.0980208811401368, 1.15190312990417,   -0.0980434066481512,
    -0.0990297440797205,-0.0989611768448433,  1.15107367264116
  );

  const float minEv = -12.47393;
  const float maxEv = 4.026069;

  color = agxInsetMatrix * color;
  color = clamp(log2(max(color, vec3(1e-10))), minEv, maxEv);
  color = (color - minEv) / (maxEv - minEv);
  color = agxDefaultContrastApprox(color);
  color = agxOutsetMatrix * color;
  color = pow(max(color, vec3(0.0)), vec3(2.2));

  return color;
}

vec3 applyTonemap(vec3 color)
{
  if (u_Frame.tonemapMode == TONEMAP_AGX)
    return agxTonemap(color);
  return acesFilm(color);
}
