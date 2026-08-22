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

    static void Save(const std::string& path,
      Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Render& render,
      const std::string& basePath = "");

    static void Load(const std::string& path,
      Scene& scene, AssetManager& assets,
      const ComponentRegistry& registry, Render& render,
      const std::string& basePath = "",
      ThreadPool* threadPool = nullptr);

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
