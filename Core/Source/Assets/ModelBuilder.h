#pragma once

#include "ModelDescription.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class AssetManager;

  class ModelBuilder
  {
  public:

    ModelBuilder() = default;
    ModelBuilder(Scene* scene, AssetManager* assetManager)
      : m_Scene(scene), m_AssetManager(assetManager)
    {
    }

    Entity Build(const ModelDescription& desc);

  private:

    Entity BuildNode(const NodeDescription& node, Entity parent, const ModelDescription& desc);

    Scene* m_Scene = nullptr;
    AssetManager* m_AssetManager = nullptr;
  };
}
