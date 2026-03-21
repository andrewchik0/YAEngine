#pragma once

#include "AssetManagerBase.h"
#include "Render/VulkanVertexBuffer.h"

namespace YAEngine
{
  struct RenderContext;

  struct Mesh
  {
  private:
    VulkanVertexBuffer vertexBuffer;
    std::vector<glm::mat4>* instanceData = nullptr;
    uint32_t offset = 0;

    friend class MeshManager;
    friend class Render;
    friend class ModelManager;
    friend class DebugUILayer;
  };

  using MeshHandle = AssetHandle;

  class MeshManager : public AssetManagerBase<Mesh>
  {
  public:

    void SetRenderContext(const RenderContext& ctx) { m_Ctx = &ctx; }

    [[nodiscard]]
    AssetHandle Load(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void Destroy(MeshHandle handle);

    void DestroyAll();
  private:
    const RenderContext* m_Ctx = nullptr;
  };
}
