#pragma once

#include "AssetManagerBase.h"
#include "IAssetManager.h"
#include "CubeMapManager.h"

#include "TextureManager.h"
#include "TransmissionMode.h"
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
    float fresnelOpacity{0.0f};
    // Path tracing only, and only while transparent is set - see IsPathTraceTransmissive. The
    // surface is then a perfectly smooth dielectric, and albedo, metallic and roughness are
    // raster tuning the path tracer ignores.
    TransmissionMode transmissionMode{TransmissionMode::None};
    float ior{1.5f};
    // Solid and ThinWalled: what is left of white light after transmittanceDistance world units
    // inside. Sheet: the tint of one crossing, the distance unused.
    glm::vec3 transmittanceColor{1.0f, 1.0f, 1.0f};
    float transmittanceDistance{1.0f};
    // Where Solid media overlap - a liquid modelled into its glass wall - the higher priority
    // owns the overlap.
    int32_t mediumPriority{0};
    // A smooth dielectric layer (F0 0.04) over the surface, in raster and the path tracer alike;
    // weight 0 is no coat. Scalars only: the coat takes no texture map and keeps the interpolated
    // vertex normal, while the surface under it keeps all of its maps.
    float clearCoat{0.0f};
    float clearCoatRoughness{0.0f};
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

  // `transparent` stays the single routing flag: a transparent material without a transmission
  // mode is raster-only, exactly as before transmission existed.
  inline bool IsPathTraceTransmissive(const Material& material)
  {
    return material.transparent && material.transmissionMode != TransmissionMode::None;
  }

  // Loaded values into the range the inspector offers and the path tracer can use.
  inline void ClampTransmission(Material& material)
  {
    material.ior = std::clamp(material.ior, MIN_TRANSMISSION_IOR, MAX_TRANSMISSION_IOR);
    material.transmittanceColor = glm::clamp(material.transmittanceColor, glm::vec3(0.0f), glm::vec3(1.0f));
    material.transmittanceDistance = std::max(material.transmittanceDistance, MIN_TRANSMITTANCE_DISTANCE);
    material.mediumPriority = std::clamp(material.mediumPriority, 0, MAX_MEDIUM_PRIORITY);
  }

  inline void ClampClearCoat(Material& material)
  {
    material.clearCoat = std::clamp(material.clearCoat, 0.0f, 1.0f);
    material.clearCoatRoughness = std::clamp(material.clearCoatRoughness, 0.0f, 1.0f);
  }

  // Whether the raster shading of the material has a clear coat at all: Unlit and transparent
  // surfaces ignore it, so the path tracer is handed no coat for them either.
  inline bool IsClearCoatShaded(const Material& material)
  {
    return !material.transparent && material.shadingModel == ShadingModel::Lit;
  }

  // The coat weight every shader is handed: zero where the coat is not shaded, and otherwise snapped
  // to the grid GBuffer2 stores it on (CLEAR_COAT_WEIGHT_STEPS), so a G-buffer texel and a traced hit
  // of the same material read the same weight.
  inline float GetShadedClearCoat(const Material& material)
  {
    if (!IsClearCoatShaded(material))
      return 0.0f;

    const float steps = float(CLEAR_COAT_WEIGHT_STEPS);
    return std::round(std::clamp(material.clearCoat, 0.0f, 1.0f) * steps) / steps;
  }

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
