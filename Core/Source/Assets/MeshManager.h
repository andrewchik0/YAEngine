#pragma once

#include "AssetManagerBase.h"
#include "Render/VulkanVertexBuffer.h"

namespace YAEngine
{
  struct Mesh
  {
  private:
    VulkanVertexBuffer vertexBuffer;

    friend class MeshManager;
    friend class Render;
  };

  using MeshHandle = AssetHandle;

  class MeshManager : public AssetManagerBase<Mesh>
  {
  public:
    [[nodiscard]]
    AssetHandle Load(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void Destroy(MeshHandle handle);

    void DestroyAll();
  private:
  };
}
