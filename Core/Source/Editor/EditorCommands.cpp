#include "Editor/EditorCommands.h"

#include "Assets/AssetManager.h"
#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Render/Render.h"
#include "Scene/SequencePlayer.h"
#include "Scene/Components.h"
#include "Utils/Log.h"

namespace YAEngine::EditorCommands
{
  namespace
  {
    void AddTerrain(Scene& scene, AssetManager& assets, Entity entity)
    {
      scene.AddComponent<TerrainComponent>(entity);
      if (!scene.HasComponent<MaterialComponent>(entity))
        scene.AddComponent<MaterialComponent>(entity, assets.FindOrCreateDefaultMaterial());
      scene.GetRegistry().emplace_or_replace<TerrainDirty>(entity);
    }

    void AddRoad(Scene& scene, AssetManager& assets, Entity entity)
    {
      scene.AddComponent<RoadComponent>(entity);
      if (!scene.HasComponent<MaterialComponent>(entity))
        scene.AddComponent<MaterialComponent>(entity, assets.FindOrCreateDefaultMaterial());
      scene.GetRegistry().emplace_or_replace<RoadDirty>(entity);
    }

    const char* LightUnavailable(Scene& scene, Entity entity)
    {
      return scene.HasComponent<LightComponent>(entity) ? "The entity already has a Light; its Type is set in Details" : nullptr;
    }

    // Terrain and Road replace the entity's mesh with the one they generate and destroy the old one,
    // which may be shared with other entities
    const char* GeneratedMeshUnavailable(Scene& scene, Entity entity)
    {
      if (scene.HasComponent<TerrainComponent>(entity))
        return "The entity's Terrain already generates its mesh";
      if (scene.HasComponent<RoadComponent>(entity))
        return "The entity's Road already generates its mesh";
      if (scene.HasComponent<MeshComponent>(entity))
        return "The entity already renders a mesh, which the generated one would replace";
      return nullptr;
    }

