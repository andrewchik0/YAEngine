#pragma once

#include "Pch.h"
#include "Utils/Topology.h"
#include "Render/VulkanBuffer.h"
#include "Render/PipelineCache.h"
#include "GizmoPushConstants.h"

namespace YAEngine
{
  struct RenderContext;

  enum class GizmoShape : uint8_t
  {
    Sphere,
    Cone,
    Arrow
  };

  class GizmoRenderer
  {
  public:
    void Init(const RenderContext& ctx, PipelineCache& psoCache, VkRenderPass renderPass, VkDescriptorSetLayout frameLayout);
    void Destroy(const RenderContext& ctx);

    void DrawWireSphere(const glm::vec3& center, float radius, const glm::vec4& color);
    void DrawWireCone(const glm::vec3& origin, const glm::vec3& direction, float height, float angle, const glm::vec4& color);
    void DrawArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);

    void Clear();
    void Flush(VkCommandBuffer cmd, VkDescriptorSet frameDescriptor);

  private:
    struct GizmoDrawRequest
    {
      GizmoShape shape;
      glm::mat4 transform;
      glm::vec4 color;
    };

    struct GizmoMesh
    {
      VulkanBuffer vertexBuffer;
      VulkanBuffer indexBuffer;
      uint32_t indexCount = 0;
    };

    static GizmoMesh UploadTopology(const RenderContext& ctx, const TopologyData& data);
    static glm::mat4 BuildRotation(const glm::vec3& direction);

    GizmoMesh m_SphereMesh;
    GizmoMesh m_ConeMesh;
    GizmoMesh m_ArrowMesh;

    PipelineHandle m_Pipeline;
    PipelineCache* m_PSOCache = nullptr;

    std::vector<GizmoDrawRequest> m_Requests;
  };
}
