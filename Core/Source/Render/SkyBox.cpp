#include "SkyBox.h"

#include "RenderContext.h"
#include "VulkanCubicTexture.h"

namespace YAEngine
{
  void SkyBox::Init(const RenderContext& ctx, VkRenderPass renderPass)
  {
    m_DescriptorSets.resize(ctx.maxFramesInFlight);

    VkDescriptorSetLayout layout = nullptr;
    for (size_t i = 0; i < ctx.maxFramesInFlight; i++)
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
        m_DescriptorSets[i].Init(ctx, desc);
        layout = m_DescriptorSets[i].GetLayout();
      }
      else
      {
        m_DescriptorSets[i].Init(ctx, layout);
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

    m_Pipeline.Init(ctx.device, renderPass, info, ctx.pipelineCache);
  }

  void SkyBox::Destroy()
  {
    m_Pipeline.Destroy();
    for (auto& set : m_DescriptorSets)
      set.Destroy();
  }

  void SkyBox::Draw(uint32_t currentFrame, VulkanCubicTexture* cube, VkCommandBuffer commandBuffer, glm::mat4& camDir, glm::mat4& proj, const CubicTextureResources& res)
  {
    m_Pipeline.Bind(commandBuffer);
    std::array<glm::mat4, 2> matrices;
    matrices[0] = camDir;
    matrices[1] = proj;
    m_Pipeline.PushConstants(commandBuffer, matrices.data());

    m_Pipeline.BindDescriptorSets(commandBuffer, { m_DescriptorSets[currentFrame].Get() }, 0);
    m_DescriptorSets[currentFrame].WriteCombinedImageSampler(0, cube->GetView(), cube->GetSampler());

    VulkanCubicTexture::DrawCube(commandBuffer, res);
  }
}
