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

  enum class ShadingModel : uint8_t
  {
    Lit,
    Unlit
  };

  struct Material
  {
    std::string name;

    glm::vec3 albedo{1.0f, 1.0f, 1.0f};
    glm::vec3 emissivity{0.0f, 0.0f, 0.0f};
    // KHR_materials_emissive_strength. Kept apart from emissivity so the colour stays a
    // colour in the editor and the glTF pair survives a round trip; the two are folded
    // together on the way into the material UBO.
    float emissiveIntensity{1.0f};
    float roughness{0.5f};
    float metallic{0.0f};
    float roughnessFactor{1.0f};
    float metallicFactor{1.0f};
    float specular{0.5f};
    bool sg{false};
    bool hasAlpha{false};
    bool alphaTest{false};
    bool combinedTextures{false};
    // Opt-in: without it an imported emissive map only tints the surface, exactly as before
    bool emissive{false};
    bool doubleSided{false};
    bool transparent{false};
    float opacity{1.0f};
    ShadingModel shadingModel{ShadingModel::Lit};

    // Tiling factor folded into the mesh UVs before every material texture fetch, the
    // alpha-test cutouts of the shadow and picking passes included.
    glm::vec2 uvScale{1.0f, 1.0f};

    TextureHandle baseColorTexture;
    TextureHandle metallicTexture;
    TextureHandle roughnessTexture;
    TextureHandle specularTexture;
    TextureHandle emissiveTexture;
    TextureHandle normalTexture;
    TextureHandle heightTexture;
    CubeMapHandle cubemap;

    uint32_t generation = 0;
    void MarkChanged() { ++generation; }

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
    [[nodiscard]]
    MaterialHandle Duplicate(MaterialHandle source);
    void Destroy(MaterialHandle handle);
    void DestroyAll() override;

    VulkanMaterial& GetVulkanMaterial(MaterialHandle handle)
    {
      return Get(handle).m_VulkanMaterial;
    }
  private:
    const RenderContext* m_Ctx = nullptr;
    const VulkanTexture* m_NoneTexture = nullptr;
    uint32_t m_NextId = 0;
  };
}
