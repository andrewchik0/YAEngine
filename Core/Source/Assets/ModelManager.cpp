#include "ModelManager.h"

#include "AssetManager.h"
#include "Application.h"

namespace YAEngine
{
  ModelHandle ModelManager::Load(const std::string& path, bool combinedTextures)
  {
    auto desc = ModelImporter::Import(path, combinedTextures);
    if (desc.root.children.empty())
    {
      return {};
    }

    auto model = std::make_unique<Model>();
    model->rootEntity = m_Builder.Build(desc);

    return Store(std::move(model));
  }

  ModelHandle ModelManager::LoadInstanced(const std::string& path, const std::vector<glm::mat4>& instances, bool combinedTextures)
  {
    auto handle = Load(path, combinedTextures);
    if (!handle)
      return handle;

    auto& model = Get(handle);

    model.modelMatrices = instances;
    model.offset = Application::Get().GetRender().AllocateInstanceData(uint32_t(instances.size() * sizeof(glm::mat4)));

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
