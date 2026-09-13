#ifndef RANDOM_GLSL
#define RANDOM_GLSL

// One 32-bit state word per invocation, hashed forward on every draw. A path tracer needs
// a different sequence for every pixel AND every frame, so nothing here is table driven:
// a LUT indexed by pixel would repeat across frames, one indexed by frame would repeat
// across the screen, and a blue noise texture only helps where the sample count is fixed.

// PCG-RXS-M-XS, one round, from Jarzynski & Olano, "Hash Functions for GPU Rendering"
// (JCGT 2020). Cheap enough to serve as both the seed mixer and the stream itself.
uint pcgHash(uint value)
{
  uint state = value * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

// Pixel and frame are hashed into each other rather than concatenated: consecutive frames
// of a neighbouring pixel must not walk into each other's sequences, which is exactly what
// a plain packed index would do.
uint initRandomSeed(uvec2 pixel, uint frameIndex)
{
  return pcgHash(pixel.x ^ pcgHash(pixel.y ^ pcgHash(frameIndex)));
}

uint nextRandomUint(inout uint state)
{
  state = pcgHash(state);
  return state;
}

// Half-open [0, 1). The top 24 bits are used rather than all 32 because float(0xFFFFFFFF)
// rounds up to 2^32 in single precision and the scaled result would be exactly 1.0 - which
// a pdf built from it may never be.
float randomFloat(inout uint state)
{
  return float(nextRandomUint(state) >> 8) * (1.0 / 16777216.0);
}

vec2 randomFloat2(inout uint state)
{
  float x = randomFloat(state);
  float y = randomFloat(state);
  return vec2(x, y);
}

#endif
