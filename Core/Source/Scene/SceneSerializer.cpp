#include "SceneSerializer.h"

#include "Scene.h"
#include "Components.h"
#include "ComponentRegistry.h"
#include "YamlUtils.h"
#include "Assets/AssetManager.h"
#include "Assets/CubeMapFile.h"
#include "Render/Render.h"
#include "Render/LightProbeAtlas.h"
#include "Utils/Log.h"

#include <fstream>

namespace YAEngine
{

  static bool IsDefaultTransform(const LocalTransform& t)
  {
    return t.position == glm::vec3(0) &&
           t.rotation == glm::quat(1, 0, 0, 0) &&
           t.scale == glm::vec3(1);
  }

  // Recursive walk: serialize entity and its children in hierarchy order
  static void SerializeEntity(YAML::Node& entities, Scene& scene,
    const ComponentRegistry& registry, Entity entity)
  {
    // Skip editor-only entities
#ifdef YA_EDITOR
    if (scene.HasComponent<EditorOnlyTag>(entity))
      return;
#endif

    bool isModelRoot = scene.HasComponent<ModelSourceComponent>(entity);

    YAML::Node entityNode;
    entityNode["name"] = scene.GetName(entity);

    auto& hc = scene.GetHierarchy(entity);
    if (hc.parent != entt::null)
    {
#ifdef YA_EDITOR
      if (!scene.HasComponent<EditorOnlyTag>(hc.parent))
#endif
        entityNode["parent"] = scene.GetName(hc.parent);
    }

    auto& t = scene.GetTransform(entity);
    if (!IsDefaultTransform(t))
    {
      YAML::Node transform;
      if (t.position != glm::vec3(0))
        transform["position"] = SerializeVec3(t.position);
      if (t.rotation != glm::quat(1, 0, 0, 0))
        transform["rotation"] = SerializeQuat(t.rotation);
      if (t.scale != glm::vec3(1))
        transform["scale"] = SerializeVec3(t.scale);
      entityNode["transform"] = transform;
    }

    auto components = registry.SerializeAll(scene.GetRegistry(), entity);
    for (auto it = components.begin(); it != components.end(); ++it)
    {
      auto key = it->first.as<std::string>();
      if (it->second.IsNull() || (it->second.IsMap() && it->second.size() == 0))
        continue;
      entityNode[key] = it->second;
    }

    entities.push_back(entityNode);

    // If this is a model root, skip serializing children (they come from the model file)
    if (isModelRoot)
      return;

    Entity child = hc.firstChild;
    while (child != entt::null)
    {
      SerializeEntity(entities, scene, registry, child);
      child = scene.GetHierarchy(child).nextSibling;
    }
  }

  void SceneSerializer::Save(const std::string& path,
    Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Render& render,
    const std::string& basePath)
  {
    YAML::Node root;

    // Settings
    YAML::Node settings;
    auto skyboxPath = assets.CubeMaps().GetPath(scene.GetSkybox());
    if (!skyboxPath.empty())
      settings["skybox"] = assets.MakeRelative(skyboxPath);

    settings["gamma"] = render.GetGamma();
    settings["exposure"] = render.GetExposure();
    settings["tonemapMode"] = render.GetTonemapMode();
    settings["ssao"] = render.GetSSAOEnabled();
    settings["ssr"] = render.GetSSREnabled();
    settings["taa"] = render.GetTAAEnabled();
    settings["shadows"] = render.GetShadowsEnabled();
    settings["autoExposure"] = render.GetAutoExposureEnabled();
    settings["adaptSpeedUp"] = render.GetAdaptSpeedUp();
    settings["adaptSpeedDown"] = render.GetAdaptSpeedDown();
    settings["lowPercentile"] = render.GetLowPercentile();
    settings["highPercentile"] = render.GetHighPercentile();
    settings["bloom"] = render.GetBloomEnabled();
    settings["bloomIntensity"] = render.GetBloomIntensity();
    settings["bloomThreshold"] = render.GetBloomThreshold();
    settings["bloomSoftKnee"] = render.GetBloomSoftKnee();

    root["settings"] = settings;

    // Entities - walk root entities first
    YAML::Node entities;
    auto view = scene.GetView<RootTag>();
    for (auto e : view)
    {
#ifdef YA_EDITOR
      if (scene.HasComponent<EditorOnlyTag>(e))
        continue;
#endif
      SerializeEntity(entities, scene, registry, e);
    }
    root["entities"] = entities;

    std::ofstream fout(path);
    if (!fout.is_open())
    {
      YA_LOG_ERROR("Scene", "Failed to save scene: %s", path.c_str());
      return;
    }
    fout << root;
    fout.close();

    YA_LOG_INFO("Scene", "Scene saved: %s", path.c_str());
  }

