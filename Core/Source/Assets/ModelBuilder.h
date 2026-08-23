#pragma once

#include "ModelDescription.h"
#include "ModelTemplate.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class AssetManager;

  struct ModelBuildResult
  {
    Entity rootEntity { entt::null };

    // Indexed by material index of the source model. Meshes sharing a material index
    // share one handle, so the serializer can tell an untouched slot from a node that
    // was given a material of its own.
    std::vector<MaterialHandle> slotMaterials;
    std::vector<MaterialSnapshot> pristineMaterials;
  };

  class ModelBuilder
  {
  public:

    ModelBuilder() = default;
    ModelBuilder(Scene* scene, AssetManager* assetManager)
      : m_Scene(scene), m_AssetManager(assetManager)
    {
    }

    ModelBuildResult Build(const ModelDescription& desc, const ModelTemplate& tmpl);

  private:

    struct BuildContext
    {
      const ModelDescription& desc;
      const ModelTemplate& tmpl;
      ModelBuildResult result;
      // Node 0 is the model root, so children start at 1. Must advance in the same
      // pre-order BuildModelTemplate uses or node indices desync from the template.
      uint32_t nextNodeIndex = 1;
    };

    Entity BuildNode(const NodeDescription& node, Entity parent, BuildContext& ctx);
    MaterialHandle GetOrCreateSlotMaterial(uint32_t materialIndex, BuildContext& ctx);

    Scene* m_Scene = nullptr;
    AssetManager* m_AssetManager = nullptr;
  };
}
