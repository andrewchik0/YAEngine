#include "PerFrameData.h"

#include "RenderContext.h"

namespace YAEngine
{
  void PerFrameData::Init(const RenderContext& ctx)
  {
    m_DescriptorSets.resize(ctx.maxFramesInFlight);
    m_UniformBuffers.resize(ctx.maxFramesInFlight);

    VkDescriptorSetLayout layout = nullptr;
    for (size_t i = 0; i < ctx.maxFramesInFlight; i++)
    {
      SetDescription desc = {
        .set = 0,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL }
          }
        }
      };
      if (i == 0)
      {
        m_DescriptorSets[i].Init(ctx.device, ctx.descriptorPool, desc);
        layout = m_DescriptorSets[i].GetLayout();
      }
      else
      {
        m_DescriptorSets[i].Init(ctx.device, ctx.descriptorPool, layout);
      }
      m_UniformBuffers[i].Create(ctx, sizeof(__PerFrameUBO));
      m_DescriptorSets[i].WriteUniformBuffer(0, m_UniformBuffers[i].Get(), sizeof(__PerFrameUBO));
    }
  }

  void PerFrameData::Destroy(const RenderContext& ctx)
  {
    for (auto& set : m_DescriptorSets)
    {
      set.Destroy();
    }
    for (auto& ubo : m_UniformBuffers)
    {
      ubo.Destroy(ctx);
    }
  }

  void PerFrameData::SetUp(uint32_t frameIndex)
  {
    m_UniformBuffers[frameIndex].Update(ubo);
  }
}
