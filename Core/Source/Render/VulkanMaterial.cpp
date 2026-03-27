#include "VulkanMaterial.h"

#include "Application.h"
#include "RenderContext.h"
#include "Assets/MaterialManager.h"

namespace YAEngine
{
  void VulkanMaterial::Init(const RenderContext& ctx, const VulkanTexture& noneTexture)
  {
    m_DescriptorSets.resize(ctx.maxFramesInFlight);
    m_UniformBuffers.resize(ctx.maxFramesInFlight);

    VkDescriptorSetLayout layout = nullptr;
    for (size_t i = 0; i < ctx.maxFramesInFlight; i++)
    {
      SetDescription desc = {
        .set = 1,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
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
      m_UniformBuffers[i].Create(ctx, sizeof(MaterialUniforms));
      m_DescriptorSets[i].WriteUniformBuffer(0, m_UniformBuffers[i].Get(), sizeof(MaterialUniforms));
      for (size_t j = 1; j < 11; j++)
        m_DescriptorSets[i].WriteCombinedImageSampler((uint32_t)j, noneTexture.GetView(), noneTexture.GetSampler());
    }
  }

  void VulkanMaterial::Destroy(const RenderContext& ctx)
  {
    for (auto& buffer : m_UniformBuffers)
    {
      buffer.Destroy(ctx);
    }
    for (auto& descriptorSet : m_DescriptorSets)
    {
      descriptorSet.Destroy();
    }
  }

  void VulkanMaterial::Bind(Application* app, Material& material, uint32_t currentFrame, const VulkanTexture& noneTexture)
  {
    int textureMask = 0;
    auto& textures = app->GetAssetManager().Textures();
    auto& cubeMaps = app->GetAssetManager().CubeMaps();

    if (textures.Has(material.baseColorTexture))
    {
      auto& base = textures.GetVulkanTexture(material.baseColorTexture);
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(1, base.GetView(), base.GetSampler());
      textureMask |= (1 << 0);
    }
    if (textures.Has(material.metallicTexture))
    {
      auto& metallic = textures.GetVulkanTexture(material.metallicTexture);
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(2, metallic.GetView(), metallic.GetSampler());
      textureMask |= (1 << 1);
    }
    if (textures.Has(material.roughnessTexture))
    {
      auto& roughness = textures.GetVulkanTexture(material.roughnessTexture);
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(3, roughness.GetView(), roughness.GetSampler());
      textureMask |= (1 << 2);
    }
    if (textures.Has(material.specularTexture))
    {
      auto& specular = textures.GetVulkanTexture(material.specularTexture);
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(4, specular.GetView(), specular.GetSampler());
      textureMask |= (1 << 3);
    }
    if (textures.Has(material.emissiveTexture))
    {
      auto& emissive = textures.GetVulkanTexture(material.emissiveTexture);
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(5, emissive.GetView(), emissive.GetSampler());
      textureMask |= (1 << 4);
    }
    if (textures.Has(material.normalTexture))
    {
      auto& normal = textures.GetVulkanTexture(material.normalTexture);
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(6, normal.GetView(), normal.GetSampler());
      textureMask |= (1 << 5);
    }
    if (textures.Has(material.heightTexture))
    {
      auto& height = textures.GetVulkanTexture(material.heightTexture);
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(7, height.GetView(), height.GetSampler());
      textureMask |= (1 << 6);
    }
    if (cubeMaps.Has(material.cubemap))
    {
      auto& cubemap = cubeMaps.GetVulkanCubicTexture(material.cubemap);
      auto& brdfLut = app->GetRender().GetCubicResources().brdfLut;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(8, cubemap.GetPrefilterView(), cubemap.GetPrefilterSampler());
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(9, brdfLut.GetView(), brdfLut.GetSampler());
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(10, cubemap.GetIrradianceView(), cubemap.GetIrradianceSampler());
      textureMask |= (1 << 7);
    }
    textureMask |= (material.combinedTextures << 8);

    uniforms.albedo = material.albedo;
    uniforms.specular = material.specular;
    uniforms.metallic = material.metallic;
    uniforms.roughness = material.roughness;
    uniforms.emissivity = material.emissivity;
    uniforms.textureMask = textureMask;
    uniforms.sg = material.sg;

    m_UniformBuffers[currentFrame].Update(uniforms);
    m_DescriptorSets[currentFrame].WriteUniformBuffer(0, m_UniformBuffers[currentFrame].Get(), sizeof(MaterialUniforms));
  }
}
