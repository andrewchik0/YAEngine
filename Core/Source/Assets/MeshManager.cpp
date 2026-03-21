#include "MeshManager.h"

#include "Render/RenderContext.h"

namespace YAEngine
{
  AssetHandle MeshManager::Load(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
  {
    auto mesh = std::make_unique<Mesh>();

    mesh->vertexBuffer.Create(*m_Ctx, (void*)vertices.data(), vertices.size(), sizeof(Vertex), indices);
    auto handle = AssetManagerBase::Load(std::move(mesh));
    return handle;
  }

  void MeshManager::Destroy(AssetHandle handle)
  {
    Get(handle).vertexBuffer.Destroy(*m_Ctx);
    Remove(handle);
  }

  void MeshManager::DestroyAll()
  {
    for (auto& mesh : GetAll())
    {
      mesh.second.get()->vertexBuffer.Destroy(*m_Ctx);
    }
    GetAll().clear();
  }
}
