#ifdef __cplusplus
#pragma once
#define uint uint32_t
namespace YAEngine {
#endif

#define TILE_SIZE 16
#define MAX_LIGHTS_PER_TILE 128

struct TileData
{
  uint pointCount;
  uint spotCount;
  uint indices[MAX_LIGHTS_PER_TILE];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef uint
#endif
