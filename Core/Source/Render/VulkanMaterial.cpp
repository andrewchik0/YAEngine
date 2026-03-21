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
        m_DescriptorSets[i].Init(ctx.device, ctx.descriptorPool, desc);
        layout = m_DescriptorSets[i].GetLayout();
      }
      else
      {
        m_DescriptorSets[i].Init(ctx.device, ctx.descriptorPool, layout);
      }
      m_UniformBuffers[i].Create(ctx, sizeof(__PerMaterialData));
      m_DescriptorSets[i].WriteUniformBuffer(0, m_UniformBuffers[i].Get(), sizeof(__PerMaterialData));
      for (size_t j = 1; j < 8; j++)
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

    if (app->GetAssetManager().Textures().Has(material.baseColorTexture))
    {
      auto& base = app->GetAssetManager().Textures().Get(material.baseColorTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(1, base.GetView(), base.GetSampler());
      textureMask |= (1 << 0);
    }
    if (app->GetAssetManager().Textures().Has(material.metallicTexture))
    {
      auto& metallic = app->GetAssetManager().Textures().Get(material.metallicTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(2, metallic.GetView(), metallic.GetSampler());
      textureMask |= (1 << 1);
    }
    if (app->GetAssetManager().Textures().Has(material.roughnessTexture))
    {
      auto& roughness = app->GetAssetManager().Textures().Get(material.roughnessTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(3, roughness.GetView(), roughness.GetSampler());
      textureMask |= (1 << 2);
    }
    if (app->GetAssetManager().Textures().Has(material.specularTexture))
    {
      auto& specular = app->GetAssetManager().Textures().Get(material.specularTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(4, specular.GetView(), specular.GetSampler());
      textureMask |= (1 << 3);
    }
    if (app->GetAssetManager().Textures().Has(material.emissiveTexture))
    {
      auto& emissive = app->GetAssetManager().Textures().Get(material.emissiveTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(5, emissive.GetView(), emissive.GetSampler());
      textureMask |= (1 << 4);
    }
    if (app->GetAssetManager().Textures().Has(material.normalTexture))
    {
      auto& normal = app->GetAssetManager().Textures().Get(material.normalTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(6, normal.GetView(), normal.GetSampler());
      textureMask |= (1 << 5);
    }
    if (app->GetAssetManager().Textures().Has(material.heightTexture))
    {
      auto& height = app->GetAssetManager().Textures().Get(material.heightTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(7, height.GetView(), height.GetSampler());
      textureMask |= (1 << 6);
    }
    if (app->GetAssetManager().CubeMaps().Has(material.cubemap))
    {
      auto& cubemap = app->GetAssetManager().CubeMaps().Get(material.cubemap).m_CubeTexture;
      auto& brdfLut = app->GetRender().GetCubicResources().brdfLut;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(8, cubemap.GetPrefilterView(), cubemap.GetPrefilterSampler());
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(9, brdfLut.GetView(), brdfLut.GetSampler());
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(10, cubemap.GetIrradianceView(), cubemap.GetIrradianceSampler());
      textureMask |= (1 << 7);
    }
    textureMask |= (material.combinedTextures << 8);

    data.albedo = material.albedo;
    data.specular = material.specular;
    data.metallic = material.metallic;
    data.roughness = material.roughness;
    data.emissivity = material.emissivity;
    data.textureMask = textureMask;
    data.sg = material.sg;

    m_UniformBuffers[currentFrame].Update(data);
    m_DescriptorSets[currentFrame].WriteUniformBuffer(0, m_UniformBuffers[currentFrame].Get(), sizeof(__PerMaterialData));
  }
}
