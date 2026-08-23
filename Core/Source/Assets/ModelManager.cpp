#include "ModelManager.h"

#include "AssetManager.h"
#include "Utils/Log.h"

namespace YAEngine
{
  ModelHandle ModelManager::Load(const std::string& path, bool combinedTextures)
  {
    auto desc = ModelImporter::Import(path, combinedTextures);
    if (desc.root.children.empty())
    {
      return {};
    }

    return BuildAndStore(desc, path, combinedTextures);
  }

  ModelHandle ModelManager::LoadFromDescription(ModelDescription&& desc, const std::string& sourcePath, bool combinedTextures)
  {
    if (desc.root.children.empty())
      return {};

    return BuildAndStore(desc, sourcePath, combinedTextures);
  }

  ModelHandle ModelManager::BuildAndStore(const ModelDescription& desc, const std::string& sourcePath, bool combinedTextures)
  {
    auto model = std::make_unique<Model>();

    model->modelTemplate = std::make_shared<ModelTemplate>(BuildModelTemplate(desc));
    model->modelTemplate->sourcePath = sourcePath;

    auto built = m_Builder.Build(desc, *model->modelTemplate);
    model->rootEntity = built.rootEntity;
    model->slotMaterials = std::move(built.slotMaterials);
    model->pristineMaterials = std::move(built.pristineMaterials);

    Entity rootEntity = model->rootEntity;
    auto handle = Store(std::move(model));

    auto& source = m_Scene->AddComponent<ModelSourceComponent>(rootEntity,
      ModelSourceComponent { .path = sourcePath, .combinedTextures = combinedTextures });
    source.handle = handle;

    return handle;
  }

  Model* ModelManager::FindByRoot(const Scene& scene, Entity rootEntity)
  {
    if (rootEntity == entt::null || !scene.HasComponent<ModelSourceComponent>(rootEntity))
      return nullptr;

    return TryGet(scene.GetComponent<ModelSourceComponent>(rootEntity).handle);
  }

  ModelHandle ModelManager::LoadInstanced(const std::string& path, const std::vector<glm::mat4>& instances, bool combinedTextures)
  {
    auto handle = Load(path, combinedTextures);
    if (!handle)
      return handle;

    auto& model = Get(handle);

    model.modelMatrices = instances;
    uint32_t dataSize = uint32_t(instances.size() * sizeof(glm::mat4));
    model.offset = m_AllocateInstanceData(dataSize);

    if (model.offset == UINT32_MAX)
    {
      YA_LOG_ERROR("Render", "Model instance buffer out of space: requested %u bytes, skipping instance data for '%s'",
        dataSize, path.c_str());
      return handle;
    }

    TraverseInstanceData(model.rootEntity, &model.modelMatrices, model.offset);

    return handle;
  }

  void ModelManager::Destroy(ModelHandle handle)
  {
    if (!Has(handle))
      return;

    auto& model = Get(handle);
    DestroyEntityAssets(model.rootEntity);
    m_Scene->DestroyEntity(model.rootEntity);
    Remove(handle);
  }

  void ModelManager::DestroyAll()
  {
    ForEach([this](Model& model) {
      DestroyEntityAssets(model.rootEntity);
      m_Scene->DestroyEntity(model.rootEntity);
    });
    Clear();
  }

  void ModelManager::DestroyEntityAssets(Entity entity)
  {
    auto& hc = m_Scene->GetHierarchy(entity);

    if (m_Scene->HasComponent<MeshComponent>(entity))
    {
      auto& mc = m_Scene->GetComponent<MeshComponent>(entity);
      if (m_AssetManager->Meshes().Has(mc.asset))
        m_AssetManager->Meshes().Destroy(mc.asset);
    }
    if (m_Scene->HasComponent<MaterialComponent>(entity))
    {
      auto& mc = m_Scene->GetComponent<MaterialComponent>(entity);
      if (m_AssetManager->Materials().Has(mc.asset))
        m_AssetManager->Materials().Destroy(mc.asset);
    }

    Entity child = hc.firstChild;
    while (child != entt::null)
    {
      Entity next = m_Scene->GetHierarchy(child).nextSibling;
      DestroyEntityAssets(child);
      child = next;
    }
  }

  void ModelManager::TraverseInstanceData(Entity entity, std::vector<glm::mat4>* instanceData, uint32_t offset)
  {
    auto& hc = m_Scene->GetHierarchy(entity);

    if (m_Scene->HasComponent<MeshComponent>(entity))
    {
      auto& mesh = m_AssetManager->Meshes().Get(
        m_Scene->GetComponent<MeshComponent>(entity).asset
      );

      mesh.instanceData = instanceData;
      mesh.offset = offset;
    }

    if (hc.firstChild != entt::null)
    {
      TraverseInstanceData(hc.firstChild, instanceData, offset);
    }

    if (hc.nextSibling != entt::null)
    {
      TraverseInstanceData(hc.nextSibling, instanceData, offset);
    }
  }
}
