#pragma once

#include <yaml-cpp/yaml.h>
#include <entt/entt.hpp>

#include "Scene/Scene.h"

namespace YAEngine
{
  class AssetManager;
  class ComponentRegistry;

  // Persistence of authored changes inside an imported model subtree. The model file stays
  // the source of truth - only the delta against the imported state reaches the scene file.
  namespace ModelOverrides
  {
    // Deferred parenting, resolved by the serializer together with the regular ones
    using ParentRequest = std::pair<Entity, std::string>;

    // Null node when the subtree still matches the source model exactly
    YAML::Node Serialize(Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Entity modelRoot);

    // Applies a "modelOverrides" node onto an already built subtree. Returned parent
    // requests must be resolved after every scene entity exists.
    std::vector<ParentRequest> Apply(const YAML::Node& node, Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Entity modelRoot);

    // Live entity per template node index, entt::null for destroyed nodes
    std::vector<Entity> CollectNodeEntities(Scene& scene, AssetManager& assets, Entity modelRoot);

    // "Cafe::0/3" for a node inside a model, plain entity name otherwise
    std::string MakeEntityReference(Scene& scene, AssetManager& assets, Entity entity);

    // Same reference built from a template node index instead of a live entity. Empty for the
    // model root, which is addressable by its own name, and for an index the model does not have.
    std::string MakeNodeReference(Scene& scene, AssetManager& assets, Entity modelRoot, uint32_t nodeIndex);

    // Splits "Cafe::0/3" into the model root name and the node path. False for a plain name.
    bool SplitEntityReference(const std::string& reference, std::string& outName, std::string& outNodePath);

    // Node of an already built model subtree, or entt::null when the path does not resolve
    Entity ResolveNodeReference(Scene& scene, AssetManager& assets, Entity modelRoot,
      const std::string& nodePath);

#ifdef YA_EDITOR
    // Cheaper than building the patch: tells the outliner whether a node differs from
    // its imported state. Covers this node only, not its subtree.
    bool IsNodeOverridden(Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Entity entity);

    // True when the node carries a material of its own or its slot material was edited
    bool IsMaterialOverridden(Scene& scene, AssetManager& assets, Entity entity);

    // Restores transform, name and slot material of a node and strips every registered
    // component the model itself did not create
    void RevertNode(Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Entity entity);

    // Puts a node back on its imported material: reattaches the shared slot material and
    // resets its values. A hand-assigned material is left alive - it may be shared.
    void RevertMaterial(Scene& scene, AssetManager& assets, Entity entity);
#endif
  }
}
