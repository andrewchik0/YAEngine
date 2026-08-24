layout(location = 0) in vec3 inPosition;

#ifdef ALPHA_TEST
layout(location = 1) in vec2 inTexCoord;
layout(location = 0) out vec2 outTexCoord;
#endif

#ifdef INSTANCED
  #ifdef ALPHA_TEST
layout(set = 2, binding = 0) readonly buffer Instances
  #else
layout(set = 1, binding = 0) readonly buffer Instances
  #endif
{
  mat4 data[];
} instances;
#endif

#ifdef INDIRECT
// One clip-space matrix per (instance, atlas tile): the tile's viewProj is folded
// into the world matrix on the CPU, so a vertex is one matrix load and one product.
// The tile blocks are laid out back to back and firstInstance selects the block, so
// a non-instanced caster is still just a batch of length one.
layout(set = 1, binding = 0) readonly buffer Models
{
  mat4 data[];
} models;
#else
layout(push_constant) uniform PushConstants
{
  // viewProj * world, folded together on the CPU. Pushing both separately needs
  // 132 bytes and the guaranteed push constant limit is 128.
  mat4 viewProjWorld;
  int offset;
} pc;
#endif

void main()
{
#ifdef INDIRECT
  // gl_InstanceIndex already carries firstInstance from the indirect command, so the
  // command's firstInstance is the batch base and no offset is added here.
  gl_Position = models.data[gl_InstanceIndex] * vec4(inPosition, 1.0);
#elif defined(INSTANCED)
  gl_Position = pc.viewProjWorld * (instances.data[gl_InstanceIndex + pc.offset] * vec4(inPosition, 1.0));
#else
  gl_Position = pc.viewProjWorld * vec4(inPosition, 1.0);
#endif

#ifdef ALPHA_TEST
  outTexCoord = inTexCoord;
#endif
}
