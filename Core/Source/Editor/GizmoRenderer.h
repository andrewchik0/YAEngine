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
#include "Editor/Utils/EditorIcons.h"

namespace YAEngine
{
  struct RenderContext;

  // Billboard icons for entities without geometry; picking rebuilds the same quad so the
  // click hitbox never drifts from what's drawn.
  namespace EditorIcon
  {
    // Decodes an icon macro's UTF-8 literal, so the sprites follow the generated header
    constexpr uint32_t CodepointOf(const char* utf8)
    {
      const uint32_t b0 = uint8_t(utf8[0]);
      if (b0 < 0x80)
        return b0;
      if ((b0 & 0xE0) == 0xC0)
        return ((b0 & 0x1F) << 6) | (uint8_t(utf8[1]) & 0x3Fu);
      if ((b0 & 0xF0) == 0xE0)
        return ((b0 & 0x0F) << 12) | ((uint8_t(utf8[1]) & 0x3Fu) << 6) | (uint8_t(utf8[2]) & 0x3Fu);
      return ((b0 & 0x07) << 18) | ((uint8_t(utf8[1]) & 0x3Fu) << 12)
        | ((uint8_t(utf8[2]) & 0x3Fu) << 6) | (uint8_t(utf8[3]) & 0x3Fu);
    }

    constexpr float WORLD_SIZE = 0.5f;
    constexpr uint32_t LIGHT_BULB = CodepointOf(ICON_LC_LIGHTBULB);
    constexpr uint32_t SUN = CodepointOf(ICON_LC_SUN);
    constexpr uint32_t PROBE = CodepointOf(ICON_LC_GLOBE);
    constexpr uint32_t CAMERA = CodepointOf(ICON_LC_VIDEO);
  }

  enum class GizmoShape : uint8_t
  {
    Sphere,
    Box,
    Cone,
    Arrow,
    Line,
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

    void DrawWireSphereDepthTested(const glm::vec3& center, float radius, const glm::vec4& color);
    void DrawWireBoxDepthTested(const glm::vec3& center, const glm::vec3& extents, const glm::vec4& color);
    void DrawWireBoxDepthTested(const glm::vec3& center, const glm::vec3& extents, const glm::quat& rotation, const glm::vec4& color);
    // Raw transform for the unit box, for shapes an affine translate/rotate/scale cannot
    // express - see DrawWireFrustum.
    void DrawWireBoxDepthTested(const glm::mat4& transform, const glm::vec4& color);
    // Perspective frustum of a camera whose world matrix is cameraWorld (looking down -Z,
    // scale already removed), drawn from nearDist out to farDist.
    void DrawWireFrustum(const glm::mat4& cameraWorld, float fovY, float aspect,
      float nearDist, float farDist, const glm::vec4& color);
    // Depth-tested segment on the instanced path: a sampled spline is hundreds of them,
    // and one draw call per segment would not survive that.
    void DrawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);
    void DrawWireCone(const glm::vec3& origin, const glm::vec3& direction, float height, float angle, const glm::vec4& color);
    void DrawArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);

    void DrawSolidArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);
    void DrawSolidScaleArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);
    void DrawSolidRing(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color);

    void DrawSprite(const glm::vec3& position, float size, uint32_t codepoint, const glm::vec4& color);
    // Width/height of the rasterized glyph, the same factor gizmo_sprite.vert applies to
    // the quad. Picking has to reproduce the billboard exactly, so it needs this too.
    float GetSpriteAspect(uint32_t codepoint) const;

    // A billboard keeps its world size until its quad would be taller than this on screen, in
    // pixels at a content scale of 1; up close it would otherwise cover the viewport.
    static constexpr float SPRITE_MAX_PIXELS = 96.0f;

    // Camera the next overlay flush sizes sprites for, with the height of the target in pixels
    void SetSpriteView(const glm::mat4& view, const glm::mat4& proj, float viewportHeight);
    void SetSpriteMaxPixels(float pixels) { m_SpriteMaxPixels = pixels; }
    float GetSpriteMaxPixels() const { return m_SpriteMaxPixels; }
    // World size a sprite of the given size is drawn at from this camera. Drawing and icon
    // picking both size the quad with it.
    static float ClampSpriteSize(float size, const glm::vec3& position, const glm::mat4& view,
      const glm::mat4& proj, float viewportHeight, float maxPixels);

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

    GizmoMesh m_SphereMesh;
    GizmoMesh m_BoxMesh;
    GizmoMesh m_ConeMesh;
    GizmoMesh m_ArrowMesh;
    GizmoMesh m_LineMesh;

    GizmoMesh m_SolidArrowMesh;
    GizmoMesh m_SolidScaleArrowMesh;
    GizmoMesh m_SolidRingMesh;

    // Volume node overlay can queue tens of thousands of wire draws, so they batch into
    // one instanced draw per shape instead of one draw call per request.
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

    glm::mat4 m_SpriteView { 1.0f };
    glm::mat4 m_SpriteProj { 1.0f };
    float m_SpriteViewportHeight = 0.0f;
    float m_SpriteMaxPixels = SPRITE_MAX_PIXELS;

    GizmoAxis m_HoveredAxis = GizmoAxis::None;
    GizmoAxis m_DraggedAxis = GizmoAxis::None;
    glm::mat4 m_AxisTransforms[3] {};
    bool b_HasActiveGizmo = false;
    bool b_IsRingGizmo = false;
  };
}
