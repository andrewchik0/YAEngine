#include "SceneSerializer.h"

#include "Scene.h"
#include "Components.h"
#include "ComponentRegistry.h"
#include "YamlUtils.h"
#include "Assets/AssetManager.h"
#include "Assets/CubeMapFile.h"
#include "Assets/ModelImporter.h"
#include "Render/Render.h"
#include "Render/LightProbeAtlas.h"
#include "Utils/Log.h"
#include "Utils/ThreadPool.h"

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
    // Skip runtime-generated entities
    if (scene.HasComponent<ScatterInstanceTag>(entity))
      return;
    if (scene.HasComponent<NoSerializeTag>(entity))
      return;
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
    settings["ssaoIntensity"] = render.GetSSAOIntensity();
    settings["ssaoRadius"] = render.GetSSAORadius();
    settings["ssaoBias"] = render.GetSSAOBias();
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
    settings["fog"] = render.GetFogEnabled();
    settings["fogDensity"] = render.GetFogDensity();
    settings["fogHeightFalloff"] = render.GetFogHeightFalloff();
    settings["fogColor"] = SerializeVec3(render.GetFogColor());
    settings["fogMaxOpacity"] = render.GetFogMaxOpacity();
    settings["fogStartDistance"] = render.GetFogStartDistance();

    root["settings"] = settings;

#ifdef YA_EDITOR
    {
      auto& camState = scene.GetEditorCameraState();
      YAML::Node editorCam;
      editorCam["position"] = SerializeVec3(camState.position);
      editorCam["yaw"] = camState.yaw;
      editorCam["pitch"] = camState.pitch;
      root["editorCamera"] = editorCam;
    }
