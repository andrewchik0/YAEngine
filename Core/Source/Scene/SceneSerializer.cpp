#include "SceneSerializer.h"
#include "Scene/TransformSystem.h"

#include "Scene.h"
#include "Components.h"
#include "ComponentRegistry.h"
#include "YamlUtils.h"
#include "Assets/AssetManager.h"
#include "Assets/CubeMapFile.h"
#include "Assets/IrradianceVolumeFile.h"
#include "Assets/ModelImporter.h"
#include "ModelOverrides.h"
#include "Render/Render.h"
#include "Render/ReflectionProbeAtlas.h"
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

  static void SerializeEntity(YAML::Node& entities, Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Entity entity);

  // Entities a user created inside a model subtree are regular entities: they carry no
  // ModelNodeComponent, so they are written out with a qualified parent reference
  static void SerializeModelUserEntities(YAML::Node& entities, Scene& scene, AssetManager& assets,
    const ComponentRegistry& registry, Entity entity)
  {
    Entity child = scene.GetHierarchy(entity).firstChild;
    while (child != entt::null)
    {
      Entity next = scene.GetHierarchy(child).nextSibling;

      if (scene.HasComponent<ModelNodeComponent>(child))
        SerializeModelUserEntities(entities, scene, assets, registry, child);
      else
        SerializeEntity(entities, scene, assets, registry, child);

      child = next;
    }
  }

  // Recursive walk: serialize entity and its children in hierarchy order
  static void SerializeEntity(YAML::Node& entities, Scene& scene, AssetManager& assets,
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

    // Model nodes are owned by the override layer of their model, even if one was
    // reparented out of the subtree - writing it here too would duplicate it
    if (scene.HasComponent<ModelNodeComponent>(entity)
      && scene.GetComponent<ModelNodeComponent>(entity).nodeIndex != 0)
    {
      return;
    }

    bool isModelRoot = scene.HasComponent<ModelSourceComponent>(entity);

    YAML::Node entityNode;
    entityNode["name"] = scene.GetName(entity);

    auto& hc = scene.GetHierarchy(entity);
    if (hc.parent != entt::null)
    {
#ifdef YA_EDITOR
      if (!scene.HasComponent<EditorOnlyTag>(hc.parent))
#endif
        entityNode["parent"] = ModelOverrides::MakeEntityReference(scene, assets, hc.parent);
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

    // Children of a model root come from the model file; only the delta is written
    if (isModelRoot)
    {
      auto overrides = ModelOverrides::Serialize(scene, assets, registry, entity);
      if (!overrides.IsNull())
        entityNode["modelOverrides"] = overrides;
    }

    entities.push_back(entityNode);

    if (isModelRoot)
    {
      SerializeModelUserEntities(entities, scene, assets, registry, entity);
      return;
    }

    Entity child = hc.firstChild;
    while (child != entt::null)
    {
      SerializeEntity(entities, scene, assets, registry, child);
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
    settings["ao"] = render.GetAOEnabled();
    settings["aoDenoise"] = render.GetAODenoiseEnabled();
    settings["aoQualityLevel"] = render.GetAOQualityLevel();
    settings["aoRadius"] = render.GetAORadius();
    settings["aoStrength"] = render.GetAOStrength();
    settings["aoSpecularStrength"] = render.GetAOSpecularStrength();
    settings["aoMultiBounce"] = render.GetAOMultiBounce();
    settings["aoRadiusMultiplier"] = render.GetAORadiusMultiplier();
    settings["aoFalloffRange"] = render.GetAOFalloffRange();
    settings["aoSampleDistributionPower"] = render.GetAOSampleDistributionPower();
    settings["aoThinOccluderCompensation"] = render.GetAOThinOccluderCompensation();
    settings["aoFinalValuePower"] = render.GetAOFinalValuePower();
    settings["aoDepthMipSamplingOffset"] = render.GetAODepthMipSamplingOffset();
    settings["ssr"] = render.GetSSREnabled();
    settings["ssrIntensity"] = render.GetSSRIntensity();
    settings["taa"] = render.GetTAAEnabled();
    settings["taaClampSigma"] = render.GetTAAClampSigma();
    settings["shadows"] = render.GetShadowsEnabled();
    settings["shadowIndirect"] = render.GetShadowIndirectEnabled();
    settings["shadowLod"] = render.GetShadowLodEnabled();
    {
      YAML::Node cascadeLods(YAML::NodeType::Sequence);
      cascadeLods.SetStyle(YAML::EmitterStyle::Flow);
      for (uint32_t cascade = 0; cascade < CSM_CASCADE_COUNT; cascade++)
        cascadeLods.push_back(render.GetShadowCascadeLods()[cascade]);
      settings["shadowCascadeLods"] = cascadeLods;
    }
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
    settings["probeBounces"] = render.GetProbeBounceCount();
    settings["volumeBounces"] = render.GetVolumeBounceCount();
    settings["irradianceVolumes"] = render.GetIrradianceVolumesEnabled();
    settings["irradianceNormalBias"] = render.GetIrradianceNormalBias();

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
      SerializeEntity(entities, scene, assets, registry, e);
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
    // "ssao" is the pre-GTAO key for the same on/off switch; the tuning keys next to it
    // described the old hemisphere kernel and have no GTAO equivalent, so they are dropped.
    if (settings["ssao"]) render.GetAOEnabled() = settings["ssao"].as<bool>();
    if (settings["ao"]) render.GetAOEnabled() = settings["ao"].as<bool>();
    if (settings["aoDenoise"]) render.GetAODenoiseEnabled() = settings["aoDenoise"].as<bool>();
    if (settings["aoQualityLevel"]) render.GetAOQualityLevel() = settings["aoQualityLevel"].as<int>();
    if (settings["aoRadius"]) render.GetAORadius() = settings["aoRadius"].as<float>();
    if (settings["aoStrength"]) render.GetAOStrength() = settings["aoStrength"].as<float>();
    if (settings["aoSpecularStrength"]) render.GetAOSpecularStrength() = settings["aoSpecularStrength"].as<float>();
    if (settings["aoMultiBounce"]) render.GetAOMultiBounce() = settings["aoMultiBounce"].as<float>();
    if (settings["aoRadiusMultiplier"]) render.GetAORadiusMultiplier() = settings["aoRadiusMultiplier"].as<float>();
    if (settings["aoFalloffRange"]) render.GetAOFalloffRange() = settings["aoFalloffRange"].as<float>();
    if (settings["aoSampleDistributionPower"]) render.GetAOSampleDistributionPower() = settings["aoSampleDistributionPower"].as<float>();
    if (settings["aoThinOccluderCompensation"]) render.GetAOThinOccluderCompensation() = settings["aoThinOccluderCompensation"].as<float>();
    if (settings["aoFinalValuePower"]) render.GetAOFinalValuePower() = settings["aoFinalValuePower"].as<float>();
    if (settings["aoDepthMipSamplingOffset"]) render.GetAODepthMipSamplingOffset() = settings["aoDepthMipSamplingOffset"].as<float>();
    if (settings["ssr"]) render.GetSSREnabled() = settings["ssr"].as<bool>();
    if (settings["ssrIntensity"]) render.GetSSRIntensity() = settings["ssrIntensity"].as<float>();
    if (settings["taa"]) render.GetTAAEnabled() = settings["taa"].as<bool>();
    if (settings["taaClampSigma"]) render.GetTAAClampSigma() = settings["taaClampSigma"].as<float>();
    if (settings["shadows"]) render.GetShadowsEnabled() = settings["shadows"].as<bool>();
    // Absent in scenes written before the indirect shadow path existed. Leaving the
    // member default (on) is deliberate: those scenes should pick up the new path.
    if (settings["shadowIndirect"]) render.GetShadowIndirectEnabled() = settings["shadowIndirect"].as<bool>();
    if (settings["shadowLod"]) render.GetShadowLodEnabled() = settings["shadowLod"].as<bool>();
    if (settings["shadowCascadeLods"])
    {
      const auto& cascadeLods = settings["shadowCascadeLods"];
      // A scene saved with a different cascade count must not shift the mapping, so
      // only the cascades the file actually describes are overwritten.
      for (uint32_t cascade = 0; cascade < CSM_CASCADE_COUNT && cascade < cascadeLods.size(); cascade++)
      {
        render.GetShadowCascadeLods()[cascade] = std::clamp(cascadeLods[cascade].as<int>(),
          0, int(MeshSimplifier::LOD_COUNT) - 1);
      }
    }
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
    if (settings["probeBounces"])
    {
      render.GetProbeBounceCount() = std::clamp(settings["probeBounces"].as<int>(),
        Render::MIN_PROBE_BOUNCES, Render::MAX_PROBE_BOUNCES);
    }
    if (settings["volumeBounces"])
    {
      render.GetVolumeBounceCount() = std::clamp(settings["volumeBounces"].as<int>(),
        Render::MIN_VOLUME_BOUNCES, Render::MAX_VOLUME_BOUNCES);
    }
    if (settings["irradianceVolumes"])
      render.GetIrradianceVolumesEnabled() = settings["irradianceVolumes"].as<bool>();
    if (settings["irradianceNormalBias"])
    {
      // Clamped to the range the UI offers - a negative bias pulls the diffuse
      // sample point INTO the geometry, which is the leak this exists to prevent.
      render.GetIrradianceNormalBias() = std::clamp(
        settings["irradianceNormalBias"].as<float>(), 0.0f, 1.0f);
    }
  }

  // Shared: load reflection probes (Pass 4)
  static void LoadReflectionProbes(Scene& scene, AssetManager& assets, Render& render)
  {
    auto& ctx = render.GetContext();
    uint32_t nextSlot = 1;
    auto probeView = scene.GetView<ReflectionProbeComponent>();
    for (auto entity : probeView)
    {
      auto& lp = scene.GetComponent<ReflectionProbeComponent>(entity);
      if (lp.bakedPrefilterPath.empty())
        continue;

      std::string pfAbsPath = assets.ResolvePath(lp.bakedPrefilterPath);

      CubeMapFileData pfData;
      if (!CubeMapFile::Load(pfAbsPath, pfData))
      {
        YA_LOG_WARN("Scene", "Failed to load probe prefilter: %s", pfAbsPath.c_str());
        continue;
      }

      if (nextSlot >= render.GetProbeAtlas().GetMaxSlots())
      {
        YA_LOG_WARN("Scene", "No atlas slots left for probe '%s'",
          scene.GetName(entity).c_str());
        continue;
      }
      if (!render.GetProbeAtlas().UploadPrefilterFromData(ctx, nextSlot, pfData))
        continue;

      uint32_t slot = nextSlot++;
      lp.atlasSlot = slot;
      lp.baked = true;

      YA_LOG_INFO("Scene", "Loaded probe '%s' -> atlas slot %u",
        scene.GetName(entity).c_str(), slot);
    }
  }

  // Shared: load irradiance volumes (Pass 5)
  void SceneSerializer::LoadIrradianceVolumes(Scene& scene, AssetManager& assets, Render& render)
  {
    std::vector<entt::entity> entities;
    std::vector<IrradianceVolumeFileData> volumes;

    auto volumeView = scene.GetView<IrradianceVolumeComponent>();
    for (auto entity : volumeView)
    {
      auto& volume = scene.GetComponent<IrradianceVolumeComponent>(entity);
      volume.baked = false;
      volume.atlasSlot = 0;

      if (volume.bakedVolumePath.empty())
        continue;

      std::string absPath = assets.ResolvePath(volume.bakedVolumePath);
      IrradianceVolumeFileData data;
      if (!IrradianceVolumeFile::Load(absPath, data))
      {
        YA_LOG_WARN("Scene", "Failed to load irradiance volume: %s", absPath.c_str());
        continue;
      }

      // The baked coefficients only describe the box they were captured in, so the
      // asset transform is authoritative. A moved or resized entity keeps rendering
      // with the old box until it is rebaked - warn instead of silently drifting.
      auto& wt = scene.GetWorldTransform(entity);
      glm::vec3 entityPos = glm::vec3(wt.world[3]);
      float scale = glm::max(glm::length(data.halfExtents), 1e-3f);
      bool positionMoved = glm::length(entityPos - data.position) > 0.01f * scale;
      bool sizeChanged = glm::length(volume.halfExtents - data.halfExtents) > 0.01f * scale;
      if (positionMoved || sizeChanged)
      {
        YA_LOG_WARN("Scene", "Irradiance volume '%s' no longer matches its baked box - rebake it",
          scene.GetName(entity).c_str());
      }

      entities.push_back(entity);
      volumes.push_back(std::move(data));
    }

    ApplyIrradianceVolumes(scene, render, entities, volumes);
  }

  void SceneSerializer::ApplyIrradianceVolumes(Scene& scene, Render& render,
    const std::vector<entt::entity>& entities,
    const std::vector<IrradianceVolumeFileData>& volumes)
  {
    // Public, so the precondition is checked rather than assumed: the loop below
    // walks entities and indexes slots, which Upload sizes from volumes.
    if (entities.size() != volumes.size())
    {
      YA_LOG_ERROR("Scene", "ApplyIrradianceVolumes got %zu entities for %zu volumes",
        entities.size(), volumes.size());
      return;
    }

    // Always called, even with nothing to upload - a previous scene may have left
    // volumes in the atlas and they have to go away with it.
    std::vector<uint32_t> slots;
    render.UploadIrradianceVolumes(volumes, slots);

    for (size_t i = 0; i < entities.size(); i++)
    {
      auto& volume = scene.GetComponent<IrradianceVolumeComponent>(entities[i]);
      if (slots[i] == IrradianceVolumeStorage::INVALID_SLOT)
      {
        // Cleared, not just skipped: Upload reassigns every slot, so a leftover
        // baked + atlasSlot would now point at somebody else's texels and the
        // next bake would drop the wrong volume from its capture.
        volume.baked = false;
        volume.atlasSlot = 0;
        YA_LOG_WARN("Scene", "Irradiance volume '%s' did not fit into the atlas and is inactive",
          scene.GetName(entities[i]).c_str());
        continue;
      }

      volume.atlasSlot = slots[i];
      volume.baked = true;

      YA_LOG_INFO("Scene", "Loaded irradiance volume '%s' -> slot %u",
        scene.GetName(entities[i]).c_str(), slots[i]);
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
    Entity rootEntity, Scene& scene, AssetManager& assets, const ComponentRegistry& registry,
    std::vector<std::pair<Entity, std::string>>& parentRequests)
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
      if (key == "name" || key == "parent" || key == "transform" || key == "model" || key == "modelOverrides")
        continue;
      if (!registry.Deserialize(key, scene.GetRegistry(), rootEntity, it->second))
        YA_LOG_WARN("Scene", "Unknown component '%s' on entity '%s'", key.c_str(), name.c_str());
    }

    if (!entityNode["modelOverrides"])
      return;

    auto nodeParents = ModelOverrides::Apply(entityNode["modelOverrides"], scene, assets, registry, rootEntity);
    parentRequests.insert(parentRequests.end(), nodeParents.begin(), nodeParents.end());
  }

  // Shared: pass 3, resolve parent references. A reference may address a node inside a
  // model subtree, which is not present in the name map.
  static void ResolveParentRequests(Scene& scene, AssetManager& assets,
    const std::unordered_map<std::string, Entity>& nameMap,
    const std::vector<std::pair<Entity, std::string>>& parentRequests)
  {
    for (auto& [entity, parentName] : parentRequests)
    {
      std::string rootName;
      std::string nodePath;

      if (ModelOverrides::SplitEntityReference(parentName, rootName, nodePath))
      {
        auto it = nameMap.find(rootName);
        Entity target = it != nameMap.end()
          ? ModelOverrides::ResolveNodeReference(scene, assets, it->second, nodePath)
          : Entity(entt::null);

        if (target != entt::null)
          scene.SetParent(entity, target);
        else
          YA_LOG_WARN("Scene", "Parent node '%s' not found", parentName.c_str());

        continue;
      }

      auto it = nameMap.find(parentName);
      if (it != nameMap.end())
        scene.SetParent(entity, it->second);
      else
        YA_LOG_WARN("Scene", "Parent '%s' not found", parentName.c_str());
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

    // Pass 5 compares the baked box of a volume against its entity transform, and
    // nothing has run TransformSystem yet at this point - every WorldTransform is
    // still the identity the entity was created with, so every volume off the world
    // origin would report as moved.
    TransformSystem transforms;
    transforms.Update(scene.GetRegistry(), 0.0);

    LoadReflectionProbes(scene, assets, render);
    LoadIrradianceVolumes(scene, assets, render);

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

        ApplyModelEntityOverrides(entityNode, name, rootEntity, scene, assets, registry, parentRequests);

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
    ResolveParentRequests(scene, assets, nameMap, parentRequests);
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

    auto scheduleTexture = [&](const ModelDescription& desc, const std::string& path, bool linear)
    {
      if (path.empty())
        return;

      // Embedded texture paths are synthetic keys - canonicalizing them would break the lookup
      const EmbeddedTexture* embedded = desc.FindEmbedded(path);
      auto canonical = embedded != nullptr
        ? path
        : std::filesystem::weakly_canonical(path).string();

      std::string key = canonical + (linear ? "|1" : "|0");
      if (textureKeyMap.contains(key))
        return;

      textureKeyMap[key] = textureTasks.size();
      TextureTask task;
      task.originalPath = path;
      task.canonicalPath = canonical;
      task.linear = linear;
      if (embedded != nullptr)
      {
        task.future = threadPool.Submit([embedded, linear]() {
          return TextureManager::DecodeEmbeddedToCpu(*embedded, linear);
        });
      }
      else
      {
        task.future = threadPool.Submit([path, linear]() {
          return TextureManager::DecodeToCpu(path, linear);
        });
      }
      textureTasks.push_back(std::move(task));
    };

    for (auto& desc : modelDescs)
    {
      for (auto& mat : desc.materials)
      {
        scheduleTexture(desc, mat.baseColorTexture, false);
        scheduleTexture(desc, mat.metallicTexture, true);
        scheduleTexture(desc, mat.roughnessTexture, true);
        scheduleTexture(desc, mat.specularTexture, true);
        scheduleTexture(desc, mat.emissiveTexture, false);
        scheduleTexture(desc, mat.normalTexture, true);
        scheduleTexture(desc, mat.heightTexture, true);
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

        ApplyModelEntityOverrides(entityNode, name, rootEntity, scene, assets, registry, parentRequests);

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
    ResolveParentRequests(scene, assets, nameMap, parentRequests);
  }
}
