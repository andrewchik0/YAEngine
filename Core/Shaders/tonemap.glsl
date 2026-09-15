// ACES filmic tone mapping: Narkowicz's 2015 fit of the RRT and ODT, evaluated in ACEScg.
// The fit approximates a curve that is defined on AP1 primaries; handing it sRGB primaries
// directly compresses each channel on its own and leaves saturated surfaces looking neon,
// so the color is taken into AP1 and back around it (Stephen Hill's matrices).
vec3 acesTonemap(vec3 color)
{
  const mat3 acesInputMatrix = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
  );

  const mat3 acesOutputMatrix = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602
  );

  color = acesInputMatrix * color;
  vec3 numerator = color * (color + 0.0245786) - 0.000090537;
  vec3 denominator = color * (0.983729 * color + 0.432951) + 0.238081;
  color = acesOutputMatrix * (numerator / denominator);

  return clamp(color, 0.0, 1.0);
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

// Contrast and saturation on top of the tone mapped image - the grade stage both operators
// expect. AgX is deliberately flat on its own and is always shipped with a look; ACES has no
// knob at all without one. Both are applied in display encoded space, where a power reads as
// contrast rather than as an exposure shift, which is also where the reference AgX looks
// define their numbers.
vec3 applyLook(vec3 color)
{
  if (u_Frame.tonemapPower == 1.0 && u_Frame.tonemapSaturation == 1.0)
    return color;

  vec3 encoded = pow(max(color, 0.0), vec3(1.0 / 2.2));
  float luma = dot(encoded, vec3(0.2126, 0.7152, 0.0722));
  encoded = pow(encoded, vec3(u_Frame.tonemapPower));
  encoded = luma + u_Frame.tonemapSaturation * (encoded - luma);

  return pow(max(encoded, 0.0), vec3(2.2));
}

vec3 applyTonemap(vec3 color)
{
  vec3 mapped = u_Frame.tonemapMode == TONEMAP_AGX ? agxTonemap(color) : acesTonemap(color);
  return applyLook(mapped);
}
