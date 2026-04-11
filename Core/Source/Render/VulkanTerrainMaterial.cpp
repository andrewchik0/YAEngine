#include "VulkanTerrainMaterial.h"

#include <entt/entt.hpp>
#include "RenderContext.h"
#include "Assets/TextureManager.h"
#include "Assets/MaterialManager.h"
#include "Scene/Components.h"

namespace YAEngine
{
  void VulkanTerrainMaterial::Init(const RenderContext& ctx, const VulkanTexture& noneTexture)
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
            // Layer 0: albedo, normal, roughness, metallic
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            // Layer 1: albedo, normal, roughness, metallic
            { 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
            { 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT },
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
      m_UniformBuffers[i].Create(ctx, sizeof(TerrainMaterialUniforms));

      auto writer = m_DescriptorSets[i].Writer();
      writer.WriteUniformBuffer(0, m_UniformBuffers[i].Get(), sizeof(TerrainMaterialUniforms));
      for (uint32_t j = 1; j <= 8; j++)
        writer.WriteCombinedImageSampler(j, noneTexture.GetView(), noneTexture.GetSampler());
      writer.Flush();
    }
  }

  void VulkanTerrainMaterial::Destroy(const RenderContext& ctx)
  {
    for (auto& buffer : m_UniformBuffers)
      buffer.Destroy(ctx);
    for (auto& descriptorSet : m_DescriptorSets)
      descriptorSet.Destroy();
  }

  void VulkanTerrainMaterial::Bind(TextureManager& textures, Material& layer0,
                                    const TerrainMaterialComponent& layer1,
                                    uint32_t currentFrame, const VulkanTexture& noneTexture)
  {
    if (m_BoundGeneration == layer0.generation && m_LastFrame == currentFrame)
      return;

    int textureMask = 0;
    auto writer = m_DescriptorSets[currentFrame].Writer();

    // Layer 0 textures (from MaterialComponent)
    if (textures.Has(layer0.baseColorTexture))
    {
      auto& tex = textures.GetVulkanTexture(layer0.baseColorTexture);
      writer.WriteCombinedImageSampler(1, tex.GetView(), tex.GetSampler());
      textureMask |= (1 << 0);
    }
    if (textures.Has(layer0.normalTexture))
    {
      auto& tex = textures.GetVulkanTexture(layer0.normalTexture);
      writer.WriteCombinedImageSampler(2, tex.GetView(), tex.GetSampler());
      textureMask |= (1 << 1);
    }
    if (textures.Has(layer0.roughnessTexture))
    {
      auto& tex = textures.GetVulkanTexture(layer0.roughnessTexture);
      writer.WriteCombinedImageSampler(3, tex.GetView(), tex.GetSampler());
      textureMask |= (1 << 2);
    }
    if (textures.Has(layer0.metallicTexture))
    {
      auto& tex = textures.GetVulkanTexture(layer0.metallicTexture);
      writer.WriteCombinedImageSampler(4, tex.GetView(), tex.GetSampler());
      textureMask |= (1 << 3);
    }

    // Layer 1 textures (from TerrainMaterialComponent)
    if (textures.Has(layer1.layer1Albedo))
    {
      auto& tex = textures.GetVulkanTexture(layer1.layer1Albedo);
      writer.WriteCombinedImageSampler(5, tex.GetView(), tex.GetSampler());
      textureMask |= (1 << 4);
    }
    if (textures.Has(layer1.layer1Normal))
    {
      auto& tex = textures.GetVulkanTexture(layer1.layer1Normal);
      writer.WriteCombinedImageSampler(6, tex.GetView(), tex.GetSampler());
      textureMask |= (1 << 5);
    }
    if (textures.Has(layer1.layer1Roughness))
    {
      auto& tex = textures.GetVulkanTexture(layer1.layer1Roughness);
      writer.WriteCombinedImageSampler(7, tex.GetView(), tex.GetSampler());
      textureMask |= (1 << 6);
    }
    if (textures.Has(layer1.layer1Metallic))
    {
      auto& tex = textures.GetVulkanTexture(layer1.layer1Metallic);
      writer.WriteCombinedImageSampler(8, tex.GetView(), tex.GetSampler());
      textureMask |= (1 << 7);
    }

    uniforms.layer0Albedo = layer0.albedo;
    uniforms.layer0Roughness = layer0.roughness;
    uniforms.layer0Metallic = layer0.metallic;
    uniforms.layer1Albedo = glm::vec3(1.0f);
    uniforms.layer1Roughness = 1.0f;
    uniforms.layer1Metallic = 0.0f;
    uniforms.slopeStart = layer1.slopeStart;
    uniforms.slopeEnd = layer1.slopeEnd;
    uniforms.textureMask = textureMask;
    uniforms.layer0UvScale = 1.0f;
    uniforms.layer1UvScale = layer1.layer1UvScale;

    m_UniformBuffers[currentFrame].Update(uniforms);
    writer.WriteUniformBuffer(0, m_UniformBuffers[currentFrame].Get(), sizeof(TerrainMaterialUniforms));
    writer.Flush();

    m_BoundGeneration = layer0.generation;
    m_LastFrame = currentFrame;
  }
}
