float interleavedGradientNoise(vec2 pos)
{
  return fract(52.9829189 * fract(dot(pos, vec2(0.06711056, 0.00583715))));
}

float temporalNoise(vec2 fragCoord, float seed, vec2 offset)
{
  return interleavedGradientNoise(fragCoord + fract(seed) * offset);
}
