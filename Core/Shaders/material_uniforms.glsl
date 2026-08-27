#ifndef MATERIAL_UNIFORMS_GLSL
#define MATERIAL_UNIFORMS_GLSL

#include "../Shared/MaterialUniforms.h"

layout(set = 1, binding = 0) uniform MaterialUniformsBlock { MaterialUniforms u_Material; };

// Split out of material.glsl so the alpha-test passes can reach the tiling factor without
// declaring the ten samplers they never touch.
//
// Every material texture fetch goes through this, the shadow and picking cutouts included:
// a cutout sampled at a different scale than the shaded surface punches its hole somewhere
// other than where the hole is visible.
vec2 materialUV(vec2 uv)
{
  return uv * u_Material.uvScale;
}

#endif
