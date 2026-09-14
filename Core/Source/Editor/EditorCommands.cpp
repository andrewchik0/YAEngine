#include "Editor/EditorCommands.h"

#include "Assets/AssetManager.h"
#include "Editor/EditorContext.h"
#include "Render/Render.h"
#include "Scene/CameraTrackPlayer.h"
#include "Scene/Components.h"
#include "Utils/Log.h"

namespace YAEngine::EditorCommands
{
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
        scene.AddComponent<TerrainComponent>(e);
        scene.AddComponent<MaterialComponent>(e, assets.FindOrCreateDefaultMaterial());
        scene.GetRegistry().emplace<TerrainDirty>(e);
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
    scene.GetName(copy) = name;
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

    scene.GetName(entity) = name;
    return true;
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
    if (view == DEBUG_VIEW_RAY_QUERY && !render.IsRayQueryAvailable())
      return "Ray queries are unavailable on this device";

    if (view == DEBUG_VIEW_RT_PIPELINE && !render.IsRayTracingPipelineAvailable())
      return "The ray tracing pipeline is unavailable on this device.\nIt needs hardware ray tracing and bindless descriptors.";

    if ((view == DEBUG_VIEW_PT_NOISY || view == DEBUG_VIEW_PT_REFERENCE
      || (view >= DEBUG_VIEW_PT_MAX_CONTRIB && view <= DEBUG_VIEW_PT_NONFINITE))
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

  bool PlayCameraTrack(CameraTrackPlayer& player, Scene& scene, Entity track)
  {
    const auto& keys = scene.GetComponent<CameraTrackComponent>(track).keys;
    float duration = keys.empty() ? 0.0f : keys.back().time;

    bool active = player.IsPlaying() && player.GetTrackEntity() == track;
    // Resuming exactly at the end would stop again on the next tick
    bool resumable = active && player.GetElapsed() < double(duration) - 1e-4;
    if (resumable)
    {
      player.Resume();
      return true;
    }

    if (active)
      player.Stop(scene);
    player.Start(scene, track);
    return false;
  }
}
