#pragma once

#include "Pch.h"
#include "GeometryArena.h"
#include "VulkanBuffer.h"
#include "Utils/MeshSimplifier.h"
#include "Utils/PositionQuantizer.h"

namespace YAEngine
{
  struct RenderContext;

  // Resolved draw range of one LOD level: the vertices are shared with level 0, so
  // only the index window moves.
  struct MeshLodRange
  {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
  };

  // What a pass has already bound for its position-only draws. The arena holds one
  // position buffer and two index buffers for the whole scene, so a run of meshes
  // over it binds geometry once and varies only the draw arguments, which is the
  // point of the arena; without this every mesh re-binds the same two handles.
  // A pass declares one on the stack and hands it to every draw it issues. That
  // scope is deliberate: the cache cannot outlive the recording it describes, and it
  // starts empty every time the pass runs. Draw() clears it because the interleaved
  // stream replaces these bindings, and anything else on the same command buffer
  // that binds a vertex or index buffer has to Clear() it for the same reason.
  struct MeshBindCache
  {
    VkBuffer vertices = VK_NULL_HANDLE;
    VkBuffer indices = VK_NULL_HANDLE;
    VkDeviceSize indexOffset = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;

    void Clear()
    {
      vertices = VK_NULL_HANDLE;
      indices = VK_NULL_HANDLE;
    }
  };

  struct Vertex
  {
    glm::vec3 position;
    glm::vec2 tex;
    glm::vec3 normal;
    glm::vec4 tangent;
  };

  struct VertexAttribs
  {
    glm::vec2 tex;
    glm::vec3 normal;
    glm::vec4 tangent;
  };

  class VulkanVertexBuffer
  {
  public:

    void Create(const RenderContext& ctx, const void* inputData, size_t vertexCount, uint32_t vertexSize,
      const std::vector<uint32_t>& indices, bool generateShadowLods = true);

    void Destroy(const RenderContext& ctx);

    // bindCache is optional: passing null simply binds on every draw, which is what
    // a pass that issues a single draw wants anyway.
    void Draw(VkCommandBuffer cmd, uint32_t instanceCount = 1, MeshBindCache* bindCache = nullptr);
    void DrawPositionOnly(VkCommandBuffer cmd, uint32_t instanceCount = 1, uint32_t lodLevel = 0,
      MeshBindCache* bindCache = nullptr);

    VkBuffer Get() const { return m_VerticesBuffer.Get(); }
    size_t GetIndexCount() const { return m_IndicesCount; }

    bool IsArenaResident() const { return m_ArenaAllocation.resident; }
    const GeometryArenaAllocation& GetArenaAllocation() const { return m_ArenaAllocation; }

    // Draw range for the requested level, falling back to the nearest populated one
    // below it. Never fails, so no call site has to test for a missing level.
    MeshLodRange GetLodRange(uint32_t lodLevel) const;

  private:

    void CreateWeldedPositions(const RenderContext& ctx,
      const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices,
      const MeshLodLevels* lods, const QuantizedPositions& quantized);

    void CreateStandalonePositions(const RenderContext& ctx,
      const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);

    // Binds the single vertex binding and the index buffer a position-only draw
    // reads, skipping the pair when the cache says they are already current.
    static void BindPositionStream(VkCommandBuffer cmd, MeshBindCache* bindCache,
      VkBuffer vertices, VkBuffer indices, VkDeviceSize indexOffset, VkIndexType indexType);

    VulkanBuffer m_VerticesBuffer;
    VulkanBuffer m_IndicesBuffer;
    size_t m_IndicesCount {};
    VkDeviceSize m_AttribOffset {};

    // Position-only stream with duplicate positions removed. Depth prepass and
    // shadow passes fetch this instead of the interleaved stream, which carries
    // one vertex per attribute combination and reuses none of them.
    // It normally lives in the shared geometry arena; only the arena pointer is
    // kept, never a cached VkBuffer, because growth replaces the arena's handles.
    GeometryArena* m_Arena = nullptr;
    GeometryArenaAllocation m_ArenaAllocation {};

    // Shadow LOD levels 1..N, indexing the same arena vertices as the allocation
    // above. A non-resident entry means that level collapsed into the one below it.
    GeometryArenaIndexRange m_LodRanges[MeshSimplifier::LOD_COUNT - 1] {};

    // Fallback for meshes the arena could not accept. Positions and indices share
    // one allocation: every staged upload costs a queue submit plus a fence wait,
    // and meshes are uploaded one by one.
    VulkanBuffer m_PositionsBuffer;
    VkDeviceSize m_PositionIndexOffset {};
    size_t m_PositionIndexCount {};
    VkIndexType m_PositionIndexType = VK_INDEX_TYPE_UINT32;
    bool b_HasStandalonePositions = false;
  };
}
