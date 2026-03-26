#pragma once

#include "AssetManagerBase.h"
#include "IAssetManager.h"
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
    friend class ModelManager;
  };

  class MeshManager : public AssetManagerBase<Mesh, MeshTag>, public IAssetManager
  {
  public:

    void SetRenderContext(const AssetManagerInitInfo& info) override { m_Ctx = info.ctx; }

    [[nodiscard]]
    MeshHandle Load(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void Destroy(MeshHandle handle);

    void DestroyAll() override;

    VulkanVertexBuffer& GetVertexBuffer(MeshHandle handle)
    {
      return Get(handle).vertexBuffer;
    }

    std::vector<glm::mat4>* GetInstanceData(MeshHandle handle)
    {
      return Get(handle).instanceData;
    }

    uint32_t GetInstanceOffset(MeshHandle handle)
    {
      return Get(handle).offset;
    }
  private:
    const RenderContext* m_Ctx = nullptr;
  };
}
