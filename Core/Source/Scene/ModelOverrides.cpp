#include "ModelOverrides.h"

#include "ComponentRegistry.h"
#include "CoreComponentSerializers.h"
#include "Components.h"
#include "YamlUtils.h"
#include "Assets/AssetManager.h"
#include "Utils/Log.h"

namespace YAEngine::ModelOverrides
{
  static constexpr const char* NODE_SEPARATOR = "::";

  // Keys of a node patch that are not components
  static bool IsPatchMetaKey(const std::string& key)
  {
    return key == "node" || key == "namePath" || key == "transform"
        || key == "name" || key == "parent";
  }

  static Model* FindModel(Scene& scene, AssetManager& assets, Entity modelRoot)
  {
    auto* model = assets.Models().FindByRoot(scene, modelRoot);
    return (model != nullptr && model->modelTemplate) ? model : nullptr;
  }

  std::vector<Entity> CollectNodeEntities(Scene& scene, AssetManager& assets, Entity modelRoot)
  {
    auto* model = FindModel(scene, assets, modelRoot);
    if (model == nullptr)
      return {};

    std::vector<Entity> entities(model->modelTemplate->nodes.size(), Entity(entt::null));

    // Collected from the component rather than by walking the hierarchy, so a node that
    // was reparented elsewhere still counts as alive instead of looking destroyed
    auto view = scene.GetView<ModelNodeComponent>();
    for (auto entity : view)
    {
      auto& node = view.get<ModelNodeComponent>(entity);
      if (node.modelRoot != modelRoot || node.nodeIndex >= entities.size())
        continue;
      entities[node.nodeIndex] = entity;
    }

    return entities;
  }

  bool SplitEntityReference(const std::string& reference, std::string& outName, std::string& outNodePath)
  {
    auto pos = reference.find(NODE_SEPARATOR);
    if (pos == std::string::npos)
      return false;

    outName = reference.substr(0, pos);
    outNodePath = reference.substr(pos + std::strlen(NODE_SEPARATOR));
    return true;
  }

  std::string MakeEntityReference(Scene& scene, AssetManager& assets, Entity entity)
  {
    if (!scene.HasComponent<ModelNodeComponent>(entity))
      return scene.GetName(entity);

    auto& node = scene.GetComponent<ModelNodeComponent>(entity);
    // The model root is addressable by its own name - it is a regular scene entry
    if (node.nodeIndex == 0 || node.modelRoot == entt::null)
      return scene.GetName(entity);

    auto* model = FindModel(scene, assets, node.modelRoot);
    if (model == nullptr || node.nodeIndex >= model->modelTemplate->nodes.size())
      return scene.GetName(entity);

    return scene.GetName(node.modelRoot) + NODE_SEPARATOR
         + model->modelTemplate->nodes[node.nodeIndex].indexPath;
  }

  Entity ResolveNodeReference(Scene& scene, AssetManager& assets, Entity modelRoot,
    const std::string& nodePath)
  {
    auto* model = FindModel(scene, assets, modelRoot);
    if (model == nullptr)
      return entt::null;

    uint32_t index = model->modelTemplate->FindByIndexPath(nodePath);
    if (index == ModelTemplate::INVALID_NODE)
      return entt::null;

    auto entities = CollectNodeEntities(scene, assets, modelRoot);
    return index < entities.size() ? entities[index] : Entity(entt::null);
  }

  // Index path first, name path as the fallback: the former survives renames, the latter
  // survives reordering, and neither alone survives a re-export
  static uint32_t ResolvePatchNode(const ModelTemplate& tmpl, const YAML::Node& patch,
    const std::string& modelName)
  {
    if (!patch["node"])
      return ModelTemplate::INVALID_NODE;

    auto indexPath = patch["node"].as<std::string>();
    auto namePath = patch["namePath"] ? patch["namePath"].as<std::string>() : std::string();

    uint32_t byIndex = tmpl.FindByIndexPath(indexPath);
    if (byIndex != ModelTemplate::INVALID_NODE
      && (namePath.empty() || tmpl.nodes[byIndex].namePath == namePath))
    {
      return byIndex;
    }

    uint32_t byName = namePath.empty()
      ? ModelTemplate::INVALID_NODE
      : tmpl.FindByNamePath(namePath);

    if (byName != ModelTemplate::INVALID_NODE)
    {
      YA_LOG_WARN("Scene", "Model '%s': node '%s' moved to '%s', matched by name path '%s'",
        modelName.c_str(), indexPath.c_str(), tmpl.nodes[byName].indexPath.c_str(), namePath.c_str());
      return byName;
    }

    return ModelTemplate::INVALID_NODE;
  }

