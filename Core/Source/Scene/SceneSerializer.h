#pragma once

#include <yaml-cpp/yaml.h>

namespace YAEngine
{
  class Scene;
  class AssetManager;
  class ComponentRegistry;
  class Render;

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
      const std::string& basePath = "");
  };
}
