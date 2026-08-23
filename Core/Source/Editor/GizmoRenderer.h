#pragma once

#include "Pch.h"
#include "Utils/Topology.h"
#include "Utils/Ray.h"
#include "Render/VulkanBuffer.h"
#include "Render/VulkanTexture.h"
#include "Render/VulkanDescriptorSet.h"
#include "Render/PipelineCache.h"
#include "GizmoPushConstants.h"
#include "SpritePushConstants.h"

namespace YAEngine
{
  struct RenderContext;

  // Billboard icons for entities with no geometry of their own. Editor picking rebuilds
  // the same quad from these, so the icon the user sees and the icon that catches the
  // click cannot drift apart.
  namespace EditorIcon
  {
    constexpr float WORLD_SIZE = 0.5f;
    constexpr uint32_t LIGHT_BULB = 0xf0eb;
    constexpr uint32_t SUN = 0xf185;
    constexpr uint32_t PROBE = 0xf0ac;
  }

  enum class GizmoShape : uint8_t
  {
    Sphere,
    Box,
    Cone,
    Arrow,
    SolidArrow,
    SolidScaleArrow,
    SolidRing
  };

  enum class GizmoRenderMode : uint8_t
  {
    Wire,
    WireDepthTested,
    Solid
  };

  enum class GizmoMode : uint8_t { Translate, Rotate, Scale };
  enum class GizmoAxis : uint8_t { None, X, Y, Z };

  class GizmoRenderer
  {
  public:
    void Init(const RenderContext& ctx, PipelineCache& psoCache, VkRenderPass renderPass, VkDescriptorSetLayout frameLayout);
    void Destroy(const RenderContext& ctx);

    void DrawWireSphere(const glm::vec3& center, float radius, const glm::vec4& color);
    void DrawWireBox(const glm::vec3& center, const glm::vec3& extents, const glm::vec4& color);
    void DrawWireBox(const glm::vec3& center, const glm::vec3& extents, const glm::quat& rotation, const glm::vec4& color);
    void DrawWireSphereDepthTested(const glm::vec3& center, float radius, const glm::vec4& color);
    void DrawWireBoxDepthTested(const glm::vec3& center, const glm::vec3& extents, const glm::vec4& color);
    void DrawWireBoxDepthTested(const glm::vec3& center, const glm::vec3& extents, const glm::quat& rotation, const glm::vec4& color);
    void DrawWireCone(const glm::vec3& origin, const glm::vec3& direction, float height, float angle, const glm::vec4& color);
    void DrawArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);

    void DrawSolidArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);
    void DrawSolidScaleArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);
    void DrawSolidRing(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color);

    void DrawSprite(const glm::vec3& position, float size, uint32_t codepoint, const glm::vec4& color);
    // Width/height of the rasterized glyph, the same factor gizmo_sprite.vert applies to
    // the quad. Picking has to reproduce the billboard exactly, so it needs this too.
    float GetSpriteAspect(uint32_t codepoint) const;

    void DrawTranslateGizmo(const glm::vec3& position, const glm::vec3& cameraPos);
    void DrawRotateGizmo(const glm::vec3& position, const glm::vec3& cameraPos);
    void DrawScaleGizmo(const glm::vec3& position, const glm::vec3& cameraPos);

    void UpdateHover(const Ray& ray);
    void ClearHover() { m_HoveredAxis = GizmoAxis::None; }
    GizmoAxis GetHoveredAxis() const { return m_HoveredAxis; }

    void SetDraggedAxis(GizmoAxis axis) { m_DraggedAxis = axis; }

    void Clear();
    void FlushDepthTested(VkCommandBuffer cmd, VkDescriptorSet frameDescriptor);
    void FlushOverlay(VkCommandBuffer cmd, VkDescriptorSet frameDescriptor);

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

    // One entry per instanced wire draw. Layout must match the per-instance
    // attributes of gizmo_instanced.vert.
    struct GizmoInstance
    {
      glm::mat4 world;
      glm::vec4 color;
    };

    static GizmoMesh UploadTopology(const RenderContext& ctx, const TopologyData& data);
    static glm::mat4 BuildRotation(const glm::vec3& direction);

    const GizmoMesh* MeshForShape(GizmoShape shape) const;

    void LoadGlyphSprite(const RenderContext& ctx, uint32_t codepoint, float pixelHeight);

    // Wire meshes
    GizmoMesh m_SphereMesh;
    GizmoMesh m_BoxMesh;
    GizmoMesh m_ConeMesh;
    GizmoMesh m_ArrowMesh;

    // Solid meshes
    GizmoMesh m_SolidArrowMesh;
    GizmoMesh m_SolidScaleArrowMesh;
    GizmoMesh m_SolidRingMesh;

    // Depth-tested wire draws all use the same handful of meshes, and the volume
    // node overlay can queue tens of thousands of them, so they go through one
    // instanced draw per shape instead of one draw call per request.
    static constexpr uint32_t MAX_WIRE_INSTANCES = 32768;

    VulkanBuffer m_InstanceBuffer;
    std::vector<GizmoInstance> m_InstanceScratch;

    PipelineHandle m_WirePipeline;
    PipelineHandle m_WireDepthPipeline;
    PipelineHandle m_WireDepthInstancedPipeline;
    PipelineHandle m_SolidPipeline;
    PipelineHandle m_SpritePipeline;
    PipelineCache* m_PSOCache = nullptr;

    VkDescriptorSetLayout m_SpriteTextureLayout {};

    std::vector<GizmoDrawRequest> m_Requests;
    std::vector<SpriteDrawRequest> m_SpriteRequests;

    std::unordered_map<uint32_t, SpriteEntry> m_SpriteEntries;

    // Hover and drag state
    GizmoAxis m_HoveredAxis = GizmoAxis::None;
    GizmoAxis m_DraggedAxis = GizmoAxis::None;
    glm::mat4 m_AxisTransforms[3] {};
    bool b_HasActiveGizmo = false;
    bool b_IsRingGizmo = false;
  };
}
