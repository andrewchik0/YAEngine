#ifdef __cplusplus
#pragma once
#define vec4 glm::vec4
namespace YAEngine {
#endif

#define SSAO_KERNEL_SIZE 16

struct SSAOKernel
{
  vec4 samples[SSAO_KERNEL_SIZE];
};

#ifdef __cplusplus
} // namespace YAEngine
#undef vec4
#endif