  // Sparse diff of a slot material against its imported state. Writing the whole material
  // instead would freeze every value the model should keep providing and would drag every
  // texture path into the scene file for materials nobody touched.
  static YAML::Node SerializeMaterialDiff(const Material& material, const MaterialSnapshot& pristine,
    AssetManager& assets)
  {
    YAML::Node n;

    if (material.name != pristine.name) n["name"] = material.name;
    if (material.albedo != pristine.albedo) n["albedo"] = SerializeVec3(material.albedo);
    if (material.emissivity != pristine.emissivity) n["emissivity"] = SerializeVec3(material.emissivity);
    if (material.roughness != pristine.roughness) n["roughness"] = material.roughness;
    if (material.metallic != pristine.metallic) n["metallic"] = material.metallic;
    if (material.roughnessFactor != pristine.roughnessFactor) n["roughnessFactor"] = material.roughnessFactor;
    if (material.metallicFactor != pristine.metallicFactor) n["metallicFactor"] = material.metallicFactor;
    if (material.specular != pristine.specular) n["specular"] = material.specular;
    if (material.sg != pristine.sg) n["sg"] = material.sg;
    if (material.alphaTest != pristine.alphaTest) n["alphaTest"] = material.alphaTest;
    if (material.combinedTextures != pristine.combinedTextures) n["combinedTextures"] = material.combinedTextures;
    if (material.doubleSided != pristine.doubleSided) n["doubleSided"] = material.doubleSided;
    if (material.transparent != pristine.transparent) n["transparent"] = material.transparent;
    if (material.opacity != pristine.opacity) n["opacity"] = material.opacity;
    if (material.shadingModel != pristine.shadingModel)
      n["shadingModel"] = (material.shadingModel == ShadingModel::Unlit) ? "unlit" : "lit";

    auto& textures = assets.Textures();

    auto diffTexture = [&](const std::string& key, TextureHandle current, TextureHandle base)
    {
      if (current == base)
        return;

      // Empty scalar means "cleared by hand", which a missing key cannot express
      if (!current)
      {
        n[key] = "";
        return;
      }

      size_t before = n.size();
      SerializeTextureField(n, key, current, textures, assets);
      if (n.size() == before)
      {
        YA_LOG_WARN("Scene", "Material '%s': texture '%s' is embedded in a model file and cannot be stored",
          material.name.c_str(), key.c_str());
      }
    };

    diffTexture("baseColorTexture", material.baseColorTexture, pristine.baseColorTexture);
    diffTexture("metallicTexture", material.metallicTexture, pristine.metallicTexture);
    diffTexture("roughnessTexture", material.roughnessTexture, pristine.roughnessTexture);
    diffTexture("specularTexture", material.specularTexture, pristine.specularTexture);
    diffTexture("emissiveTexture", material.emissiveTexture, pristine.emissiveTexture);
    diffTexture("normalTexture", material.normalTexture, pristine.normalTexture);
    diffTexture("heightTexture", material.heightTexture, pristine.heightTexture);

    return n;
  }

