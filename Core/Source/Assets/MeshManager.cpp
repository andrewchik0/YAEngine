#include "MeshManager.h"

#include "Render/RenderContext.h"

namespace YAEngine
{
  MeshHandle MeshManager::Load(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
  {
    auto mesh = std::make_unique<Mesh>();

    mesh->vertexBuffer.Create(*m_Ctx, (void*)vertices.data(), vertices.size(), sizeof(Vertex), indices);

    if (!vertices.empty())
    {
      glm::vec3 bbMin = vertices[0].position;
      glm::vec3 bbMax = vertices[0].position;
      for (size_t i = 1; i < vertices.size(); i++)
      {
        bbMin = glm::min(bbMin, vertices[i].position);
        bbMax = glm::max(bbMax, vertices[i].position);
      }
      mesh->minBB = bbMin;
      mesh->maxBB = bbMax;
    }

    return Store(std::move(mesh));
  }

  MeshHandle MeshManager::LoadFromCpuData(CpuMeshData&& cpuData)
  {
    auto mesh = std::make_unique<Mesh>();
    mesh->vertexBuffer.CreateFromSoA(*m_Ctx, cpuData);
    mesh->minBB = cpuData.minBB;
    mesh->maxBB = cpuData.maxBB;
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
