#pragma once

namespace YAEngine
{
  class ComponentRegistry;
  class AssetManager;

  void RegisterCoreComponentSerializers(ComponentRegistry& registry, AssetManager& assets);
}