    const AddableComponent ADDABLE_COMPONENTS[] = {
      {
        .label = "Point Light", .icon = ICON_LC_LIGHTBULB, .tooltip = "Shines in every direction from the entity",
        .unavailableReason = &LightUnavailable,
        .add = [](Scene& scene, AssetManager&, Entity entity) { scene.AddComponent<LightComponent>(entity, LightType::Point); },
      },
      {
        .label = "Spot Light", .icon = ICON_LC_LIGHTBULB, .tooltip = "Shines a cone along the entity's rotation",
        .unavailableReason = &LightUnavailable,
        .add = [](Scene& scene, AssetManager&, Entity entity) { scene.AddComponent<LightComponent>(entity, LightType::Spot); },
      },
      {
        .label = "Directional Light", .icon = ICON_LC_SUN, .tooltip = "Parallel light along the entity's rotation, like the sun",
        .unavailableReason = &LightUnavailable,
        .add = [](Scene& scene, AssetManager&, Entity entity) { scene.AddComponent<LightComponent>(entity, LightType::Directional); },
      },
      {
        .label = "Camera", .icon = ICON_LC_VIDEO, .tooltip = "Scene camera the viewport can look through and a Camera Track can drive",
        .separatorBefore = true,
        .unavailableReason = [](Scene& scene, Entity entity) -> const char* {
          return scene.HasComponent<CameraComponent>(entity) ? "The entity already has a Camera" : nullptr;
        },
        .add = [](Scene& scene, AssetManager&, Entity entity) { scene.AddComponent<CameraComponent>(entity); },
      },
      {
        .label = "Motion Path", .icon = ICON_LC_SPLINE,
        .tooltip = "Timed drive along a curve, played on the Sequencer timeline; its points and speed keys are edited there",
        .unavailableReason = [](Scene& scene, Entity entity) -> const char* {
          if (scene.HasComponent<MotionPathComponent>(entity))
            return "The entity already has a Motion Path";
          if (scene.HasComponent<CameraTrackComponent>(entity))
            return "A Camera Track already drives this entity";
          return nullptr;
        },
        .add = [](Scene& scene, AssetManager&, Entity entity) { scene.AddComponent<MotionPathComponent>(entity); },
      },
      {
        .label = "Reflection Probe", .icon = ICON_LC_GLOBE, .tooltip = "Baked specular reflections for the surfaces inside its volume",
        .separatorBefore = true,
        .unavailableReason = [](Scene& scene, Entity entity) -> const char* {
          return scene.HasComponent<ReflectionProbeComponent>(entity) ? "The entity already has a Reflection Probe" : nullptr;
        },
        .add = [](Scene& scene, AssetManager&, Entity entity) { scene.AddComponent<ReflectionProbeComponent>(entity); },
      },
      {
        .label = "Irradiance Volume", .icon = ICON_LC_BOXES, .tooltip = "Baked diffuse lighting for the space inside its box",
        .unavailableReason = [](Scene& scene, Entity entity) -> const char* {
          return scene.HasComponent<IrradianceVolumeComponent>(entity) ? "The entity already has an Irradiance Volume" : nullptr;
        },
        .add = [](Scene& scene, AssetManager&, Entity entity) { scene.AddComponent<IrradianceVolumeComponent>(entity); },
      },
      {
        .label = "Terrain", .icon = ICON_LC_MOUNTAIN,
        .tooltip = "Generated heightfield mesh; an entity without a material gets the default one",
        .separatorBefore = true,
        .unavailableReason = &GeneratedMeshUnavailable,
        .add = &AddTerrain,
      },
      {
        .label = "Road", .icon = ICON_LC_ROUTE,
        .tooltip = "Generated road mesh along a spline; an entity without a material gets the default one",
        .unavailableReason = &GeneratedMeshUnavailable,
        .add = &AddRoad,
      },
      {
        .label = "Scatter", .icon = ICON_LC_SPROUT, .tooltip = "Instances scattered over the terrain on this entity or its parent",
        .unavailableReason = [](Scene& scene, Entity entity) -> const char* {
          if (scene.HasComponent<ScatterComponent>(entity))
            return "The entity already has a Scatter";
          // Satellite scatters find their source by name, and the scatter update reads it unguarded
          if (!scene.HasComponent<Name>(entity))
            return "A scatter entity needs a name: rename the entity first";
          return nullptr;
        },
        .add = [](Scene& scene, AssetManager&, Entity entity) {
          scene.AddComponent<ScatterComponent>(entity);
          scene.GetRegistry().emplace_or_replace<ScatterDirty>(entity);
        },
      },
      {
        .label = "Collider", .icon = ICON_LC_BOX, .tooltip = "Box collider for physics and collision queries",
        .separatorBefore = true,
        .unavailableReason = [](Scene& scene, Entity entity) -> const char* {
          const ModelSourceComponent* model = scene.GetRegistry().try_get<ModelSourceComponent>(entity);
          if (model != nullptr && model->colliderEnabled)
            return "The Model section's collider builds this entity's Collider";
          return scene.HasComponent<ColliderComponent>(entity) ? "The entity already has a Collider" : nullptr;
        },
        .add = [](Scene& scene, AssetManager&, Entity entity) { scene.AddComponent<ColliderComponent>(entity); },
      },
    };
  }

  std::span<const AddableComponent> GetAddableComponents()
  {
    return ADDABLE_COMPONENTS;
  }

