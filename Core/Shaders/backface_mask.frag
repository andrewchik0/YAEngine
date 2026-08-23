// Node classification for the irradiance volume bake. Depth testing leaves only the
// nearest surface in every direction, and its winding answers the one question the
// bake asks: is the node sitting behind that surface instead of in front of it?
// Sky and empty space write nothing and keep the cleared zero.
layout(location = 0) out float outBackface;

void main()
{
  outBackface = gl_FrontFacing ? 0.0 : 1.0;
}
