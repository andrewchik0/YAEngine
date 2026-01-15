#include "SkyBox.h"

#include "Application.h"
#include "Application.h"
#include "Application.h"
#include "Application.h"
#include "Application.h"
#include "VulkanCubicTexture.h"

namespace YAEngine
{
  void SkyBox::Init(VkDevice device, VkDescriptorPool descriptorPool, VkRenderPass renderPass, uint32_t maxFramesInFlight)
  {
    m_Device = device;
    m_DescriptorPool = descriptorPool;

    m_DescriptorSets.resize(maxFramesInFlight);

    VkDescriptorSetLayout layout = nullptr;
    for (size_t i = 0; i < maxFramesInFlight; i++)
    {
      SetDescription desc = {
        .set = 0,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
          }
        }
      };
      if (i == 0)
      {
        m_DescriptorSets[i].Init(m_Device, m_DescriptorPool, desc);
        layout = m_DescriptorSets[i].GetLayout();
      }
      else
      {
        m_DescriptorSets[i].Init(m_Device, m_DescriptorPool, layout);
      }
    }

    PipelineCreateInfo info;
    info.fragmentShaderFile = "sky.frag";
    info.vertexShaderFile = "sky.vert";
    info.vertexInputFormat = "f3";
    info.secondaryAttachment = true;
    info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    info.depthWrite = false;
    info.pushConstantSize = sizeof(glm::mat4) * 2;
    info.doubleSided = true;
    info.sets = {
      m_DescriptorSets[0].GetLayout()
    };

    m_Pipeline.Init(m_Device, renderPass, info);
  }

  void SkyBox::Destroy()
  {
    m_Pipeline.Destroy();
    for (auto& set : m_DescriptorSets)
      set.Destroy();
  }

  void SkyBox::Draw(uint32_t currentFrame, VulkanCubicTexture* cube, VkCommandBuffer commandBuffer, glm::mat4& camDir, glm::mat4& proj)
  {
    m_Pipeline.Bind(commandBuffer);
    std::array<glm::mat4, 2> matrices;
    matrices[0] = camDir;
    matrices[1] = proj;
    m_Pipeline.PushConstants(commandBuffer, matrices.data());

    m_Pipeline.BindDescriptorSets(commandBuffer, { m_DescriptorSets[currentFrame].Get() }, 0);
    m_DescriptorSets[currentFrame].WriteCombinedImageSampler(0, cube->GetView(), cube->GetSampler());

    VulkanCubicTexture::DrawCube(commandBuffer);
  }

}