  // Applied onto the live slot material rather than into a fresh one: anything the diff
  // does not mention - embedded textures above all - stays exactly as the model built it.
  static void ApplyMaterialDiff(Material& material, const YAML::Node& n, AssetManager& assets)
  {
    if (n["name"]) material.name = n["name"].as<std::string>();
    if (n["albedo"]) material.albedo = DeserializeVec3(n["albedo"]);
    if (n["emissivity"]) material.emissivity = DeserializeVec3(n["emissivity"]);
    if (n["roughness"]) material.roughness = n["roughness"].as<float>();
    if (n["metallic"]) material.metallic = n["metallic"].as<float>();
    if (n["roughnessFactor"]) material.roughnessFactor = n["roughnessFactor"].as<float>();
    if (n["metallicFactor"]) material.metallicFactor = n["metallicFactor"].as<float>();
    if (n["specular"]) material.specular = n["specular"].as<float>();
    if (n["sg"]) material.sg = n["sg"].as<bool>();
    if (n["alphaTest"]) material.alphaTest = n["alphaTest"].as<bool>();
    if (n["combinedTextures"]) material.combinedTextures = n["combinedTextures"].as<bool>();
    if (n["doubleSided"]) material.doubleSided = n["doubleSided"].as<bool>();
    if (n["transparent"]) material.transparent = n["transparent"].as<bool>();
    if (n["opacity"]) material.opacity = n["opacity"].as<float>();
    if (n["shadingModel"])
    {
      auto mode = n["shadingModel"].as<std::string>();
      material.shadingModel = (mode == "unlit") ? ShadingModel::Unlit : ShadingModel::Lit;
    }

    auto applyTexture = [&](const std::string& key, TextureHandle& target, bool* hasAlpha)
    {
      if (!n[key])
        return;

      if (n[key].IsScalar() && n[key].as<std::string>().empty())
      {
        target = {};
        return;
      }

      target = DeserializeTextureField(n, key, assets, hasAlpha);
    };

    applyTexture("baseColorTexture", material.baseColorTexture, &material.hasAlpha);
    applyTexture("metallicTexture", material.metallicTexture, nullptr);
    applyTexture("roughnessTexture", material.roughnessTexture, nullptr);
    applyTexture("specularTexture", material.specularTexture, nullptr);
    applyTexture("emissiveTexture", material.emissiveTexture, nullptr);
    applyTexture("normalTexture", material.normalTexture, nullptr);
    applyTexture("heightTexture", material.heightTexture, nullptr);

    material.MarkChanged();
  }

  static YAML::Node SerializeNodePatch(Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, const Model& model, const std::vector<Entity>& entities,
    uint32_t nodeIndex, Entity entity)
  {
    auto& tmpl = *model.modelTemplate;
    auto& templateNode = tmpl.nodes[nodeIndex];

    YAML::Node patch;

    auto& transform = scene.GetTransform(entity);
    YAML::Node transformNode;
    if (transform.position != templateNode.position)
      transformNode["position"] = SerializeVec3(transform.position);
    if (transform.rotation != templateNode.rotation)
      transformNode["rotation"] = SerializeQuat(transform.rotation);
    if (transform.scale != templateNode.scale)
      transformNode["scale"] = SerializeVec3(transform.scale);
    if (transformNode.size() > 0)
      patch["transform"] = transformNode;

    if (scene.GetName(entity) != templateNode.name)
      patch["name"] = scene.GetName(entity);

    Entity expectedParent = templateNode.parent == ModelTemplate::INVALID_NODE
      ? Entity(entt::null)
      : entities[templateNode.parent];
    Entity actualParent = scene.GetHierarchy(entity).parent;
    if (actualParent != expectedParent && actualParent != entt::null)
      patch["parent"] = MakeEntityReference(scene, assets, actualParent);

    auto components = registry.SerializeAll(scene.GetRegistry(), entity);
    for (auto it = components.begin(); it != components.end(); ++it)
    {
      auto key = it->first.as<std::string>();
      // Mesh and material identity comes from the model file: the mesh is a raw vertex
      // array with no persistent name, the material is handled per slot below
      if (key == "mesh" || key == "material")
        continue;
      if (it->second.IsNull() || (it->second.IsMap() && it->second.size() == 0))
        continue;
      patch[key] = it->second;
    }

    if (scene.HasComponent<MaterialComponent>(entity))
    {
      // A node still on its slot material is covered by the slot diff below; only a
      // material assigned to this node alone travels inside the node patch
      auto handle = scene.GetComponent<MaterialComponent>(entity).asset;
      uint32_t slot = templateNode.materialIndex.value_or(ModelTemplate::INVALID_NODE);
      bool nativeSlot = slot < model.slotMaterials.size() && model.slotMaterials[slot] == handle;

      if (!nativeSlot)
      {
        auto materialNode = registry.Serialize("material", scene.GetRegistry(), entity);
        if (!materialNode.IsNull())
          patch["material"] = materialNode;
      }
    }

    return patch;
  }