  Entity CreateEntity(Scene& scene, AssetManager& assets, NewEntityKind kind)
  {
    switch (kind)
    {
      case NewEntityKind::Empty:
        return scene.CreateEntity("Entity");

      case NewEntityKind::PointLight:
      {
        Entity e = scene.CreateEntity("PointLight");
        scene.AddComponent<LightComponent>(e, LightType::Point);
        return e;
      }

      case NewEntityKind::SpotLight:
      {
        Entity e = scene.CreateEntity("SpotLight");
        scene.AddComponent<LightComponent>(e, LightType::Spot);
        return e;
      }

      case NewEntityKind::DirectionalLight:
      {
        Entity e = scene.CreateEntity("DirectionalLight");
        scene.AddComponent<LightComponent>(e, LightType::Directional);
        return e;
      }

      case NewEntityKind::Terrain:
      {
        Entity e = scene.CreateEntity("Terrain");
        AddTerrain(scene, assets, e);
        return e;
      }

      case NewEntityKind::Camera:
      {
        Entity e = scene.CreateEntity("Camera");
        scene.AddComponent<CameraComponent>(e);
        return e;
      }

      case NewEntityKind::ReflectionProbe:
      {
        Entity e = scene.CreateEntity("ReflectionProbe");
        scene.AddComponent<ReflectionProbeComponent>(e);
        return e;
      }

      case NewEntityKind::IrradianceVolume:
      {
        Entity e = scene.CreateEntity("IrradianceVolume");
        scene.AddComponent<IrradianceVolumeComponent>(e);
        return e;
      }
    }

    return entt::null;
  }

  const char* GetPrimitiveName(PrimitiveType type)
  {
    switch (type)
    {
      case PrimitiveType::Box: return "Cube";
      case PrimitiveType::Sphere: return "Sphere";
      case PrimitiveType::Plane: return "Plane";
    }
    return "Primitive";
  }

  Entity CreatePrimitive(Scene& scene, AssetManager& assets, PrimitiveType type)
  {
    Entity e = scene.CreateEntity(GetPrimitiveName(type));
    scene.AddComponent<MeshComponent>(e, assets.Primitives().Create(type));
    scene.AddComponent<MaterialComponent>(e, assets.FindOrCreateDefaultMaterial());
    return e;
  }

  Entity DuplicateModel(Scene& scene, AssetManager& assets, Entity modelRoot)
  {
    auto& source = scene.GetComponent<ModelSourceComponent>(modelRoot);
    std::string path = source.path;
    bool combined = source.combinedTextures;

    Entity parent = scene.GetHierarchy(modelRoot).parent;
    LocalTransform transform = scene.GetTransform(modelRoot);
    Name name = scene.MakeUniqueEntityName(scene.GetName(modelRoot) + " (Copy)");

    auto handle = assets.Models().Load(path, combined);
    if (!handle)
    {
      YA_LOG_WARN("Assets", "Duplicate failed: model '%s' could not be loaded", path.c_str());
      return entt::null;
    }

    Entity copy = assets.Models().Get(handle).rootEntity;
    scene.SetName(copy, name);
    scene.GetTransform(copy) = transform;

    if (parent != entt::null)
      scene.SetParent(copy, parent);

    scene.MarkDirty(copy);
    return copy;
  }

  void DeleteEntity(EditorContext& context, Entity entity)
  {
    Scene& scene = *context.scene;
    // A selected descendant goes with the subtree too; the Details panel and the gizmo would
    // otherwise read a destroyed entity
    for (Entity e = context.selectedEntity; e != entt::null && scene.GetRegistry().valid(e);
         e = scene.GetHierarchy(e).parent)
    {
      if (e == entity)
      {
        context.ClearSelection();
        break;
      }
    }

    scene.DestroyEntity(entity);
  }

  bool RenameEntity(Scene& scene, Entity entity, std::string_view name)
  {
    if (name.empty())
      return false;

    scene.SetName(entity, name);
    return true;
  }

  const char* GetAddComponentUnavailableReason(const AddableComponent& component, Scene& scene, Entity entity)
  {
    if (scene.HasComponent<ScatterInstanceTag>(entity))
    {
      return "Runtime scatter output: its scatter parent regenerates it and it is never saved, so an added component "
             "would be lost. Add components to the scatter parent instead.";
    }
    return component.unavailableReason(scene, entity);
  }

  std::string GetEntityDisplayName(const entt::registry& registry, Entity entity)
  {
    char buffer[32];
    return GetEntityDisplayName(registry, entity, buffer);
  }

