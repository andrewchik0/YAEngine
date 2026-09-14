#include "Editor/Bridge/BridgeData.h"

#include "Assets/AssetManager.h"
#include "Editor/Bridge/BridgeTypes.h"
#include "Render/Render.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/CoreComponentSerializers.h"
#include "Scene/ModelOverrides.h"
#include "Scene/SceneSerializer.h"
#include "Scene/YamlUtils.h"
#include "Utils/Log.h"
#include "Utils/ServiceRegistry.h"

namespace YAEngine
{
  namespace
  {
    constexpr int64_t ENTITIES_DEFAULT_MAX_COUNT = 2000;
    // Leaves room under the 4 MB message limit for JSON escaping of the names
    constexpr size_t ENTITIES_BYTE_BUDGET = 3 * 1024 * 1024;
    // LocalTransform is written by the scene serializer itself rather than through the component
    // registry, so it is exposed under the key the scene file uses for it.
    constexpr const char* TRANSFORM_COMPONENT = "transform";

    using EntityId = std::underlying_type_t<Entity>;

    std::string ToLowerAscii(std::string_view text)
    {
      std::string lower(text);
      for (char& c : lower)
      {
        if (c >= 'A' && c <= 'Z')
          c = static_cast<char>(c - 'A' + 'a');
      }
      return lower;
    }

    std::string EmitYaml(const YAML::Node& node)
    {
      // The emitter writes nothing at all for a null document
      if (!node || node.IsNull())
        return "~";

      YAML::Emitter out;
      out << node;
      return out.c_str();
    }

    // An empty document is an empty patch.
    bool ParseYamlMapping(const Json& params, YAML::Node& outNode, std::string& outError)
    {
      auto it = params.find("yaml");
      if (it == params.end() || !it->is_string())
      {
        outError = "'yaml' must be a string holding a YAML mapping";
        return false;
      }

      try
      {
        outNode = YAML::Load(it->get<std::string>());
      }
      catch (const YAML::Exception& e)
      {
        outError = std::string("cannot parse 'yaml': ") + e.what();
        return false;
      }

      if (outNode.IsNull())
        outNode = YAML::Node(YAML::NodeType::Map);

      if (!outNode.IsMap())
      {
        outError = "'yaml' must be a YAML mapping of the keys to change";
        return false;
      }
      return true;
    }

    // Mappings merge key by key; a sequence or a scalar in the patch replaces what was there.
    YAML::Node MergeYaml(const YAML::Node& base, const YAML::Node& patch)
    {
      if (!base.IsMap() || !patch.IsMap())
        return YAML::Clone(patch);

      YAML::Node merged = YAML::Clone(base);
      for (auto it = patch.begin(); it != patch.end(); ++it)
      {
        const std::string& key = it->first.Scalar();
        const YAML::Node existing = base[key];
        merged[key] = existing ? MergeYaml(existing, it->second) : YAML::Clone(it->second);
      }
      return merged;
    }

    std::string ListComponentNames(const ComponentRegistry& components)
    {
      std::vector<std::string> names;
      for (const auto& [name, entry] : components.GetEntries())
        names.push_back(name);
      std::sort(names.begin(), names.end());

      std::string list = TRANSFORM_COMPONENT;
      for (const std::string& name : names)
        list += ", " + name;
      return list;
    }

    bool HasComponent(Scene& scene, const ComponentRegistry& components, Entity entity, const std::string& name)
    {
      if (name == TRANSFORM_COMPONENT)
        return scene.HasComponent<LocalTransform>(entity);

      return components.GetEntries().at(name).has(scene.GetRegistry(), entity);
    }

    YAML::Node SerializeComponent(Scene& scene, const ComponentRegistry& components, Entity entity,
      const std::string& name)
    {
      if (name != TRANSFORM_COMPONENT)
        return components.Serialize(name, scene.GetRegistry(), entity);

      const LocalTransform& transform = scene.GetTransform(entity);
      YAML::Node node;
      node["position"] = SerializeVec3(transform.position);
      node["rotation"] = SerializeQuat(transform.rotation);
      node["scale"] = SerializeVec3(transform.scale);
      return node;
    }

    void ApplyTransform(Scene& scene, Entity entity, const YAML::Node& node)
    {
      LocalTransform transform = scene.GetTransform(entity);
      if (node["position"]) transform.position = DeserializeVec3(node["position"]);
      if (node["rotation"]) transform.rotation = DeserializeQuat(node["rotation"]);
      if (node["scale"]) transform.scale = DeserializeVec3(node["scale"]);

      // Assigned once every value converted, so a bad one leaves the entity where it was
      scene.GetTransform(entity) = transform;
      scene.MarkDirty(entity);
    }