  YAML::Node Serialize(Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Entity modelRoot)
  {
    auto* model = FindModel(scene, assets, modelRoot);
    if (model == nullptr)
      return {};

    auto& tmpl = *model->modelTemplate;
    auto entities = CollectNodeEntities(scene, assets, modelRoot);

    YAML::Node nodes;
    YAML::Node materials;
    YAML::Node removed;

    // Node 0 is the model root: its own transform and components are written by the
    // regular entity path, so patching it here would duplicate them
    for (uint32_t i = 1; i < tmpl.nodes.size(); i++)
    {
      Entity entity = entities[i];
      auto& templateNode = tmpl.nodes[i];

      if (entity == entt::null)
      {
        // DestroyEntity is recursive, so only the topmost node of a gone subtree is stored
        if (templateNode.parent == ModelTemplate::INVALID_NODE || entities[templateNode.parent] != entt::null)
          removed.push_back(templateNode.indexPath);
        continue;
      }

      auto patch = SerializeNodePatch(scene, assets, registry, *model, entities, i, entity);
      if (patch.size() == 0)
        continue;

      patch["node"] = templateNode.indexPath;
      patch["namePath"] = templateNode.namePath;
      nodes.push_back(patch);
    }

    for (uint32_t slot = 0; slot < model->slotMaterials.size(); slot++)
    {
      auto handle = model->slotMaterials[slot];
      if (!assets.Materials().Has(handle))
        continue;

      auto& material = assets.Materials().Get(handle);

      // Every editor edit goes through MarkChanged(), so an unchanged generation means
      // the material is still exactly what the import produced
      if (material.generation == model->pristineMaterials[slot].generation)
        continue;

      auto diff = SerializeMaterialDiff(material, model->pristineMaterials[slot], assets);
      if (diff.size() == 0)
        continue;

      diff["slot"] = slot;
      diff["sourceName"] = tmpl.materials[slot].name;
      materials.push_back(diff);
    }

    if (scene.HasComponent<ModelOverridesComponent>(modelRoot))
    {
      auto& stash = scene.GetComponent<ModelOverridesComponent>(modelRoot);
      for (auto& raw : stash.unresolvedNodes)
        nodes.push_back(YAML::Load(raw));
      for (auto& raw : stash.unresolvedMaterials)
        materials.push_back(YAML::Load(raw));
      for (auto& raw : stash.unresolvedRemoved)
        removed.push_back(raw);
    }

    YAML::Node result;
    if (nodes.size() > 0)
      result["nodes"] = nodes;
    if (materials.size() > 0)
      result["materials"] = materials;
    if (removed.size() > 0)
      result["removed"] = removed;

    if (result.size() == 0)
      return {};

    result["fingerprint"] = tmpl.fingerprint;
    return result;
  }

  static void ApplyMaterialPatches(const YAML::Node& patches, AssetManager& assets,
    Model& model, const std::string& modelName, ModelOverridesComponent& stash)
  {
    auto& tmpl = *model.modelTemplate;

    for (auto patch : patches)
    {
      uint32_t slot = patch["slot"] ? patch["slot"].as<uint32_t>() : ModelTemplate::INVALID_NODE;
      auto sourceName = patch["sourceName"] ? patch["sourceName"].as<std::string>() : std::string();

      bool slotMatches = slot < tmpl.materials.size()
        && (sourceName.empty() || tmpl.materials[slot].name == sourceName);

      // A re-export can shift slot numbering; the material name is the fallback identity
      if (!slotMatches && !sourceName.empty())
      {
        for (uint32_t i = 0; i < tmpl.materials.size(); i++)
        {
          if (tmpl.materials[i].name != sourceName)
            continue;
          slot = i;
          slotMatches = true;
          break;
        }
      }

      auto handle = slotMatches ? model.slotMaterials[slot] : MaterialHandle {};

      if (!handle || !assets.Materials().Has(handle))
      {
        stash.unresolvedMaterials.push_back(YAML::Dump(patch));
        YA_LOG_WARN("Scene", "Model '%s': material slot patch '%s' did not resolve",
          modelName.c_str(), sourceName.c_str());
        continue;
      }

      // Patched in place, so every node sharing the slot keeps pointing at it and
      // pristineMaterials still describes the import - which is what makes the patch
      // survive the next save
      ApplyMaterialDiff(assets.Materials().Get(handle), patch, assets);
    }
  }

