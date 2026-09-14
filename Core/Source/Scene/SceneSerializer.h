#pragma once

#include <yaml-cpp/yaml.h>
#include <entt/entt.hpp>

namespace YAEngine
{
  struct IrradianceVolumeFileData;

  class Scene;
  class AssetManager;
  class ComponentRegistry;
  class Render;
  class ThreadPool;

  class SceneSerializer
  {
  public:

    // False when the file cannot be opened for writing.
    static bool Save(const std::string& path,
      Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Render& render,
      const std::string& basePath = "");

    static void Load(const std::string& path,
      Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Render& render,
      const std::string& basePath = "",
      ThreadPool* threadPool = nullptr);

    // The "settings" block of a scene file: render settings and the skybox. Public so the editor
    // bridge reads and patches settings in exactly the shape the scene file stores them.
    static YAML::Node SerializeRenderSettings(Scene& scene, AssetManager& assets, Render& render);

    // Applies a settings block. Absent keys keep their current value, so a partial block is a patch.
    static void ApplyRenderSettings(const YAML::Node& settings, Scene& scene, AssetManager& assets,
      Render& render);

    // Rebuilds the render side volume atlas from every baked IrradianceVolumeComponent
    // in the scene. Public because the editor has to re-run it after a bake.
    static void LoadIrradianceVolumes(Scene& scene, AssetManager& assets, Render& render);

    // Atlas half of LoadIrradianceVolumes, without the disk read. entities and volumes
    // are parallel. Public so a bounce loop can refresh the atlas from data that is
    // still only in memory.
    static void ApplyIrradianceVolumes(Scene& scene, Render& render,
      const std::vector<entt::entity>& entities,
      const std::vector<IrradianceVolumeFileData>& volumes);

  private:

    static void LoadSync(const YAML::Node& root, const YAML::Node& entities,
      Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Render& render);

    static void LoadParallel(const YAML::Node& root, const YAML::Node& entities,
      Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Render& render,
      ThreadPool& threadPool);
  };
}