    // Deserializers rebuild a component from the fields the scene file carries; live editor state
    // outside the file is copied back from the component as it was.
    template<typename T, typename Restore>
    void DeserializeKeeping(const ComponentRegistry& components, entt::registry& reg, Entity entity,
      const std::string& name, const YAML::Node& node, Restore restore)
    {
      T previous = reg.get<T>(entity);
      components.Deserialize(name, reg, entity, node);
      restore(previous, reg.get<T>(entity));
    }

    void ApplyRegistryComponent(Scene& scene, AssetManager& assets, const ComponentRegistry& components,
      Entity entity, const std::string& name, const YAML::Node& node)
    {
      entt::registry& reg = scene.GetRegistry();

      if (name == "camera")
      {
        DeserializeKeeping<CameraComponent>(components, reg, entity, name, node,
          [](const CameraComponent& previous, CameraComponent& current) {
            current.aspectRatio = previous.aspectRatio;
          });
      }
      else if (name == "model")
      {
        bool hadColliderDirty = reg.all_of<ModelColliderDirty>(entity);
        bool combinedChanged = false;
        bool colliderChanged = false;
        DeserializeKeeping<ModelSourceComponent>(components, reg, entity, name, node,
          [&](const ModelSourceComponent& previous, ModelSourceComponent& current) {
            // The handle is what links the root to its model asset, and the override layer finds
            // the model through it. The path stays the absolute one the model was loaded from.
            current.handle = previous.handle;
            current.path = previous.path;
            combinedChanged = current.combinedTextures != previous.combinedTextures;
            colliderChanged = current.colliderEnabled != previous.colliderEnabled
              || current.colliderHalfExtentsScale != previous.colliderHalfExtentsScale
              || current.colliderOffset != previous.colliderOffset
              || current.colliderIsStatic != previous.colliderIsStatic
              || current.colliderLayer != previous.colliderLayer
              || current.colliderMask != previous.colliderMask;
          });

        // The deserializer always asks for a collider rebuild, and a rebuild with the model collider
        // off removes a hand-made ColliderComponent. The Details panel asks only when a collider
        // field changed.
        if (!colliderChanged && !hadColliderDirty)
          reg.remove<ModelColliderDirty>(entity);

        if (combinedChanged)
        {
          ModelOverrides::SetCombinedTextures(scene, assets, entity,
            reg.get<ModelSourceComponent>(entity).combinedTextures);
        }
      }
      else if (name == "mesh")
      {
        MeshHandle previous = reg.get<MeshComponent>(entity).asset;
        components.Deserialize(name, reg, entity, node);
        // The render snapshot measures a mesh whose entity has no bounds, so stale ones are dropped
        if (!(reg.get<MeshComponent>(entity).asset == previous))
          reg.remove<LocalBounds>(entity);
      }
      else if (name == "reflectionProbe")
      {
        DeserializeKeeping<ReflectionProbeComponent>(components, reg, entity, name, node,
          [](const ReflectionProbeComponent& previous, ReflectionProbeComponent& current) {
            // The atlas still holds the bake as long as the probe points at the same file
            if (current.bakedPrefilterPath == previous.bakedPrefilterPath)
            {
              current.baked = previous.baked;
              current.atlasSlot = previous.atlasSlot;
            }
          });
      }
      else if (name == "irradianceVolume")
      {
        DeserializeKeeping<IrradianceVolumeComponent>(components, reg, entity, name, node,
          [](const IrradianceVolumeComponent& previous, IrradianceVolumeComponent& current) {
            if (current.bakedVolumePath == previous.bakedVolumePath)
            {
              current.baked = previous.baked;
              current.atlasSlot = previous.atlasSlot;
            }
          });
      }
      else if (name == "instancedCollider")
      {
        // The entries are generated by ScatterSystem and never reach the scene file
        DeserializeKeeping<InstancedColliderComponent>(components, reg, entity, name, node,
          [](InstancedColliderComponent& previous, InstancedColliderComponent& current) {
            current.instances = std::move(previous.instances);
          });
      }
      else
      {
        components.Deserialize(name, reg, entity, node);
      }
    }

    // Key of the first texture of the material that exists only inside a model file, or empty.
    std::string FindEmbeddedTexture(AssetManager& assets, MaterialHandle handle)
    {
      const Material* material = assets.Materials().TryGet(handle);
      if (material == nullptr)
        return {};

      const std::pair<const char*, TextureHandle> textures[] = {
        { "baseColorTexture", material->baseColorTexture },
        { "metallicTexture", material->metallicTexture },
        { "roughnessTexture", material->roughnessTexture },
        { "specularTexture", material->specularTexture },
        { "emissiveTexture", material->emissiveTexture },
        { "normalTexture", material->normalTexture },
        { "heightTexture", material->heightTexture },
      };

      for (const auto& [key, texture] : textures)
      {
        if (texture && IsEmbeddedTexturePath(assets.Textures().GetPath(texture).path))
          return key;
      }
      return {};
    }
  }