  const char* GetEntityDisplayName(const entt::registry& registry, Entity entity, std::span<char> buffer)
  {
    if (entity == entt::null)
      return "None";
    if (!registry.valid(entity))
      return "(missing entity)";
    if (const Name* name = registry.try_get<Name>(entity); name != nullptr && !name->empty())
      return name->c_str();
    std::snprintf(buffer.data(), buffer.size(), "Entity %u", uint32_t(entt::to_integral(entity)));
    return buffer.data();
  }

  size_t CountMaterialUsers(Scene& scene, MaterialHandle material)
  {
    size_t count = 0;
    for (auto [entity, component] : scene.GetView<MaterialComponent>().each())
    {
      if (component.asset == material)
        count++;
    }
    return count;
  }

  size_t MaterialUserCount::Get(Scene& scene, MaterialHandle material, double now)
  {
    const size_t components = scene.GetRegistry().storage<MaterialComponent>().size();
    if (material != m_Material || components != m_ComponentCount || now < m_CountedAt || now - m_CountedAt >= REFRESH_SECONDS)
    {
      m_Material = material;
      m_ComponentCount = components;
      m_CountedAt = now;
      m_Count = CountMaterialUsers(scene, material);
    }
    return m_Count;
  }

  Entity EntityNameLookup::Resolve(Scene& scene, std::string_view name)
  {
    entt::registry& registry = scene.GetRegistry();
    // The entity check catches a rename that went around Scene::SetName
    const bool current = m_Scene == &scene && m_Generation == scene.GetStructureGeneration() && m_Name == name
      && (m_Entity == entt::null
        || (registry.valid(m_Entity) && registry.all_of<Name>(m_Entity) && registry.get<Name>(m_Entity) == name));
    if (current)
      return m_Entity;

    m_Scene = &scene;
    m_Generation = scene.GetStructureGeneration();
    m_Name = name;
    m_Entity = entt::null;
    m_MatchCount = 0;
    if (name.empty())
      return m_Entity;

    // The same walk as FindEntityByName, so the first match is the entity it resolves to
    for (auto [entity, entityName] : registry.view<Name>().each())
    {
      if (entityName != name)
        continue;
      if (m_Entity == entt::null)
        m_Entity = entity;
      m_MatchCount++;
    }
    return m_Entity;
  }

  Entity ImportModel(AssetManager& assets, const std::string& path)
  {
    auto handle = assets.Models().Load(path);
    if (!handle)
      return entt::null;

    return assets.Models().Get(handle).rootEntity;
  }

  bool LoadSkybox(Scene& scene, AssetManager& assets, const std::string& path)
  {
    auto handle = assets.CubeMaps().Load(path);
    if (!handle)
      return false;

    scene.SetSkybox(handle);
    return true;
  }

  const char* GetDebugViewUnavailableReason(Render& render, int view)
  {
    if ((view == DEBUG_VIEW_PT_NOISY || view == DEBUG_VIEW_PT_REFERENCE
      || (view >= DEBUG_VIEW_PT_MAX_CONTRIB && view <= DEBUG_VIEW_PT_DELTA_LIGHTS))
      && !render.IsPathTracerAvailable())
    {
      return "The path tracer is unavailable on this device.\nIt needs hardware ray tracing and bindless descriptors.";
    }

    if ((view == DEBUG_VIEW_PT_GUIDES || view == DEBUG_VIEW_PT_SPECULAR_MOTION) && !render.IsPathTracingActive())
      return "The guide buffers are only written while the Path Tracing\nrender path is the effective one.";

    if (render.IsPathTracingActive() && IS_RASTER_ONLY_DEBUG_VIEW(view))
    {
      return "Written by a pass the Path Tracing render path switches off:\nthe AO chain, SSR, the deferred lighting diagnostics or the\ntemporal resolve.";
    }

    return nullptr;
  }

  bool PlaySequence(SequencePlayer& player, Scene& scene, Entity cameraTrack)
  {
    // A paused or scrubbed session of the same shot carries on from its playhead
    if (player.IsPaused() && player.GetCameraTrack() == cameraTrack)
    {
      player.Resume(scene);
      return true;
    }

    player.Play(scene, cameraTrack);
    return false;
  }
}