  static void ApplyNodePatches(const YAML::Node& patches, Scene& scene,
    const ComponentRegistry& registry, const ModelTemplate& tmpl, const std::vector<Entity>& entities,
    const std::string& modelName, ModelOverridesComponent& stash,
    std::vector<ParentRequest>& parentRequests)
  {
    for (auto patch : patches)
    {
      uint32_t index = ResolvePatchNode(tmpl, patch, modelName);
      Entity entity = index < entities.size() ? entities[index] : Entity(entt::null);

      if (entity == entt::null)
      {
        stash.unresolvedNodes.push_back(YAML::Dump(patch));
        YA_LOG_WARN("Scene", "Model '%s': node patch '%s' did not resolve", modelName.c_str(),
          patch["node"] ? patch["node"].as<std::string>().c_str() : "?");
        continue;
      }

      if (patch["transform"])
      {
        auto& transform = scene.GetTransform(entity);
        auto transformNode = patch["transform"];
        if (transformNode["position"]) transform.position = DeserializeVec3(transformNode["position"]);
        if (transformNode["rotation"]) transform.rotation = DeserializeQuat(transformNode["rotation"]);
        if (transformNode["scale"]) transform.scale = DeserializeVec3(transformNode["scale"]);
        scene.MarkDirty(entity);
      }

      if (patch["name"])
        scene.GetName(entity) = patch["name"].as<std::string>();

      if (patch["parent"])
        parentRequests.push_back({ entity, patch["parent"].as<std::string>() });

      for (auto it = patch.begin(); it != patch.end(); ++it)
      {
        auto key = it->first.as<std::string>();
        if (IsPatchMetaKey(key))
          continue;
        if (!registry.Deserialize(key, scene.GetRegistry(), entity, it->second))
          YA_LOG_WARN("Scene", "Unknown component '%s' on node of model '%s'", key.c_str(), modelName.c_str());
      }
    }
  }

  std::vector<ParentRequest> Apply(const YAML::Node& node, Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Entity modelRoot)
  {
    std::vector<ParentRequest> parentRequests;

    if (!node || !node.IsMap())
      return parentRequests;

    const std::string modelName = scene.GetName(modelRoot);

    auto* model = FindModel(scene, assets, modelRoot);
    if (model == nullptr)
    {
      YA_LOG_WARN("Scene", "Model overrides on '%s' dropped - the model asset is missing", modelName.c_str());
      return parentRequests;
    }

    auto& tmpl = *model->modelTemplate;
    auto entities = CollectNodeEntities(scene, assets, modelRoot);

    if (node["fingerprint"] && node["fingerprint"].as<uint64_t>() != tmpl.fingerprint)
    {
      YA_LOG_WARN("Scene", "Model '%s' changed since the scene was saved - some overrides may not apply",
        modelName.c_str());
    }

    ModelOverridesComponent stash;

    // Slots first so a node level material override still wins over its slot
    if (node["materials"])
      ApplyMaterialPatches(node["materials"], assets, *model, modelName, stash);

    if (node["nodes"])
      ApplyNodePatches(node["nodes"], scene, registry, tmpl, entities, modelName, stash, parentRequests);

    // Removals last: a patch targeting a doomed node would otherwise look unresolved
    if (node["removed"])
    {
      for (auto entry : node["removed"])
      {
        auto path = entry.as<std::string>();
        uint32_t index = tmpl.FindByIndexPath(path);
        Entity entity = index < entities.size() ? entities[index] : Entity(entt::null);

        if (entity == entt::null)
        {
          stash.unresolvedRemoved.push_back(path);
          continue;
        }

        scene.DestroyEntity(entity);
      }
    }

    if (!stash.unresolvedNodes.empty() || !stash.unresolvedMaterials.empty() || !stash.unresolvedRemoved.empty())
    {
      YA_LOG_WARN("Scene", "Model '%s': %zu node, %zu material and %zu removal overrides kept unresolved",
        modelName.c_str(), stash.unresolvedNodes.size(), stash.unresolvedMaterials.size(),
        stash.unresolvedRemoved.size());
      scene.GetRegistry().emplace_or_replace<ModelOverridesComponent>(modelRoot, std::move(stash));
    }

    return parentRequests;
  }

#ifdef YA_EDITOR

