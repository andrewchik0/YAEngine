#include "VulkanMaterial.h"

#include "RenderContext.h"
#include "VulkanCubicTexture.h"
#include "Assets/TextureManager.h"
#include "Assets/CubeMapManager.h"
#include "Assets/MaterialManager.h"

namespace YAEngine
{
  void VulkanMaterial::Init(const RenderContext& ctx, const VulkanTexture& noneTexture)
  {
    m_DescriptorSets.resize(ctx.maxFramesInFlight);
    m_UniformBuffers.resize(ctx.maxFramesInFlight);
    m_BoundGenerations.resize(ctx.maxFramesInFlight, UINT32_MAX);

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

      auto writer = m_DescriptorSets[i].Writer();
      writer.WriteUniformBuffer(0, m_UniformBuffers[i].Get(), sizeof(MaterialUniforms));
      for (uint32_t j = 1; j < 11; j++)
        writer.WriteCombinedImageSampler(j, noneTexture.GetView(), noneTexture.GetSampler());
      writer.Flush();
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

  void VulkanMaterial::Bind(TextureManager& textures, CubeMapManager& cubeMaps, CubicTextureResources& cubicResources,
                            Material& material, uint32_t currentFrame, const VulkanTexture& noneTexture)
  {
    if (m_BoundGenerations[currentFrame] == material.generation)
      return;

    int textureMask = 0;

    auto writer = m_DescriptorSets[currentFrame].Writer();

    if (textures.Has(material.baseColorTexture))
    {
      auto& base = textures.GetVulkanTexture(material.baseColorTexture);
      writer.WriteCombinedImageSampler(1, base.GetView(), base.GetSampler());
      textureMask |= (1 << 0);
    }
    if (textures.Has(material.metallicTexture))
    {
      auto& metallic = textures.GetVulkanTexture(material.metallicTexture);
      writer.WriteCombinedImageSampler(2, metallic.GetView(), metallic.GetSampler());
      textureMask |= (1 << 1);
    }
    if (textures.Has(material.roughnessTexture))
    {
      auto& roughness = textures.GetVulkanTexture(material.roughnessTexture);
      writer.WriteCombinedImageSampler(3, roughness.GetView(), roughness.GetSampler());
      textureMask |= (1 << 2);
    }
    if (textures.Has(material.specularTexture))
    {
      auto& specular = textures.GetVulkanTexture(material.specularTexture);
      writer.WriteCombinedImageSampler(4, specular.GetView(), specular.GetSampler());
      textureMask |= (1 << 3);
    }
    if (textures.Has(material.emissiveTexture))
    {
      auto& emissive = textures.GetVulkanTexture(material.emissiveTexture);
      writer.WriteCombinedImageSampler(5, emissive.GetView(), emissive.GetSampler());
      textureMask |= (1 << 4);
    }
    if (textures.Has(material.normalTexture))
    {
      auto& normal = textures.GetVulkanTexture(material.normalTexture);
      writer.WriteCombinedImageSampler(6, normal.GetView(), normal.GetSampler());
      textureMask |= (1 << 5);
    }
    if (textures.Has(material.heightTexture))
    {
      auto& height = textures.GetVulkanTexture(material.heightTexture);
      writer.WriteCombinedImageSampler(7, height.GetView(), height.GetSampler());
      textureMask |= (1 << 6);
    }
    if (cubeMaps.Has(material.cubemap))
    {
      auto& cubemap = cubeMaps.GetVulkanCubicTexture(material.cubemap);
      auto& brdfLut = cubicResources.brdfLut;
      writer.WriteCombinedImageSampler(8, cubemap.GetPrefilterView(), cubemap.GetPrefilterSampler());
      writer.WriteCombinedImageSampler(9, brdfLut.GetView(), brdfLut.GetSampler());
      writer.WriteCombinedImageSampler(10, cubemap.GetIrradianceView(), cubemap.GetIrradianceSampler());
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
    writer.WriteUniformBuffer(0, m_UniformBuffers[currentFrame].Get(), sizeof(MaterialUniforms));
    writer.Flush();

    m_BoundGenerations[currentFrame] = material.generation;
  }
}
