#pragma once

#include "Pch.h"

#include "MaterialManager.h"
#include "ModelDescription.h"

namespace YAEngine
{
  struct ModelTemplateNode
  {
    std::string name;
    glm::vec3 position { 0 };
    glm::quat rotation { 1, 0, 0, 0 };
    glm::vec3 scale { 1 };

    std::optional<uint32_t> meshIndex;
    std::optional<uint32_t> materialIndex;

    uint32_t parent = UINT32_MAX;

    // Sibling-index path from the model root ("0/3/1"). Empty on the root itself.
    std::string indexPath;
    // Name path from the model root ("Body/Wheel_FL"). Repeated sibling names get a
    // "#N" suffix and unnamed nodes an "@N" segment, so every path stays unique.
    std::string namePath;
  };

  // Serializable subset of Material as it came out of the import. Two jobs: it is the
  // baseline a slot patch is diffed against, and it is what a revert restores.
  struct MaterialSnapshot
  {
    // Material::generation at capture time. Every editor edit calls MarkChanged(), so an
    // untouched slot is detected by this alone - no float comparison involved.
    uint32_t generation = 0;

    std::string name;
    glm::vec3 albedo { 1.0f };
    glm::vec3 emissivity { 0.0f };
    float roughness = 0.5f;
    float metallic = 0.0f;
    float roughnessFactor = 1.0f;
    float metallicFactor = 1.0f;
    float specular = 0.5f;
    bool sg = false;
    bool hasAlpha = false;
    bool alphaTest = false;
    bool combinedTextures = false;
    bool doubleSided = false;
    bool transparent = false;
    float opacity = 1.0f;
    ShadingModel shadingModel = ShadingModel::Lit;

    TextureHandle baseColorTexture;
    TextureHandle metallicTexture;
    TextureHandle roughnessTexture;
    TextureHandle specularTexture;
    TextureHandle emissiveTexture;
    TextureHandle normalTexture;
    TextureHandle heightTexture;
    // Material::cubemap is deliberately absent: Render assigns the scene skybox into it
    // every frame, so it is renderer state, not something the author owns.
  };

  MaterialSnapshot SnapshotMaterial(const Material& material);
  void RestoreMaterial(Material& material, const MaterialSnapshot& snapshot);

  // Slim snapshot of a model right after import: the node tree without vertex data plus
  // the material descriptions. Diffing against it is what makes authored overrides sparse.
  struct ModelTemplate
  {
    static constexpr uint32_t INVALID_NODE = UINT32_MAX;

    std::vector<ModelTemplateNode> nodes;
    std::vector<MaterialDescription> materials;
    std::string sourcePath;

    // Structural hash of the import. Stored in the scene so a re-exported model can be
    // detected on load. File size and mtime are not usable - they differ per machine.
    uint64_t fingerprint = 0;

    uint32_t FindByIndexPath(const std::string& path) const;
    // INVALID_NODE when the path is missing or matches more than one node
    uint32_t FindByNamePath(const std::string& path) const;
  };

  ModelTemplate BuildModelTemplate(const ModelDescription& desc);
}
