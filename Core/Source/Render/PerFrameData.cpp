#include "PerFrameData.h"

namespace YAEngine
{
  void PerFrameData::Init(VkDevice device, VmaAllocator allocator, VkDescriptorPool pool, uint32_t maxFramesInFlight)
  {
    m_Device = device;
    m_DescriptorSets.resize(maxFramesInFlight);
    m_UniformBuffers.resize(maxFramesInFlight);

    VkDescriptorSetLayout layout = nullptr;
    for (size_t i = 0; i < maxFramesInFlight; i++)
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
        m_DescriptorSets[i].Init(device, pool, desc);
        layout = m_DescriptorSets[i].GetLayout();
      }
      else
      {
        m_DescriptorSets[i].Init(device, pool, layout);
      }
      m_UniformBuffers[i].Create(m_Device, allocator, sizeof(__PerFrameUBO));
      m_DescriptorSets[i].WriteUniformBuffer(0, m_UniformBuffers[i].Get(), sizeof(__PerFrameUBO));
    }
  }

  void PerFrameData::Destroy()
  {
    for (auto& set : m_DescriptorSets)
    {
      set.Destroy();
    }
    for (auto& ubo : m_UniformBuffers)
    {
      ubo.Destroy();
    }
  }

  void PerFrameData::SetUp(uint32_t frameIndex)
  {
    m_UniformBuffers[frameIndex].Update(ubo);
  }

}
