#pragma once

#include <yaml-cpp/yaml.h>

#include "Assets/Handle.h"

namespace YAEngine
{
  class ComponentRegistry;
  class AssetManager;
  class TextureManager;

  void RegisterCoreComponentSerializers(ComponentRegistry& registry, AssetManager& assets);

  // Texture reference helpers, shared with the model override layer so both write the
  // same shape. Textures embedded in a model file are addressed by a synthetic key that
  // no loader can resolve from disk, so they are never written out.
  bool IsEmbeddedTexturePath(const std::string& path);
  void SerializeTextureField(YAML::Node& node, const std::string& key,
    TextureHandle handle, TextureManager& textures, AssetManager& assets);
  TextureHandle DeserializeTextureField(const YAML::Node& node, const std::string& key,
    AssetManager& assets, bool* hasAlpha = nullptr);
}
