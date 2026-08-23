#include "ModelTemplate.h"

namespace YAEngine
{
  static constexpr uint64_t FNV_OFFSET = 14695981039346656037ull;
  static constexpr uint64_t FNV_PRIME = 1099511628211ull;

  static uint64_t HashBytes(uint64_t hash, const void* data, size_t size)
  {
    auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++)
    {
      hash ^= uint64_t(bytes[i]);
      hash *= FNV_PRIME;
    }
    return hash;
  }

  static uint64_t HashString(uint64_t hash, const std::string& value)
  {
    return HashBytes(hash, value.data(), value.size());
  }

  static uint64_t HashUint(uint64_t hash, uint64_t value)
  {
    return HashBytes(hash, &value, sizeof(value));
  }

  // Unique segment among siblings: unnamed nodes get a positional segment, repeated
  // names an occurrence suffix
  static std::string MakeNameSegment(const std::string& name, size_t siblingIndex,
    std::unordered_map<std::string, uint32_t>& used)
  {
    std::string segment = name.empty() ? "@" + std::to_string(siblingIndex) : name;

    uint32_t occurrence = used[segment]++;
    if (occurrence > 0)
      segment += "#" + std::to_string(occurrence);

    return segment;
  }

  // Pre-order walk. ModelBuilder assigns node indices in the same order, so the two
  // stay aligned without an explicit mapping.
  static void FlattenNode(const NodeDescription& node, uint32_t parent,
    const std::string& indexPath, const std::string& namePath, ModelTemplate& out)
  {
    uint32_t index = uint32_t(out.nodes.size());

    ModelTemplateNode entry;
    entry.name = node.name;
    entry.position = node.position;
    entry.rotation = node.rotation;
    entry.scale = node.scale;
    entry.meshIndex = node.meshIndex;
    entry.materialIndex = node.materialIndex;
    entry.parent = parent;
    entry.indexPath = indexPath;
    entry.namePath = namePath;
    out.nodes.push_back(std::move(entry));

    std::unordered_map<std::string, uint32_t> usedNames;

    for (size_t i = 0; i < node.children.size(); i++)
    {
      auto& child = node.children[i];

      std::string childIndexPath = indexPath.empty()
        ? std::to_string(i)
        : indexPath + "/" + std::to_string(i);

      std::string segment = MakeNameSegment(child.name, i, usedNames);
      std::string childNamePath = namePath.empty() ? segment : namePath + "/" + segment;

      FlattenNode(child, index, childIndexPath, childNamePath, out);
    }
  }

  static uint64_t ComputeFingerprint(const ModelTemplate& tmpl)
  {
    uint64_t hash = FNV_OFFSET;
    hash = HashUint(hash, tmpl.nodes.size());
    hash = HashUint(hash, tmpl.materials.size());

    for (auto& node : tmpl.nodes)
    {
      hash = HashString(hash, node.name);
      hash = HashUint(hash, node.meshIndex.value_or(UINT32_MAX));
      hash = HashUint(hash, node.materialIndex.value_or(UINT32_MAX));
    }

    for (auto& material : tmpl.materials)
      hash = HashString(hash, material.name);

    return hash;
  }

  MaterialSnapshot SnapshotMaterial(const Material& material)
  {
    return MaterialSnapshot {
      .generation = material.generation,
      .name = material.name,
      .albedo = material.albedo,
      .emissivity = material.emissivity,
      .roughness = material.roughness,
      .metallic = material.metallic,
      .roughnessFactor = material.roughnessFactor,
      .metallicFactor = material.metallicFactor,
      .specular = material.specular,
      .sg = material.sg,
      .hasAlpha = material.hasAlpha,
      .alphaTest = material.alphaTest,
      .combinedTextures = material.combinedTextures,
      .doubleSided = material.doubleSided,
      .transparent = material.transparent,
      .opacity = material.opacity,
      .shadingModel = material.shadingModel,
      .baseColorTexture = material.baseColorTexture,
      .metallicTexture = material.metallicTexture,
      .roughnessTexture = material.roughnessTexture,
      .specularTexture = material.specularTexture,
      .emissiveTexture = material.emissiveTexture,
      .normalTexture = material.normalTexture,
      .heightTexture = material.heightTexture,
    };
  }

  void RestoreMaterial(Material& material, const MaterialSnapshot& snapshot)
  {
    material.name = snapshot.name;
    material.albedo = snapshot.albedo;
    material.emissivity = snapshot.emissivity;
    material.roughness = snapshot.roughness;
    material.metallic = snapshot.metallic;
    material.roughnessFactor = snapshot.roughnessFactor;
    material.metallicFactor = snapshot.metallicFactor;
    material.specular = snapshot.specular;
    material.sg = snapshot.sg;
    material.hasAlpha = snapshot.hasAlpha;
    material.alphaTest = snapshot.alphaTest;
    material.combinedTextures = snapshot.combinedTextures;
    material.doubleSided = snapshot.doubleSided;
    material.transparent = snapshot.transparent;
    material.opacity = snapshot.opacity;
    material.shadingModel = snapshot.shadingModel;
    material.baseColorTexture = snapshot.baseColorTexture;
    material.metallicTexture = snapshot.metallicTexture;
    material.roughnessTexture = snapshot.roughnessTexture;
    material.specularTexture = snapshot.specularTexture;
    material.emissiveTexture = snapshot.emissiveTexture;
    material.normalTexture = snapshot.normalTexture;
    material.heightTexture = snapshot.heightTexture;
    // Not restoring generation: the GPU side skips a rebind when it matches what it
    // already bound. Callers re-baseline the snapshot instead.
    material.MarkChanged();
  }

  uint32_t ModelTemplate::FindByIndexPath(const std::string& path) const
  {
    for (uint32_t i = 0; i < nodes.size(); i++)
    {
      if (nodes[i].indexPath == path)
        return i;
    }
    return INVALID_NODE;
  }

  uint32_t ModelTemplate::FindByNamePath(const std::string& path) const
  {
    uint32_t found = INVALID_NODE;

    for (uint32_t i = 0; i < nodes.size(); i++)
    {
      if (nodes[i].namePath != path)
        continue;

      if (found != INVALID_NODE)
        return INVALID_NODE;

      found = i;
    }

    return found;
  }

  ModelTemplate BuildModelTemplate(const ModelDescription& desc)
  {
    ModelTemplate tmpl;
    tmpl.materials = desc.materials;
    tmpl.sourcePath = desc.sourcePath;

    FlattenNode(desc.root, ModelTemplate::INVALID_NODE, "", "", tmpl);

    tmpl.fingerprint = ComputeFingerprint(tmpl);

    return tmpl;
  }
}