  void BridgeData::Init(ServiceRegistry& registry, BridgeMethodRegistry& methods, std::function<bool()> isCaptureBusy)
  {
    m_Registry = &registry;
    m_IsCaptureBusy = std::move(isCaptureBusy);

    methods.Register("scene.entities", [this](const Json& params, const BridgeReply& reply) {
      HandleEntities(params, reply);
    });

    methods.Register("scene.componentGet", [this](const Json& params, const BridgeReply& reply) {
      HandleComponentGet(params, reply);
    });

    methods.Register("scene.componentPatch", [this](const Json& params, const BridgeReply& reply) {
      HandleComponentPatch(params, reply);
    });

    methods.Register("render.settingsGet", [this](const Json&, const BridgeReply& reply) {
      HandleSettingsGet(reply);
    });

    methods.Register("render.settingsPatch", [this](const Json& params, const BridgeReply& reply) {
      HandleSettingsPatch(params, reply);
    });
  }

  void BridgeData::HandleEntities(const Json& params, const BridgeReply& reply)
  {
    std::string filter;
    bool includeEditorOnly = false;
    int64_t maxCount = ENTITIES_DEFAULT_MAX_COUNT;
    std::string error;
    if (!ReadOptionalParam(params, "filter", filter, error)
      || !ReadOptionalParam(params, "includeEditorOnly", includeEditorOnly, error)
      || !ReadOptionalParam(params, "maxCount", maxCount, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }

    if (maxCount < 1)
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'maxCount' must be at least 1");
      return;
    }

    Scene& scene = m_Registry->Get<Scene>();
    const ComponentRegistry& components = m_Registry->Get<ComponentRegistry>();
    entt::registry& reg = scene.GetRegistry();

    // Sorted once so every entity lists its components in the same order
    std::vector<const ComponentRegistry::Entry*> entries;
    for (const auto& [name, entry] : components.GetEntries())
      entries.push_back(&entry);
    std::sort(entries.begin(), entries.end(),
      [](const ComponentRegistry::Entry* a, const ComponentRegistry::Entry* b) { return a->name < b->name; });

    // A stack rather than recursion, since imported models nest deep. The highest index goes in
    // first, so roots come out in creation order.
    std::vector<Entity> pending;
    for (Entity root : scene.GetView<RootTag>())
      pending.push_back(root);
    std::sort(pending.begin(), pending.end(),
      [](Entity a, Entity b) { return entt::to_entity(a) > entt::to_entity(b); });

    static const std::string s_NoName;
    const std::string needle = ToLowerAscii(filter);
    Json list = Json::array();
    size_t bytes = 0;
    bool truncated = false;
    std::vector<Entity> children;

    while (!pending.empty())
    {
      Entity entity = pending.back();
      pending.pop_back();

      // Its subtree goes with it, as in the scene file
      if (!includeEditorOnly && reg.all_of<EditorOnlyTag>(entity))
        continue;

      const HierarchyComponent* hierarchy = reg.try_get<HierarchyComponent>(entity);
      if (hierarchy != nullptr)
      {
        children.clear();
        for (Entity child = hierarchy->firstChild; child != entt::null;
             child = reg.get<HierarchyComponent>(child).nextSibling)
        {
          children.push_back(child);
        }
        pending.insert(pending.end(), children.rbegin(), children.rend());
      }

      const Name* name = reg.try_get<Name>(entity);
      const std::string& entityName = name != nullptr ? *name : s_NoName;
      if (!needle.empty() && ToLowerAscii(entityName).find(needle) == std::string::npos)
        continue;

      if (list.size() >= static_cast<size_t>(maxCount))
      {
        truncated = true;
        break;
      }

      Json componentNames = Json::array();
      size_t entryBytes = entityName.size() * 2 + 64;
      if (reg.all_of<LocalTransform>(entity))
      {
        componentNames.push_back(TRANSFORM_COMPONENT);
        entryBytes += 12;
      }
      for (const ComponentRegistry::Entry* entry : entries)
      {
        if (!entry->has(reg, entity))
          continue;
        componentNames.push_back(entry->name);
        entryBytes += entry->name.size() + 3;
      }

      bytes += entryBytes;
      if (bytes > ENTITIES_BYTE_BUDGET)
      {
        truncated = true;
        break;
      }

      Json item = Json::object();
      item["id"] = entt::to_integral(entity);
      item["name"] = entityName;
      item["parent"] = hierarchy != nullptr && hierarchy->parent != entt::null
        ? Json(entt::to_integral(hierarchy->parent))
        : Json(nullptr);
      item["components"] = std::move(componentNames);
      list.push_back(std::move(item));
    }

    Json result = Json::object();
    result["entities"] = std::move(list);
    result["truncated"] = truncated;
    reply.Ok(std::move(result));
  }

