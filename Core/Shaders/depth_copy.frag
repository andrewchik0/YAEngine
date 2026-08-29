layout(location = 0) in vec2 uv;

layout(set = 0, binding = 0) uniform sampler2D srcDepth;

// Point-resamples the scene depth into an output-resolution target. The sampler is
// nearest for depth, so an upscale duplicates texels instead of inventing surfaces
// between them.
void main()
{
  gl_FragDepth = texture(srcDepth, uv).r;
}
