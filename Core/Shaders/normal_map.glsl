// Tangent space normal decode, shared by the mesh and terrain G-buffer paths.
//
// BC5 stores only X and Y, so Z has to be rebuilt from them. Which textures those
// are is a per-texture property, not a per-shader one, so the caller passes the
// matching bit of its own textureMask - see IsTwoChannelNormal() on the C++ side.
vec3 decodeNormalMap(vec3 sampled, float twoChannel)
{
  vec3 n = sampled * 2.0 - 1.0;
  n.z = mix(n.z, sqrt(max(1.0 - dot(n.xy, n.xy), 0.0)), twoChannel);
  return n;
}