  // Template node behind an entity, or nullptr when it is not a model node (or is the
  // model root, which the regular entity path already owns)
  static const ModelTemplateNode* FindTemplateNode(Scene& scene, AssetManager& assets,
    Entity entity, Model** outModel)
  {
    if (!scene.HasComponent<ModelNodeComponent>(entity))
      return nullptr;

    auto& node = scene.GetComponent<ModelNodeComponent>(entity);
    if (node.nodeIndex == 0)
      return nullptr;

    auto* model = FindModel(scene, assets, node.modelRoot);
    if (model == nullptr || node.nodeIndex >= model->modelTemplate->nodes.size())
      return nullptr;

    if (outModel != nullptr)
      *outModel = model;

    return &model->modelTemplate->nodes[node.nodeIndex];
  }

  bool IsNodeOverridden(Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Entity entity)
  {
    Model* model = nullptr;
    const auto* templateNode = FindTemplateNode(scene, assets, entity, &model);
    if (templateNode == nullptr)
      return false;

    auto& transform = scene.GetTransform(entity);
    if (transform.position != templateNode->position
      || transform.rotation != templateNode->rotation
      || transform.scale != templateNode->scale)
    {
      return true;
    }

    if (scene.GetName(entity) != templateNode->name)
      return true;

    // ModelBuilder only ever creates mesh and material, so any other registered
    // component on the node was added by hand
    for (auto& [name, entry] : registry.GetEntries())
    {
      if (name == "mesh" || name == "material")
        continue;
      if (entry.has(scene.GetRegistry(), entity))
        return true;
    }

    return IsMaterialOverridden(scene, assets, entity);
  }

  bool IsMaterialOverridden(Scene& scene, AssetManager& assets, Entity entity)
  {
    if (!scene.HasComponent<MaterialComponent>(entity))
      return false;

    Model* model = nullptr;
    const auto* templateNode = FindTemplateNode(scene, assets, entity, &model);
    if (templateNode == nullptr)
      return false;

    uint32_t slot = templateNode->materialIndex.value_or(ModelTemplate::INVALID_NODE);
    if (slot >= model->slotMaterials.size())
      return false;

    auto handle = scene.GetComponent<MaterialComponent>(entity).asset;
    if (model->slotMaterials[slot] != handle)
      return true;

    if (!assets.Materials().Has(handle))
      return false;

    return assets.Materials().Get(handle).generation != model->pristineMaterials[slot].generation;
  }

  void RevertNode(Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Entity entity)
  {
    Model* model = nullptr;
    const auto* templateNode = FindTemplateNode(scene, assets, entity, &model);
    if (templateNode == nullptr)
      return;

    auto& transform = scene.GetTransform(entity);
    transform.position = templateNode->position;
    transform.rotation = templateNode->rotation;
    transform.scale = templateNode->scale;
    scene.MarkDirty(entity);

    scene.GetName(entity) = templateNode->name;

    for (auto& [name, entry] : registry.GetEntries())
    {
      if (name == "mesh" || name == "material")
        continue;
      registry.Remove(name, scene.GetRegistry(), entity);
    }

    RevertMaterial(scene, assets, entity);
  }

  void RevertMaterial(Scene& scene, AssetManager& assets, Entity entity)
  {
    Model* model = nullptr;
    const auto* templateNode = FindTemplateNode(scene, assets, entity, &model);
    if (templateNode == nullptr)
      return;

    uint32_t slot = templateNode->materialIndex.value_or(ModelTemplate::INVALID_NODE);
    if (slot >= model->slotMaterials.size())
      return;

    auto handle = model->slotMaterials[slot];
    if (!assets.Materials().Has(handle))
      return;

    // A material the node was given by hand is left alive: it may be shared with
    // entities outside this model
    scene.GetRegistry().emplace_or_replace<MaterialComponent>(entity, handle);

    auto& material = assets.Materials().Get(handle);
    RestoreMaterial(material, model->pristineMaterials[slot]);
    // RestoreMaterial bumps the generation so the GPU rebinds; re-baseline the snapshot
    // or the slot would keep reporting itself as overridden
    model->pristineMaterials[slot].generation = material.generation;
  }

#endif
}
