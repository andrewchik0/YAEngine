#include "MeshManager.h"

#include "Render/RenderContext.h"

namespace YAEngine
{
  MeshHandle MeshManager::Load(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
  {
    auto mesh = std::make_unique<Mesh>();

    mesh->vertexBuffer.Create(*m_Ctx, (void*)vertices.data(), vertices.size(), sizeof(Vertex), indices);
    return Store(std::move(mesh));
  }

  void MeshManager::Destroy(MeshHandle handle)
  {
    Get(handle).vertexBuffer.Destroy(*m_Ctx);
    Remove(handle);
  }

  void MeshManager::DestroyAll()
  {
    ForEach([this](Mesh& mesh) {
      mesh.vertexBuffer.Destroy(*m_Ctx);
    });
    Clear();
  }
}
