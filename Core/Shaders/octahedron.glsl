// Octahedron normal encoding/decoding (Cigolle et al. 2014)
// Maps unit normals to [-1,1]^2 via L1 sphere projection

vec2 octEncode(vec3 n)
{
  n /= abs(n.x) + abs(n.y) + abs(n.z);
  if (n.z < 0.0)
    n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
  return n.xy;
}

vec3 octDecode(vec2 e)
{
  vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
  if (n.z < 0.0)
    n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
  return normalize(n);
}
