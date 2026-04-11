#include "../Shared/TerrainMaterialUniforms.h"
layout(set = 1, binding = 0) uniform TerrainMaterialBlock { TerrainMaterialUniforms u_Terrain; };

layout(set = 1, binding = 1) uniform sampler2D layer0AlbedoTexture;
layout(set = 1, binding = 2) uniform sampler2D layer0NormalTexture;
layout(set = 1, binding = 3) uniform sampler2D layer0RoughnessTexture;
layout(set = 1, binding = 4) uniform sampler2D layer0MetallicTexture;

layout(set = 1, binding = 5) uniform sampler2D layer1AlbedoTexture;
layout(set = 1, binding = 6) uniform sampler2D layer1NormalTexture;
layout(set = 1, binding = 7) uniform sampler2D layer1RoughnessTexture;
layout(set = 1, binding = 8) uniform sampler2D layer1MetallicTexture;
