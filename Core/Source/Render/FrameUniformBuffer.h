#pragma once

#include "VulkanDescriptorSet.h"
#include "VulkanUniformBuffer.h"
#include "FrameUniforms.h"

namespace YAEngine
{
  struct RenderContext;

  // The GLSL side of this struct comes from the same header, but only C++ sees a
  // compiler that can check the offsets. Both vec3 members sit on a 16 byte
  // boundary purely because the scalar runs before them happen to be the right
  // length, so appending one more scalar in front of either silently desyncs the
  // block. These turn that into a build error.
  static_assert(sizeof(FrameUniforms) == 548, "FrameUniforms no longer matches its std140 layout");
  static_assert(offsetof(FrameUniforms, cameraPosition) % 16 == 0, "vec3 cameraPosition must be 16 byte aligned");
  static_assert(offsetof(FrameUniforms, cameraDirection) % 16 == 0, "vec3 cameraDirection must be 16 byte aligned");
  static_assert(offsetof(FrameUniforms, invView) % 16 == 0, "mat4 invView must be 16 byte aligned");
  static_assert(offsetof(FrameUniforms, fogColor) % 16 == 0, "vec3 fogColor must be 16 byte aligned");

  class FrameUniformBuffer
  {
  public:

    FrameUniforms uniforms {};

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    void SetUp(uint32_t frameIndex);

    VkDescriptorSetLayout GetLayout()
    {
      return m_DescriptorSets[0].GetLayout();
    }

    VkDescriptorSet GetDescriptorSet(uint32_t frameIndex)
    {
      return m_DescriptorSets[frameIndex].Get();
    }

  private:

    std::vector<VulkanDescriptorSet> m_DescriptorSets;
    std::vector<VulkanUniformBuffer> m_UniformBuffers;
  };
}
