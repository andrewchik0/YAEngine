#pragma once

#include "Pch.h"
#include "Utils/Topology.h"
#include "Render/VulkanBuffer.h"
#include "Render/VulkanTexture.h"
#include "Render/VulkanDescriptorSet.h"
#include "Render/PipelineCache.h"
#include "GizmoPushConstants.h"
#include "SpritePushConstants.h"

namespace YAEngine
{
  struct RenderContext;

  enum class GizmoShape : uint8_t
  {
    Sphere,
    Cone,
    Arrow,
    SolidArrow,
    SolidRing
  };

  enum class GizmoRenderMode : uint8_t
  {
    Wire,
    Solid
  };

  class GizmoRenderer
  {
  public:
    void Init(const RenderContext& ctx, PipelineCache& psoCache, VkRenderPass renderPass, VkDescriptorSetLayout frameLayout);
    void Destroy(const RenderContext& ctx);

    void DrawWireSphere(const glm::vec3& center, float radius, const glm::vec4& color);
    void DrawWireCone(const glm::vec3& origin, const glm::vec3& direction, float height, float angle, const glm::vec4& color);
    void DrawArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);

    void DrawSolidArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);
    void DrawSolidRing(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color);

    void DrawSprite(const glm::vec3& position, float size, uint32_t codepoint, const glm::vec4& color);

    void DrawTranslateGizmo(const glm::vec3& position, const glm::vec3& cameraPos);
    void DrawRotateGizmo(const glm::vec3& position, const glm::vec3& cameraPos);

    void Clear();
    void Flush(VkCommandBuffer cmd, VkDescriptorSet frameDescriptor);

  private:
    struct GizmoDrawRequest
    {
      GizmoShape shape;
      GizmoRenderMode mode;
      glm::mat4 transform;
      glm::vec4 color;
    };

    struct SpriteDrawRequest
    {
      glm::vec3 position;
      float size;
      uint32_t codepoint;
      glm::vec4 color;
    };

    struct GizmoMesh
    {
      VulkanBuffer vertexBuffer;
      VulkanBuffer indexBuffer;
      uint32_t indexCount = 0;
    };

    struct SpriteEntry
    {
      VulkanTexture texture;
      VulkanDescriptorSet descriptorSet;
      float aspectRatio = 1.0f;
    };

    static GizmoMesh UploadTopology(const RenderContext& ctx, const TopologyData& data);
    static glm::mat4 BuildRotation(const glm::vec3& direction);

    void LoadGlyphSprite(const RenderContext& ctx, uint32_t codepoint, float pixelHeight);

    // Wire meshes
    GizmoMesh m_SphereMesh;
    GizmoMesh m_ConeMesh;
    GizmoMesh m_ArrowMesh;

    // Solid meshes
    GizmoMesh m_SolidArrowMesh;
    GizmoMesh m_SolidRingMesh;

    PipelineHandle m_WirePipeline;
    PipelineHandle m_SolidPipeline;
    PipelineHandle m_SpritePipeline;
    PipelineCache* m_PSOCache = nullptr;

    VkDescriptorSetLayout m_SpriteTextureLayout {};

    std::vector<GizmoDrawRequest> m_Requests;
    std::vector<SpriteDrawRequest> m_SpriteRequests;

    std::unordered_map<uint32_t, SpriteEntry> m_SpriteEntries;
  };
}