  void SceneSerializer::Load(const std::string& path,
    Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Render& render,
    const std::string& basePath)
  {
    YAML::Node root;
    try
    {
      root = YAML::LoadFile(path);
    }
    catch (const YAML::Exception& e)
    {
      YA_LOG_ERROR("Scene", "Failed to load scene '%s': %s", path.c_str(), e.what());
      return;
    }

    assets.SetBasePath(basePath);

    // Pass 1: Settings
    if (root["settings"])
    {
      auto settings = root["settings"];
      if (settings["skybox"])
        scene.SetSkybox(assets.CubeMaps().Load(assets.ResolvePath(settings["skybox"].as<std::string>())));
      if (settings["gamma"]) render.GetGamma() = settings["gamma"].as<float>();
      if (settings["exposure"]) render.GetExposure() = settings["exposure"].as<float>();
      if (settings["tonemapMode"]) render.GetTonemapMode() = settings["tonemapMode"].as<int>();
      if (settings["ssao"]) render.GetSSAOEnabled() = settings["ssao"].as<bool>();
      if (settings["ssr"]) render.GetSSREnabled() = settings["ssr"].as<bool>();
      if (settings["taa"]) render.GetTAAEnabled() = settings["taa"].as<bool>();
      if (settings["shadows"]) render.GetShadowsEnabled() = settings["shadows"].as<bool>();
      if (settings["autoExposure"]) render.GetAutoExposureEnabled() = settings["autoExposure"].as<bool>();
      if (settings["adaptSpeedUp"]) render.GetAdaptSpeedUp() = settings["adaptSpeedUp"].as<float>();
      if (settings["adaptSpeedDown"]) render.GetAdaptSpeedDown() = settings["adaptSpeedDown"].as<float>();
      if (settings["lowPercentile"]) render.GetLowPercentile() = settings["lowPercentile"].as<float>();
      if (settings["highPercentile"]) render.GetHighPercentile() = settings["highPercentile"].as<float>();
      if (settings["bloom"]) render.GetBloomEnabled() = settings["bloom"].as<bool>();
      if (settings["bloomIntensity"]) render.GetBloomIntensity() = settings["bloomIntensity"].as<float>();
      if (settings["bloomThreshold"]) render.GetBloomThreshold() = settings["bloomThreshold"].as<float>();
      if (settings["bloomSoftKnee"]) render.GetBloomSoftKnee() = settings["bloomSoftKnee"].as<float>();
    }

    if (!root["entities"])
      return;

    auto entities = root["entities"];

    // Pass 2: Create entities with transforms
    std::unordered_map<std::string, Entity> nameMap;
    std::vector<std::pair<Entity, std::string>> parentRequests;

    for (size_t i = 0; i < entities.size(); i++)
    {
      auto entityNode = entities[i];
      auto name = entityNode["name"].as<std::string>();

      // Model entities are handled separately - ModelManager creates the hierarchy
      if (entityNode["model"])
      {
        auto modelNode = entityNode["model"];
        auto modelPath = assets.ResolvePath(modelNode["path"].as<std::string>());
        bool combined = modelNode["combinedTextures"] ? modelNode["combinedTextures"].as<bool>() : false;

        auto modelHandle = assets.Models().Load(modelPath, combined);
        if (!modelHandle)
        {
          YA_LOG_WARN("Scene", "Failed to load model: %s", modelPath.c_str());
          continue;
        }

        auto& model = assets.Models().Get(modelHandle);
        Entity rootEntity = model.rootEntity;

        // Apply transform override
        if (entityNode["transform"])
        {
          auto& t = scene.GetTransform(rootEntity);
          auto transform = entityNode["transform"];
          if (transform["position"]) t.position = DeserializeVec3(transform["position"]);
          if (transform["rotation"]) t.rotation = DeserializeQuat(transform["rotation"]);
          if (transform["scale"]) t.scale = DeserializeVec3(transform["scale"]);
          scene.MarkDirty(rootEntity);
        }

        if (nameMap.contains(name))
          YA_LOG_WARN("Scene", "Duplicate entity name '%s' - parent references may be incorrect", name.c_str());
        nameMap[name] = rootEntity;

        if (entityNode["parent"])
          parentRequests.push_back({ rootEntity, entityNode["parent"].as<std::string>() });

        // Deserialize any extra components (beyond model and transform)
        for (auto it = entityNode.begin(); it != entityNode.end(); ++it)
        {
          auto key = it->first.as<std::string>();
          if (key == "name" || key == "parent" || key == "transform" || key == "model")
            continue;
          if (!registry.Deserialize(key, scene.GetRegistry(), rootEntity, it->second))
            YA_LOG_WARN("Scene", "Unknown component '%s' on entity '%s'", key.c_str(), name.c_str());
        }

        continue;
      }

      if (nameMap.contains(name))
        YA_LOG_WARN("Scene", "Duplicate entity name '%s' - parent references may be incorrect", name.c_str());

      Entity entity = scene.CreateEntity(name);
      nameMap[name] = entity;

      if (entityNode["transform"])
      {
        auto& t = scene.GetTransform(entity);
        auto transform = entityNode["transform"];
        if (transform["position"]) t.position = DeserializeVec3(transform["position"]);
        if (transform["rotation"]) t.rotation = DeserializeQuat(transform["rotation"]);
        if (transform["scale"]) t.scale = DeserializeVec3(transform["scale"]);
        scene.MarkDirty(entity);
      }

      if (entityNode["parent"])
        parentRequests.push_back({ entity, entityNode["parent"].as<std::string>() });

      // Deserialize components
      for (auto it = entityNode.begin(); it != entityNode.end(); ++it)
      {
        auto key = it->first.as<std::string>();
        if (key == "name" || key == "parent" || key == "transform")
          continue;
        if (!registry.Deserialize(key, scene.GetRegistry(), entity, it->second))
          YA_LOG_WARN("Scene", "Unknown component '%s' on entity '%s'", key.c_str(), name.c_str());
      }
    }

    // Pass 3: Establish hierarchy
    for (auto& [entity, parentName] : parentRequests)
    {
      auto it = nameMap.find(parentName);
      if (it != nameMap.end())
        scene.SetParent(entity, it->second);
      else
        YA_LOG_WARN("Scene", "Parent '%s' not found", parentName.c_str());
    }

    // Pass 4: Load baked light probe data from .yacm files
    {
      auto& ctx = render.GetContext();
      uint32_t nextSlot = 1;
      auto probeView = scene.GetView<LightProbeComponent>();
      for (auto entity : probeView)
      {
        auto& lp = scene.GetComponent<LightProbeComponent>(entity);
        if (lp.bakedIrradiancePath.empty() || lp.bakedPrefilterPath.empty())
          continue;

        std::string irrAbsPath = assets.ResolvePath(lp.bakedIrradiancePath);
        std::string pfAbsPath = assets.ResolvePath(lp.bakedPrefilterPath);

        CubeMapFileData irrData, pfData;
        if (!CubeMapFile::Load(irrAbsPath, irrData))
        {
          YA_LOG_WARN("Scene", "Failed to load probe irradiance: %s", irrAbsPath.c_str());
          continue;
        }
        if (!CubeMapFile::Load(pfAbsPath, pfData))
        {
          YA_LOG_WARN("Scene", "Failed to load probe prefilter: %s", pfAbsPath.c_str());
          continue;
        }

        uint32_t slot = nextSlot++;
        if (slot >= render.GetProbeAtlas().GetMaxSlots())
        {
          YA_LOG_WARN("Scene", "No atlas slots left for probe '%s'",
            scene.GetName(entity).c_str());
          break;
        }

        render.GetProbeAtlas().UploadSlotFromData(ctx, slot,
          irrData.pixels.data(), irrData.width,
          pfData.pixels.data(), pfData.width, pfData.mipLevels,
          pfData.bytesPerPixel);

        lp.atlasSlot = slot;
        lp.baked = true;

        YA_LOG_INFO("Scene", "Loaded probe '%s' -> atlas slot %u",
          scene.GetName(entity).c_str(), slot);
      }
    }

    YA_LOG_INFO("Scene", "Scene loaded: %s (%zu entities)", path.c_str(), nameMap.size());
  }
}
