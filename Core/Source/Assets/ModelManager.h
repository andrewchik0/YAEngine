#pragma once
#include <filesystem>

#include "AssetManagerBase.h"
#include "IAssetManager.h"
#include "ModelBuilder.h"
#include "ModelImporter.h"

#include "Scene/Scene.h"

class Scene;
class AssetManager;

namespace YAEngine
{
  struct Model
  {
    Entity rootEntity;

    std::vector<glm::mat4> modelMatrices;
    uint32_t offset = 0;

    friend class ModelManager;
  };

  class ModelManager : public AssetManagerBase<Model, ModelTag>, public IAssetManager
  {
  public:

    void SetRenderContext(const AssetManagerInitInfo&) override {}

    ModelManager() = default;

    void SetDependencies(Scene* scene, AssetManager* assetManager, std::function<uint32_t(uint32_t)> allocateInstanceData)
    {
      m_Scene = scene;
      m_AssetManager = assetManager;
      m_Builder = ModelBuilder(scene, assetManager);
      m_AllocateInstanceData = std::move(allocateInstanceData);
    }

    ModelHandle Load(const std::string& path, bool combinedTextures = false);
    ModelHandle LoadInstanced(const std::string& path, const std::vector<glm::mat4>& instances, bool combinedTextures = false);

    void Destroy(ModelHandle handle);
    void DestroyAll() override;

  private:

    Scene* m_Scene = nullptr;
    AssetManager* m_AssetManager = nullptr;
    ModelBuilder m_Builder {};
    std::function<uint32_t(uint32_t)> m_AllocateInstanceData;

    void DestroyEntityAssets(Entity entity);
    void TraverseInstanceData(Entity entity, std::vector<glm::mat4>* instanceData, uint32_t offset);
  };
}