#endif

    // Entities - walk root entities first
    YAML::Node entities;
    auto view = scene.GetView<RootTag>();
    for (auto e : view)
    {
      if (scene.HasComponent<NoSerializeTag>(e))
        continue;
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

  // Shared: load settings (Pass 1)
  static void LoadSettings(const YAML::Node& root, Scene& scene, AssetManager& assets, Render& render)
  {
    if (root["editorCamera"])
    {
      auto ec = root["editorCamera"];
      auto& camState = scene.GetEditorCameraState();
      if (ec["position"]) camState.position = DeserializeVec3(ec["position"]);
      if (ec["yaw"]) camState.yaw = ec["yaw"].as<float>();
      if (ec["pitch"]) camState.pitch = ec["pitch"].as<float>();
    }

    if (!root["settings"])
      return;

    auto settings = root["settings"];
    if (settings["skybox"])
      scene.SetSkybox(assets.CubeMaps().Load(assets.ResolvePath(settings["skybox"].as<std::string>())));
    if (settings["gamma"]) render.GetGamma() = settings["gamma"].as<float>();
    if (settings["exposure"]) render.GetExposure() = settings["exposure"].as<float>();
    if (settings["tonemapMode"]) render.GetTonemapMode() = settings["tonemapMode"].as<int>();
    if (settings["ssao"]) render.GetSSAOEnabled() = settings["ssao"].as<bool>();
    if (settings["ssaoIntensity"]) render.GetSSAOIntensity() = settings["ssaoIntensity"].as<float>();
    if (settings["ssaoRadius"]) render.GetSSAORadius() = settings["ssaoRadius"].as<float>();
    if (settings["ssaoBias"]) render.GetSSAOBias() = settings["ssaoBias"].as<float>();
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
    if (settings["fog"]) render.GetFogEnabled() = settings["fog"].as<bool>();
    if (settings["fogDensity"]) render.GetFogDensity() = settings["fogDensity"].as<float>();
    if (settings["fogHeightFalloff"]) render.GetFogHeightFalloff() = settings["fogHeightFalloff"].as<float>();
    if (settings["fogColor"]) render.GetFogColor() = DeserializeVec3(settings["fogColor"]);
    if (settings["fogMaxOpacity"]) render.GetFogMaxOpacity() = settings["fogMaxOpacity"].as<float>();
    if (settings["fogStartDistance"]) render.GetFogStartDistance() = settings["fogStartDistance"].as<float>();
  }

  // Shared: load light probes (Pass 4)
  static void LoadLightProbes(Scene& scene, AssetManager& assets, Render& render)
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

  // Shared: create entity and apply transform + components
  static Entity CreateAndDeserializeEntity(const YAML::Node& entityNode, const std::string& name,
    Scene& scene, const ComponentRegistry& registry)
  {
    Entity entity = scene.CreateEntity(name);

    if (entityNode["transform"])
    {
      auto& t = scene.GetTransform(entity);
      auto transform = entityNode["transform"];
      if (transform["position"]) t.position = DeserializeVec3(transform["position"]);
      if (transform["rotation"]) t.rotation = DeserializeQuat(transform["rotation"]);
      if (transform["scale"]) t.scale = DeserializeVec3(transform["scale"]);
      scene.MarkDirty(entity);
    }

    for (auto it = entityNode.begin(); it != entityNode.end(); ++it)
    {
      auto key = it->first.as<std::string>();
      if (key == "name" || key == "parent" || key == "transform")
        continue;
      if (!registry.Deserialize(key, scene.GetRegistry(), entity, it->second))
        YA_LOG_WARN("Scene", "Unknown component '%s' on entity '%s'", key.c_str(), name.c_str());
    }

    return entity;
  }

  // Shared: apply model entity from YAML node (transform override + extra components)
  static void ApplyModelEntityOverrides(const YAML::Node& entityNode, const std::string& name,
    Entity rootEntity, Scene& scene, const ComponentRegistry& registry)
  {
    if (entityNode["transform"])
    {
      auto& t = scene.GetTransform(rootEntity);
      auto transform = entityNode["transform"];
      if (transform["position"]) t.position = DeserializeVec3(transform["position"]);
      if (transform["rotation"]) t.rotation = DeserializeQuat(transform["rotation"]);
      if (transform["scale"]) t.scale = DeserializeVec3(transform["scale"]);
      scene.MarkDirty(rootEntity);
    }

    for (auto it = entityNode.begin(); it != entityNode.end(); ++it)
    {
      auto key = it->first.as<std::string>();
      if (key == "name" || key == "parent" || key == "transform" || key == "model")
        continue;
      if (!registry.Deserialize(key, scene.GetRegistry(), rootEntity, it->second))
        YA_LOG_WARN("Scene", "Unknown component '%s' on entity '%s'", key.c_str(), name.c_str());
    }
  }

  void SceneSerializer::Load(const std::string& path,
    Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Render& render,
    const std::string& basePath,
    ThreadPool* threadPool)
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

    LoadSettings(root, scene, assets, render);

    if (!root["entities"])
    {
      scene.SetScenePath(path);
      return;
    }

    auto entities = root["entities"];

    if (threadPool)
      LoadParallel(root, entities, scene, assets, registry, render, *threadPool);
    else
      LoadSync(root, entities, scene, assets, registry, render);

    LoadLightProbes(scene, assets, render);

    scene.SetScenePath(path);
    YA_LOG_INFO("Scene", "Scene loaded: %s", path.c_str());
  }

  void SceneSerializer::LoadSync(const YAML::Node& root, const YAML::Node& entities,
    Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Render& render)
  {
    std::unordered_map<std::string, Entity> nameMap;
    std::vector<std::pair<Entity, std::string>> parentRequests;

    for (size_t i = 0; i < entities.size(); i++)
    {
      auto entityNode = entities[i];
      auto name = entityNode["name"].as<std::string>();

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

        ApplyModelEntityOverrides(entityNode, name, rootEntity, scene, registry);

        if (nameMap.contains(name))
          YA_LOG_WARN("Scene", "Duplicate entity name '%s' - parent references may be incorrect", name.c_str());
        nameMap[name] = rootEntity;

        if (entityNode["parent"])
          parentRequests.push_back({ rootEntity, entityNode["parent"].as<std::string>() });

        continue;
      }

      if (nameMap.contains(name))
        YA_LOG_WARN("Scene", "Duplicate entity name '%s' - parent references may be incorrect", name.c_str());

      Entity entity = CreateAndDeserializeEntity(entityNode, name, scene, registry);
      nameMap[name] = entity;

      if (entityNode["parent"])
        parentRequests.push_back({ entity, entityNode["parent"].as<std::string>() });
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
  }

  void SceneSerializer::LoadParallel(const YAML::Node& root, const YAML::Node& entities,
    Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Render& render,
    ThreadPool& threadPool)
  {
    // Phase 1: Submit model imports to thread pool
    struct ModelTask
    {
      size_t entityIndex;
      std::string modelPath;
      bool combined;
      std::future<ModelDescription> future;
    };
    std::vector<ModelTask> modelTasks;

    for (size_t i = 0; i < entities.size(); i++)
    {
      auto entityNode = entities[i];
      if (!entityNode["model"])
        continue;

      auto modelNode = entityNode["model"];
      auto modelPath = assets.ResolvePath(modelNode["path"].as<std::string>());
      bool combined = modelNode["combinedTextures"] ? modelNode["combinedTextures"].as<bool>() : false;

      ModelTask task;
      task.entityIndex = i;
      task.modelPath = modelPath;
      task.combined = combined;
      task.future = threadPool.Submit([path = modelPath, combined]() {
        return ModelImporter::Import(path, combined);
      });
      modelTasks.push_back(std::move(task));
    }

    // Wait for all imports
    std::vector<ModelDescription> modelDescs(modelTasks.size());
    for (size_t i = 0; i < modelTasks.size(); i++)
      modelDescs[i] = modelTasks[i].future.get();

    YA_LOG_INFO("Scene", "Parallel import: %zu models imported", modelDescs.size());

    // Phase 2: Collect unique texture paths and decode in parallel
    struct TextureTask
    {
      std::string originalPath;
      std::string canonicalPath;
      bool linear;
      std::future<CpuTextureData> future;
    };

    std::unordered_map<std::string, size_t> textureKeyMap;
    std::vector<TextureTask> textureTasks;

    auto scheduleTexture = [&](const std::string& path, bool linear)
    {
      if (path.empty())
        return;
      auto canonical = std::filesystem::weakly_canonical(path).string();
      std::string key = canonical + (linear ? "|1" : "|0");
      if (textureKeyMap.contains(key))
        return;

      textureKeyMap[key] = textureTasks.size();
      TextureTask task;
      task.originalPath = path;
      task.canonicalPath = canonical;
      task.linear = linear;
      task.future = threadPool.Submit([path, linear]() {
        return TextureManager::DecodeToCpu(path, linear);
      });
      textureTasks.push_back(std::move(task));
    };

    for (auto& desc : modelDescs)
    {
      for (auto& mat : desc.materials)
      {
        scheduleTexture(mat.baseColorTexture, false);
        scheduleTexture(mat.metallicTexture, true);
        scheduleTexture(mat.roughnessTexture, true);
        scheduleTexture(mat.specularTexture, true);
        scheduleTexture(mat.emissiveTexture, false);
        scheduleTexture(mat.normalTexture, true);
        scheduleTexture(mat.heightTexture, true);
      }
    }

    // Wait for all texture decodes and register in cache
    for (auto& task : textureTasks)
    {
      auto cpuData = task.future.get();
      if (cpuData.width == 0)
        continue;
      (void)assets.Textures().LoadFromCpuData(std::move(cpuData), nullptr, task.canonicalPath);
    }

    YA_LOG_INFO("Scene", "Parallel decode: %zu textures decoded", textureTasks.size());

    // Phase 3: Build all entities on main thread
    // Textures are now cached - ModelBuilder::Build() will hit cache for all texture loads
    std::unordered_map<std::string, Entity> nameMap;
    std::vector<std::pair<Entity, std::string>> parentRequests;
    size_t modelIdx = 0;

    for (size_t i = 0; i < entities.size(); i++)
    {
      auto entityNode = entities[i];
      auto name = entityNode["name"].as<std::string>();

      if (entityNode["model"])
      {
        auto& task = modelTasks[modelIdx];
        auto& desc = modelDescs[modelIdx];
        modelIdx++;

        if (desc.root.children.empty())
        {
          YA_LOG_WARN("Scene", "Failed to load model: %s", task.modelPath.c_str());
          continue;
        }

        auto modelHandle = assets.Models().LoadFromDescription(
          std::move(desc), task.modelPath, task.combined);

        if (!modelHandle)
          continue;

        auto& model = assets.Models().Get(modelHandle);
        Entity rootEntity = model.rootEntity;

        ApplyModelEntityOverrides(entityNode, name, rootEntity, scene, registry);

        if (nameMap.contains(name))
          YA_LOG_WARN("Scene", "Duplicate entity name '%s' - parent references may be incorrect", name.c_str());
        nameMap[name] = rootEntity;

        if (entityNode["parent"])
          parentRequests.push_back({ rootEntity, entityNode["parent"].as<std::string>() });

        continue;
      }

      if (nameMap.contains(name))
        YA_LOG_WARN("Scene", "Duplicate entity name '%s' - parent references may be incorrect", name.c_str());

      Entity entity = CreateAndDeserializeEntity(entityNode, name, scene, registry);
      nameMap[name] = entity;

      if (entityNode["parent"])
        parentRequests.push_back({ entity, entityNode["parent"].as<std::string>() });
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
  }
}
