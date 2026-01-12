#include "VulkanMaterial.h"

#include "Application.h"
#include "Assets/MaterialManager.h"

namespace YAEngine
{
  uint32_t VulkanMaterial::s_MaxFramesInFlight;
  VkDevice VulkanMaterial::s_Device;
  VmaAllocator VulkanMaterial::s_Allocator;
  VulkanTexture VulkanMaterial::s_NoneTexture;
  VkDescriptorPool VulkanMaterial::s_Pool;

  void VulkanMaterial::Init()
  {
    m_DescriptorSets.resize(s_MaxFramesInFlight);
    m_UniformBuffers.resize(s_MaxFramesInFlight);

    VkDescriptorSetLayout layout = nullptr;
    for (size_t i = 0; i < s_MaxFramesInFlight; i++)
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
        m_DescriptorSets[i].Init(s_Device, s_Pool, desc);
        layout = m_DescriptorSets[i].GetLayout();
      }
      else
      {
        m_DescriptorSets[i].Init(s_Device, s_Pool, layout);
      }
      m_UniformBuffers[i].Create(s_Device, s_Allocator, sizeof(__PerMaterialData));
      m_DescriptorSets[i].WriteUniformBuffer(0, m_UniformBuffers[i].Get(), sizeof(__PerMaterialData));
      for (size_t j = 1; j < 8; j++)
        m_DescriptorSets[i].WriteCombinedImageSampler((uint32_t)j, s_NoneTexture.GetView(), s_NoneTexture.GetSampler());
    }
  }

  void VulkanMaterial::InitMaterials(VkDevice device, VkDescriptorPool pool, VmaAllocator allocator,
    uint32_t maxFramesInFlight)
  {
    s_Device = device;
    s_Allocator = allocator;
    s_Pool = pool;
    s_MaxFramesInFlight = maxFramesInFlight;

    uint8_t data[] = { 1, 1, 1, 1 };
    s_NoneTexture.Load(data, 1, 1, 4, VK_FORMAT_R8G8B8A8_SRGB);
  }

  void VulkanMaterial::Destroy()
  {
    for (auto buffer : m_UniformBuffers)
    {
      buffer.Destroy();
    }
    for (auto& descriptorSet : m_DescriptorSets)
    {
      descriptorSet.Destroy();
    }
    s_NoneTexture.Destroy();
  }

  void VulkanMaterial::Bind(Application* app, Material& material, uint32_t currentFrame)
  {
    int textureMask = 0;

    if (app->GetAssetManager().Textures().Has(material.baseColorTexture))
    {
      auto base = app->GetAssetManager().Textures().Get(material.baseColorTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(1, base.GetView(), base.GetSampler());
      textureMask |= (1 << 0);
    }
    if (app->GetAssetManager().Textures().Has(material.metallicTexture))
    {
      auto metallic = app->GetAssetManager().Textures().Get(material.metallicTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(2, metallic.GetView(), metallic.GetSampler());
      textureMask |= (1 << 1);
    }
    if (app->GetAssetManager().Textures().Has(material.roughnessTexture))
    {
      auto roughness = app->GetAssetManager().Textures().Get(material.roughnessTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(3, roughness.GetView(), roughness.GetSampler());
      textureMask |= (1 << 2);
    }
    if (app->GetAssetManager().Textures().Has(material.specularTexture))
    {
      auto specular = app->GetAssetManager().Textures().Get(material.specularTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(4, specular.GetView(), specular.GetSampler());
      textureMask |= (1 << 3);
    }
    if (app->GetAssetManager().Textures().Has(material.emissiveTexture))
    {
      auto emissive = app->GetAssetManager().Textures().Get(material.emissiveTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(5, emissive.GetView(), emissive.GetSampler());
      textureMask |= (1 << 4);
    }
    if (app->GetAssetManager().Textures().Has(material.normalTexture))
    {
      auto normal = app->GetAssetManager().Textures().Get(material.normalTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(6, normal.GetView(), normal.GetSampler());
      textureMask |= (1 << 5);
    }
    if (app->GetAssetManager().Textures().Has(material.heightTexture))
    {
      auto height = app->GetAssetManager().Textures().Get(material.heightTexture).m_VulkanTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(7, height.GetView(), height.GetSampler());
      textureMask |= (1 << 6);
    }
    if (app->GetAssetManager().CubeMaps().Has(material.cubemap))
    {
      auto cubemap = app->GetAssetManager().CubeMaps().Get(material.cubemap).m_CubeTexture;
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(8, cubemap.GetView(), cubemap.GetSampler());
      m_DescriptorSets[currentFrame].WriteCombinedImageSampler(9, VulkanCubicTexture::m_BRDFLut.GetView(), VulkanCubicTexture::m_BRDFLut.GetSampler());
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
