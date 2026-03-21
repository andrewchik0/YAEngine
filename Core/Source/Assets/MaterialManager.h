#pragma once

#include "AssetManagerBase.h"
#include "CubeMapManager.h"

#include "TextureManager.h"
#include "Render/VulkanMaterial.h"

namespace YAEngine
{
  struct RenderContext;
  class VulkanTexture;

  struct Material
  {
    glm::vec3 albedo;
    glm::vec3 emissivity;
    float roughness;
    float metallic;
    float specular;
    bool sg;
    bool hasAlpha;
    bool combinedTextures;

    TextureHandle baseColorTexture;
    TextureHandle metallicTexture;
    TextureHandle roughnessTexture;
    TextureHandle specularTexture;
    TextureHandle emissiveTexture;
    TextureHandle normalTexture;
    TextureHandle heightTexture;
    CubeMapHandle cubemap;
  private:

    VulkanMaterial m_VulkanMaterial;

    friend class MaterialManager;
    friend class ModelManager;
    friend class VulkanMaterial;
    friend class AssetManager;
    friend class Render;
  };

  using MaterialHandle = AssetHandle;

  class MaterialManager : public AssetManagerBase<Material>
  {
  public:

    void SetRenderContext(const RenderContext& ctx, const VulkanTexture& noneTexture)
    {
      m_Ctx = &ctx;
      m_NoneTexture = &noneTexture;
    }

    [[nodiscard]]
    MaterialHandle Create();
    void DestroyAll();
  private:
    const RenderContext* m_Ctx = nullptr;
    const VulkanTexture* m_NoneTexture = nullptr;
  };
}