  bool BridgeData::ResolveComponent(const Json& params, const BridgeReply& reply, Entity& outEntity,
    std::string& outComponent) const
  {
    int64_t id = -1;
    std::string error;
    if (!ReadOptionalParam(params, "entity", id, error)
      || !ReadOptionalParam(params, "component", outComponent, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return false;
    }

    if (id < 0 || id > static_cast<int64_t>(std::numeric_limits<EntityId>::max()))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'entity' must be an entity id as listed by scene.entities");
      return false;
    }

    if (outComponent.empty())
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'component' must name a component, e.g. transform or light");
      return false;
    }

    Scene& scene = m_Registry->Get<Scene>();
    const ComponentRegistry& components = m_Registry->Get<ComponentRegistry>();
    Entity entity = static_cast<Entity>(static_cast<EntityId>(id));

    if (!scene.GetRegistry().valid(entity))
    {
      reply.Fail(BridgeErrorCode::NOT_FOUND, "no entity with id " + std::to_string(id));
      return false;
    }

    // Refused the way actions refuse them: the editor camera and its kin exist for the editor itself
    if (scene.HasComponent<EditorOnlyTag>(entity))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "entity " + std::to_string(id) + " ('" + scene.GetName(entity)
        + "') belongs to the editor itself; the editor camera is driven with camera.set");
      return false;
    }

    if (outComponent != TRANSFORM_COMPONENT && !components.Has(outComponent))
    {
      reply.Fail(BridgeErrorCode::NOT_FOUND,
        "unknown component '" + outComponent + "'; components: " + ListComponentNames(components));
      return false;
    }

    if (!HasComponent(scene, components, entity, outComponent))
    {
      reply.Fail(BridgeErrorCode::NOT_FOUND,
        "entity " + std::to_string(id) + " has no '" + outComponent + "' component");
      return false;
    }

    outEntity = entity;
    return true;
  }

  bool BridgeData::RefuseWhileCapturing(const char* method, const BridgeReply& reply) const
  {
    if (!m_IsCaptureBusy || !m_IsCaptureBusy())
      return false;

    // A change would land in the shot, and the render settings go back to their snapshot when it ends
    reply.Fail(BridgeErrorCode::BUSY, std::string(method) + " has to wait for the running capture, which renders "
      "the scene as it stands and restores the render settings and the camera when it ends");
    return true;
  }

  void BridgeData::HandleComponentGet(const Json& params, const BridgeReply& reply)
  {
    Entity entity = entt::null;
    std::string component;
    if (!ResolveComponent(params, reply, entity, component))
      return;

    YAML::Node node = SerializeComponent(m_Registry->Get<Scene>(), m_Registry->Get<ComponentRegistry>(),
      entity, component);

    Json result = Json::object();
    result["yaml"] = EmitYaml(node);
    reply.Ok(std::move(result));
  }

  void BridgeData::HandleComponentPatch(const Json& params, const BridgeReply& reply)
  {
    YAML::Node patch;
    std::string error;
    if (!ParseYamlMapping(params, patch, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }

    Entity entity = entt::null;
    std::string component;
    if (!ResolveComponent(params, reply, entity, component))
      return;

    if (RefuseWhileCapturing("scene.componentPatch", reply))
      return;

    Scene& scene = m_Registry->Get<Scene>();
    AssetManager& assets = m_Registry->Get<AssetManager>();
    const ComponentRegistry& components = m_Registry->Get<ComponentRegistry>();

    const YAML::Node before = SerializeComponent(scene, components, entity, component);
    if (before.IsNull())
    {
      reply.Fail(BridgeErrorCode::FAILED, "'" + component + "' has no serializable state on this entity "
        "(a mesh imported from a model file, for example), so it cannot be patched");
      return;
    }
    if (!before.IsMap())
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'" + component + "' is a tag without fields; there is nothing to patch");
      return;
    }

    const YAML::Node merged = MergeYaml(before, patch);
    const std::string beforeText = EmitYaml(before);

    // A round trip is not free: it rebuilds terrain, regenerates scatter, allocates a material
    if (EmitYaml(merged) == beforeText)
    {
      Json result = Json::object();
      result["yaml"] = beforeText;
      reply.Ok(std::move(result));
      return;
    }

    if (component == "model" && merged["path"].Scalar() != before["path"].Scalar())
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS,
        "the path of a model cannot be patched; it names the file the subtree was built from");
      return;
    }

    if (component == "material")
    {
      std::string embedded = FindEmbeddedTexture(assets, scene.GetComponent<MaterialComponent>(entity).asset);
      if (!embedded.empty())
      {
        reply.Fail(BridgeErrorCode::FAILED, "the material's " + embedded + " is embedded in its model file; "
          "the scene file cannot reference it, so rebuilding the material from YAML would drop it");
        return;
      }
    }

    try
    {
      if (component == TRANSFORM_COMPONENT)
        ApplyTransform(scene, entity, merged);
      else
        ApplyRegistryComponent(scene, assets, components, entity, component, merged);
    }
    catch (const YAML::Exception& e)
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "cannot apply the patch to '" + component + "': " + e.what());
      return;
    }

    YA_LOG_INFO("Bridge", "Patched '%s' on entity %u ('%s')", component.c_str(),
      static_cast<unsigned>(entt::to_integral(entity)), scene.GetName(entity).c_str());

    Json result = Json::object();
    result["yaml"] = EmitYaml(SerializeComponent(scene, components, entity, component));
    reply.Ok(std::move(result));
  }

  void BridgeData::HandleSettingsGet(const BridgeReply& reply)
  {
    YAML::Node settings = SceneSerializer::SerializeRenderSettings(m_Registry->Get<Scene>(),
      m_Registry->Get<AssetManager>(), m_Registry->Get<Render>());

    Json result = Json::object();
    result["yaml"] = EmitYaml(settings);
    reply.Ok(std::move(result));
  }

  void BridgeData::HandleSettingsPatch(const Json& params, const BridgeReply& reply)
  {
    YAML::Node parsed;
    std::string error;
    if (!ParseYamlMapping(params, parsed, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }
    const YAML::Node& patch = parsed;

    if (RefuseWhileCapturing("render.settingsPatch", reply))
      return;

    Scene& scene = m_Registry->Get<Scene>();
    AssetManager& assets = m_Registry->Get<AssetManager>();
    Render& render = m_Registry->Get<Render>();

    const YAML::Node before = SceneSerializer::SerializeRenderSettings(scene, assets, render);

    // The serializer writes every key but an unset skybox, so any other key is one the loader would
    // skip without a word
    std::string unknown;
    for (auto it = patch.begin(); it != patch.end(); ++it)
    {
      const std::string& key = it->first.Scalar();
      if (key != "skybox" && !before[key])
        unknown += (unknown.empty() ? "" : ", ") + key;
    }

    if (!unknown.empty())
    {
      std::string known = "skybox";
      for (auto it = before.begin(); it != before.end(); ++it)
      {
        if (it->first.Scalar() != "skybox")
          known += ", " + it->first.Scalar();
      }
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "unknown render settings: " + unknown + "; known: " + known);
      return;
    }

    CubeMapHandle previousSkybox = scene.GetSkybox();
    const char* failureCode = nullptr;
    std::string failure;
    try
    {
      SceneSerializer::ApplyRenderSettings(patch, scene, assets, render);
      if (patch["skybox"] && !scene.GetSkybox())
      {
        failureCode = BridgeErrorCode::FAILED;
        failure = "cannot load the skybox '" + patch["skybox"].Scalar() + "'; see the editor log";
      }
    }
    catch (const YAML::Exception& e)
    {
      failureCode = BridgeErrorCode::INVALID_PARAMS;
      failure = std::string("cannot apply the settings patch: ") + e.what();
    }

    if (failureCode != nullptr)
    {
      // Applied key by key, so whatever came before the bad value is put back
      YAML::Node restore = YAML::Clone(before);
      restore.remove("skybox");
      SceneSerializer::ApplyRenderSettings(restore, scene, assets, render);
      scene.SetSkybox(previousSkybox);
      reply.Fail(failureCode, failure);
      return;
    }

    std::string keys;
    for (auto it = patch.begin(); it != patch.end(); ++it)
      keys += (keys.empty() ? "" : ", ") + it->first.Scalar();
    if (!keys.empty())
      YA_LOG_INFO("Bridge", "Patched render settings: %s", keys.c_str());

    Json result = Json::object();
    result["yaml"] = EmitYaml(SceneSerializer::SerializeRenderSettings(scene, assets, render));
    reply.Ok(std::move(result));
  }
}
