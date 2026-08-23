#pragma once

#include "AssetManagerBase.h"
#include "IAssetManager.h"
#include "ModelBuilder.h"
#include "ModelImporter.h"

#include "Scene/Scene.h"

namespace YAEngine
{
  struct Model
  {
    Entity rootEntity;

    std::vector<glm::mat4> modelMatrices;
    uint32_t offset = 0;

    // Imported state of the model. Kept alive for the lifetime of the instance so the
    // scene serializer can diff authored changes against it.
    std::shared_ptr<ModelTemplate> modelTemplate;
    std::vector<MaterialHandle> slotMaterials;
    std::vector<MaterialSnapshot> pristineMaterials;

    friend class ModelManager;
  };

  class ModelManager : public AssetManagerBase<Model, ModelTag>, public IAssetManager
  {
  public:

    void SetRenderContext(const AssetManagerInitInfo&) override {}

    ModelManager() = default;

    void SetDependencies(Scene* scene, AssetManager* assetManager, std::function<uint32_t(uint32_t)>&& allocateInstanceData)
    {
      m_Scene = scene;
      m_AssetManager = assetManager;
      m_Builder = ModelBuilder(scene, assetManager);
      m_AllocateInstanceData = std::move(allocateInstanceData);
    }

    ModelHandle Load(const std::string& path, bool combinedTextures = false);
    ModelHandle LoadFromDescription(ModelDescription&& desc, const std::string& sourcePath, bool combinedTextures);
    ModelHandle LoadInstanced(const std::string& path, const std::vector<glm::mat4>& instances, bool combinedTextures = false);

    // Model asset behind a scene entity, or nullptr when the entity is not a model root
    // or the asset is gone. The serializer and the editor both need this lookup.
    Model* FindByRoot(const Scene& scene, Entity rootEntity);

    void Destroy(ModelHandle handle);
    void DestroyAll() override;

  private:

    Scene* m_Scene = nullptr;
    AssetManager* m_AssetManager = nullptr;
    ModelBuilder m_Builder {};
    std::function<uint32_t(uint32_t)> m_AllocateInstanceData;

    ModelHandle BuildAndStore(const ModelDescription& desc, const std::string& sourcePath, bool combinedTextures);
    void DestroyEntityAssets(Entity entity);
    void TraverseInstanceData(Entity entity, std::vector<glm::mat4>* instanceData, uint32_t offset);
  };
}
