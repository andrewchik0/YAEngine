#include "Editor/GizmoRenderer.h"

#include "Utils/Topology.h"
#include "Render/RenderContext.h"

#include <glm/gtc/matrix_transform.hpp>

namespace YAEngine
{
  GizmoRenderer::GizmoMesh GizmoRenderer::UploadTopology(const RenderContext& ctx, const TopologyData& data)
  {
    GizmoMesh mesh;
    mesh.vertexBuffer = VulkanBuffer::CreateStaged(ctx, data.positions.data(),
      data.positions.size() * sizeof(glm::vec3), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    mesh.indexBuffer = VulkanBuffer::CreateStaged(ctx, data.indices.data(),
      data.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    mesh.indexCount = uint32_t(data.indices.size());
    return mesh;
  }

  glm::mat4 GizmoRenderer::BuildRotation(const glm::vec3& direction)
  {
    assert(glm::dot(direction, direction) > 0.0f && "BuildRotation: direction must be non-zero");

    glm::vec3 from(0.0f, -1.0f, 0.0f);
    glm::vec3 to = glm::normalize(direction);

    float d = glm::dot(from, to);
    if (d < -0.999f)
      return glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1, 0, 0));
    if (d > 0.999f)
      return glm::mat4(1.0f);

    glm::vec3 axis = glm::normalize(glm::cross(from, to));
    return glm::rotate(glm::mat4(1.0f), std::acos(d), axis);
  }

  void GizmoRenderer::Init(const RenderContext& ctx, PipelineCache& psoCache, VkRenderPass renderPass, VkDescriptorSetLayout frameLayout)
  {
    m_PSOCache = &psoCache;

    m_SphereMesh = UploadTopology(ctx, Topology::WireSphere());
    m_ConeMesh = UploadTopology(ctx, Topology::WireCone());
    m_ArrowMesh = UploadTopology(ctx, Topology::Arrow());

    PipelineCreateInfo info = {
      .fragmentShaderFile = "gizmo.frag",
      .vertexShaderFile = "gizmo.vert",
      .pushConstantSize = uint32_t(sizeof(GizmoPushConstants)),
      .depthWrite = false,
      .blending = true,
      .doubleSided = true,
      .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
      .vertexInputFormat = "f3",
      .sets = std::vector({ frameLayout })
    };
    m_Pipeline = psoCache.Register(ctx.device, renderPass, info, ctx.pipelineCache);
  }

  void GizmoRenderer::Destroy(const RenderContext& ctx)
  {
    m_SphereMesh.vertexBuffer.Destroy(ctx);
    m_SphereMesh.indexBuffer.Destroy(ctx);
    m_ConeMesh.vertexBuffer.Destroy(ctx);
    m_ConeMesh.indexBuffer.Destroy(ctx);
    m_ArrowMesh.vertexBuffer.Destroy(ctx);
    m_ArrowMesh.indexBuffer.Destroy(ctx);
  }

  void GizmoRenderer::Clear()
  {
    m_Requests.clear();
  }

  void GizmoRenderer::DrawWireSphere(const glm::vec3& center, float radius, const glm::vec4& color)
  {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), center)
                         * glm::scale(glm::mat4(1.0f), glm::vec3(radius));
    m_Requests.push_back({ GizmoShape::Sphere, transform, color });
  }

  void GizmoRenderer::DrawWireCone(const glm::vec3& origin, const glm::vec3& direction, float height, float angle, const glm::vec4& color)
  {
    glm::mat4 rotation = BuildRotation(direction);
    float baseRadius = height * std::tan(angle);
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), origin)
                         * rotation
                         * glm::scale(glm::mat4(1.0f), glm::vec3(baseRadius, height, baseRadius));
    m_Requests.push_back({ GizmoShape::Cone, transform, color });
  }

  void GizmoRenderer::DrawArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color)
  {
    glm::mat4 rotation = BuildRotation(direction);
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), origin)
                         * rotation
                         * glm::scale(glm::mat4(1.0f), glm::vec3(length));
    m_Requests.push_back({ GizmoShape::Arrow, transform, color });
  }

  void GizmoRenderer::Flush(VkCommandBuffer cmd, VkDescriptorSet frameDescriptor)
  {
    if (m_Requests.empty()) return;

    auto& pipeline = m_PSOCache->Get(m_Pipeline);
    pipeline.Bind(cmd);
    vkCmdSetLineWidth(cmd, 2.0f);
    pipeline.BindDescriptorSets(cmd, { frameDescriptor }, 0);

    for (auto& req : m_Requests)
    {
      GizmoMesh* mesh = nullptr;
      switch (req.shape)
      {
        case GizmoShape::Sphere: mesh = &m_SphereMesh; break;
        case GizmoShape::Cone:   mesh = &m_ConeMesh; break;
        case GizmoShape::Arrow:  mesh = &m_ArrowMesh; break;
      }

      GizmoPushConstants pc { req.transform, req.color };
      pipeline.PushConstants(cmd, &pc);

      VkBuffer vb = mesh->vertexBuffer.Get();
      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
      vkCmdBindIndexBuffer(cmd, mesh->indexBuffer.Get(), 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }
  }
}
