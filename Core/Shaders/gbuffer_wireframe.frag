layout(location = 0) out vec4 outGBuffer0;
layout(location = 1) out vec4 outGBuffer1;
layout(location = 2) out vec2 outVelocity;

#include "octahedron.glsl"

void main() {
  // Solid white wireframe color, metallic 0
  outGBuffer0 = vec4(1.0, 1.0, 1.0, 0.0);

  // Neutral up normal, roughness 1, shadingModel 0
  vec2 octNorm = octEncode(vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
  outGBuffer1 = vec4(octNorm, 1.0, 0.0);

  outVelocity = vec2(0.0);
}
