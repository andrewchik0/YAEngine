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
    glm::vec3 minBB { 0.0f };
    glm::vec3 maxBB { 0.0f };
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

    void SetInstanceData(MeshHandle handle, std::vector<glm::mat4>* data, uint32_t bufferOffset)
    {
      auto& m = Get(handle);
      m.instanceData = data;
      m.offset = bufferOffset;
    }

    glm::vec3 GetMinBB(MeshHandle handle) { return Get(handle).minBB; }
    glm::vec3 GetMaxBB(MeshHandle handle) { return Get(handle).maxBB; }
  private:
    const RenderContext* m_Ctx = nullptr;
  };
}
