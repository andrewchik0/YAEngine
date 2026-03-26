#pragma once

#include "AssetManagerBase.h"
#include "IAssetManager.h"
#include "CubeMapManager.h"

#include "TextureManager.h"
#include "Render/VulkanMaterial.h"

namespace YAEngine
{
  struct RenderContext;
  class VulkanTexture;

  struct Material
  {
    glm::vec3 albedo{1.0f, 1.0f, 1.0f};
    glm::vec3 emissivity{0.0f, 0.0f, 0.0f};
    float roughness{0.5f};
    float metallic{0.0f};
    float specular{0.5f};
    bool sg{false};
    bool hasAlpha{false};
    bool combinedTextures{false};

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
  };

  class MaterialManager : public AssetManagerBase<Material, MaterialTag>, public IAssetManager
  {
  public:

    void SetRenderContext(const AssetManagerInitInfo& info) override
    {
      m_Ctx = info.ctx;
      m_NoneTexture = info.noneTexture;
    }

    [[nodiscard]]
    MaterialHandle Create();
    void Destroy(MaterialHandle handle);
    void DestroyAll() override;

    VulkanMaterial& GetVulkanMaterial(MaterialHandle handle)
    {
      return Get(handle).m_VulkanMaterial;
    }
  private:
    const RenderContext* m_Ctx = nullptr;
    const VulkanTexture* m_NoneTexture = nullptr;
  };
}
