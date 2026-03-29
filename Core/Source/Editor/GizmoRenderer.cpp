#include "Editor/GizmoRenderer.h"

#include "Editor/GlyphRasterizer.h"
#include "Utils/Topology.h"
#include "Render/RenderContext.h"

#include <glm/gtc/matrix_transform.hpp>

namespace
{
  const glm::vec4 kAxisDim[3] = {
    {0.7f, 0.15f, 0.15f, 0.9f},   // X - red
    {0.15f, 0.7f, 0.15f, 0.9f},   // Y - green
    {0.15f, 0.3f, 0.7f, 0.9f}     // Z - blue
  };
  const glm::vec4 kAxisBright[3] = {
    {0.95f, 0.3f, 0.3f, 0.9f},    // X - bright red
    {0.3f, 0.95f, 0.3f, 0.9f},    // Y - bright green
    {0.3f, 0.5f, 0.95f, 0.9f}     // Z - bright blue
  };
  const glm::vec3 kAxisDirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
}

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
    if (glm::dot(direction, direction) <= 0.0f)
      YA_LOG_ERROR("Gizmo", "BuildRotation: direction must be non-zero");
    assert(glm::dot(direction, direction) > 0.0f);

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

  void GizmoRenderer::LoadGlyphSprite(const RenderContext& ctx, uint32_t codepoint, float pixelHeight)
  {
    GlyphRasterizer rasterizer;
    if (!rasterizer.Init(WORKING_DIR "/Assets/Fonts/FontAwesome7.ttf"))
      return;

    auto glyph = rasterizer.RasterizeGlyph(codepoint, pixelHeight);
    rasterizer.Destroy();

    if (!glyph.pixels || glyph.width <= 0 || glyph.height <= 0)
      return;

    // Add transparent padding so shadow blur doesn't clip at edges
    int pad = int(pixelHeight * 0.25f);
    int paddedW = glyph.width + pad * 2;
    int paddedH = glyph.height + pad * 2;

    auto padded = std::make_unique<uint8_t[]>(paddedW * paddedH);
    memset(padded.get(), 0, paddedW * paddedH);
    for (int y = 0; y < glyph.height; y++)
      memcpy(padded.get() + (y + pad) * paddedW + pad, glyph.pixels.get() + y * glyph.width, glyph.width);

    SpriteEntry entry;
    entry.aspectRatio = float(paddedW) / float(paddedH);
    entry.texture.Load(ctx, padded.get(), uint32_t(paddedW), uint32_t(paddedH), 1, VK_FORMAT_R8_UNORM);

    entry.descriptorSet.Init(ctx, m_SpriteTextureLayout);
    entry.descriptorSet.WriteCombinedImageSampler(0,
      entry.texture.GetView(), entry.texture.GetSampler());

    m_SpriteEntries[codepoint] = std::move(entry);
  }

  void GizmoRenderer::Init(const RenderContext& ctx, PipelineCache& psoCache, VkRenderPass renderPass, VkDescriptorSetLayout frameLayout)
  {
    m_PSOCache = &psoCache;

    // Wire meshes
    m_SphereMesh = UploadTopology(ctx, Topology::WireSphere());
    m_ConeMesh = UploadTopology(ctx, Topology::WireCone());
    m_ArrowMesh = UploadTopology(ctx, Topology::Arrow());

    // Solid meshes
    m_SolidArrowMesh = UploadTopology(ctx, Topology::SolidArrow());
    m_SolidScaleArrowMesh = UploadTopology(ctx, Topology::SolidScaleArrow());
    m_SolidRingMesh = UploadTopology(ctx, Topology::SolidRing());

    // Wire pipeline (LINE_LIST)
    PipelineCreateInfo wireInfo = {
      .fragmentShaderFile = "gizmo.frag",
      .vertexShaderFile = "gizmo.vert",
      .pushConstantSize = uint32_t(sizeof(GizmoPushConstants)),
      .depthTesting = false,
      .depthWrite = false,
      .blending = true,
      .doubleSided = true,
      .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
      .vertexInputFormat = "f3",
      .sets = std::vector({ frameLayout })
    };
    m_WirePipeline = psoCache.Register(ctx.device, renderPass, wireInfo, ctx.pipelineCache);

    // Solid pipeline (TRIANGLE_LIST)
    PipelineCreateInfo solidInfo = {
      .fragmentShaderFile = "gizmo.frag",
      .vertexShaderFile = "gizmo.vert",
      .pushConstantSize = uint32_t(sizeof(GizmoPushConstants)),
      .depthTesting = true,
      .depthWrite = true,
      .blending = true,
      .doubleSided = true,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .vertexInputFormat = "f3",
      .sets = std::vector({ frameLayout })
    };
    m_SolidPipeline = psoCache.Register(ctx.device, renderPass, solidInfo, ctx.pipelineCache);

    // Sprite texture descriptor layout (set 1, binding 0 = combined image sampler)
    {
      SetDescription spriteTexDesc = {
        .set = 1,
        .bindings = {
          { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT }
        }
      };
      VulkanDescriptorSet tempSet;
      tempSet.Init(ctx, spriteTexDesc);
      m_SpriteTextureLayout = tempSet.GetLayout();
      tempSet.Destroy();
    }

    // Sprite pipeline (TRIANGLE_STRIP, no vertex input)
    PipelineCreateInfo spriteInfo = {
      .fragmentShaderFile = "gizmo_sprite.frag",
      .vertexShaderFile = "gizmo_sprite.vert",
      .pushConstantSize = uint32_t(sizeof(SpritePushConstants)),
      .depthTesting = false,
      .depthWrite = false,
      .blending = true,
      .doubleSided = true,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .vertexInputFormat = "",
      .sets = std::vector({ frameLayout, m_SpriteTextureLayout })
    };
    m_SpritePipeline = psoCache.Register(ctx.device, renderPass, spriteInfo, ctx.pipelineCache);

    // Pre-rasterize glyph sprites
    LoadGlyphSprite(ctx, 0xf0eb, 128); // lightbulb
    LoadGlyphSprite(ctx, 0xf185, 128); // sun
  }

  void GizmoRenderer::Destroy(const RenderContext& ctx)
  {
    m_SphereMesh.vertexBuffer.Destroy(ctx);
    m_SphereMesh.indexBuffer.Destroy(ctx);
    m_ConeMesh.vertexBuffer.Destroy(ctx);
    m_ConeMesh.indexBuffer.Destroy(ctx);
    m_ArrowMesh.vertexBuffer.Destroy(ctx);
    m_ArrowMesh.indexBuffer.Destroy(ctx);

    m_SolidArrowMesh.vertexBuffer.Destroy(ctx);
    m_SolidArrowMesh.indexBuffer.Destroy(ctx);
    m_SolidScaleArrowMesh.vertexBuffer.Destroy(ctx);
    m_SolidScaleArrowMesh.indexBuffer.Destroy(ctx);
    m_SolidRingMesh.vertexBuffer.Destroy(ctx);
    m_SolidRingMesh.indexBuffer.Destroy(ctx);

    for (auto& [cp, entry] : m_SpriteEntries)
    {
      entry.texture.Destroy(ctx);
      entry.descriptorSet.Destroy();
    }
    m_SpriteEntries.clear();
  }

  void GizmoRenderer::Clear()
  {
    m_Requests.clear();
    m_SpriteRequests.clear();
  }

  void GizmoRenderer::DrawWireSphere(const glm::vec3& center, float radius, const glm::vec4& color)
  {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), center)
                         * glm::scale(glm::mat4(1.0f), glm::vec3(radius));
    m_Requests.push_back({ GizmoShape::Sphere, GizmoRenderMode::Wire, transform, color });
  }

  void GizmoRenderer::DrawWireCone(const glm::vec3& origin, const glm::vec3& direction, float height, float angle, const glm::vec4& color)
  {
    glm::mat4 rotation = BuildRotation(direction);
    float baseRadius = height * std::tan(angle);
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), origin)
                         * rotation
                         * glm::scale(glm::mat4(1.0f), glm::vec3(baseRadius, height, baseRadius));
    m_Requests.push_back({ GizmoShape::Cone, GizmoRenderMode::Wire, transform, color });
  }

  void GizmoRenderer::DrawArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color)
  {
    glm::mat4 rotation = BuildRotation(direction);
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), origin)
                         * rotation
                         * glm::scale(glm::mat4(1.0f), glm::vec3(length));
    m_Requests.push_back({ GizmoShape::Arrow, GizmoRenderMode::Wire, transform, color });
  }

  void GizmoRenderer::DrawSolidArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color)
  {
    glm::mat4 rotation = BuildRotation(direction);
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), origin)
                         * rotation
                         * glm::scale(glm::mat4(1.0f), glm::vec3(length));
    m_Requests.push_back({ GizmoShape::SolidArrow, GizmoRenderMode::Solid, transform, color });
  }

  void GizmoRenderer::DrawSolidScaleArrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color)
  {
    glm::mat4 rotation = BuildRotation(direction);
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), origin)
                         * rotation
                         * glm::scale(glm::mat4(1.0f), glm::vec3(length));
    m_Requests.push_back({ GizmoShape::SolidScaleArrow, GizmoRenderMode::Solid, transform, color });
  }

  void GizmoRenderer::DrawSolidRing(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color)
  {
    // Default ring lies in XZ plane (normal = Y). Rotate to target normal.
    glm::vec3 from(0.0f, 1.0f, 0.0f);
    glm::vec3 to = glm::normalize(normal);
    glm::mat4 rotation(1.0f);

    float d = glm::dot(from, to);
    if (d < -0.999f)
      rotation = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1, 0, 0));
    else if (d < 0.999f)
    {
      glm::vec3 axis = glm::normalize(glm::cross(from, to));
      rotation = glm::rotate(glm::mat4(1.0f), std::acos(d), axis);
    }

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), center)
                         * rotation
                         * glm::scale(glm::mat4(1.0f), glm::vec3(radius));
    m_Requests.push_back({ GizmoShape::SolidRing, GizmoRenderMode::Solid, transform, color });
  }

  void GizmoRenderer::DrawSprite(const glm::vec3& position, float size, uint32_t codepoint, const glm::vec4& color)
  {
    m_SpriteRequests.push_back({ position, size, codepoint, color });
  }

  void GizmoRenderer::DrawTranslateGizmo(const glm::vec3& position, const glm::vec3& cameraPos)
  {
    float dist = glm::length(cameraPos - position);
    float scale = dist * 0.15f;

    for (int i = 0; i < 3; ++i)
    {
      glm::mat4 rotation = BuildRotation(kAxisDirs[i]);
      glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
                           * rotation
                           * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
      m_AxisTransforms[i] = transform;

      auto axis = static_cast<GizmoAxis>(i + 1);
      bool highlight = (m_DraggedAxis != GizmoAxis::None) ? (m_DraggedAxis == axis) : (m_HoveredAxis == axis);
      glm::vec4 color = highlight ? kAxisBright[i] : kAxisDim[i];
      m_Requests.push_back({ GizmoShape::SolidArrow, GizmoRenderMode::Solid, transform, color });
    }

    m_HasActiveGizmo = true;
    m_IsRingGizmo = false;
  }

  void GizmoRenderer::DrawRotateGizmo(const glm::vec3& position, const glm::vec3& cameraPos)
  {
    float dist = glm::length(cameraPos - position);
    float scale = dist * 0.12f;

    for (int i = 0; i < 3; ++i)
    {
      glm::vec3 from(0.0f, 1.0f, 0.0f);
      glm::vec3 to = kAxisDirs[i];
      glm::mat4 rotation(1.0f);

      float d = glm::dot(from, to);
      if (d < -0.999f)
        rotation = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(1, 0, 0));
      else if (d < 0.999f)
      {
        glm::vec3 rotAxis = glm::normalize(glm::cross(from, to));
        rotation = glm::rotate(glm::mat4(1.0f), std::acos(d), rotAxis);
      }

      glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
                           * rotation
                           * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
      m_AxisTransforms[i] = transform;

      auto axis = static_cast<GizmoAxis>(i + 1);
      bool highlight = (m_DraggedAxis != GizmoAxis::None) ? (m_DraggedAxis == axis) : (m_HoveredAxis == axis);
      glm::vec4 color = highlight ? kAxisBright[i] : kAxisDim[i];
      m_Requests.push_back({ GizmoShape::SolidRing, GizmoRenderMode::Solid, transform, color });
    }

    m_HasActiveGizmo = true;
    m_IsRingGizmo = true;
  }

  void GizmoRenderer::DrawScaleGizmo(const glm::vec3& position, const glm::vec3& cameraPos)
  {
    float dist = glm::length(cameraPos - position);
    float scale = dist * 0.15f;

    for (int i = 0; i < 3; ++i)
    {
      glm::mat4 rotation = BuildRotation(kAxisDirs[i]);
      glm::mat4 transform = glm::translate(glm::mat4(1.0f), position)
                           * rotation
                           * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
      m_AxisTransforms[i] = transform;

      auto axis = static_cast<GizmoAxis>(i + 1);
      bool highlight = (m_DraggedAxis != GizmoAxis::None) ? (m_DraggedAxis == axis) : (m_HoveredAxis == axis);
      glm::vec4 color = highlight ? kAxisBright[i] : kAxisDim[i];
      m_Requests.push_back({ GizmoShape::SolidScaleArrow, GizmoRenderMode::Solid, transform, color });
    }

    m_HasActiveGizmo = true;
    m_IsRingGizmo = false;
  }

  void GizmoRenderer::UpdateHover(const Ray& ray)
  {
    m_HoveredAxis = GizmoAxis::None;
    if (!m_HasActiveGizmo) return;

    float closestT = std::numeric_limits<float>::max();

    for (int i = 0; i < 3; ++i)
    {
      glm::mat4 inv = glm::inverse(m_AxisTransforms[i]);
      glm::vec3 localOrigin = glm::vec3(inv * glm::vec4(ray.origin, 1.0f));
      glm::vec3 localDir = glm::normalize(glm::vec3(inv * glm::vec4(ray.direction, 0.0f)));
      Ray localRay { localOrigin, localDir };

      std::optional<float> hit;

      if (m_IsRingGizmo)
      {
        // Intersect with Y=0 plane, check radial distance matches ring [0.9, 1.0]
        if (std::abs(localDir.y) > 1e-6f)
        {
          float t = -localOrigin.y / localDir.y;
          if (t > 0.0f)
          {
            glm::vec3 p = localOrigin + t * localDir;
            float dist = std::sqrt(p.x * p.x + p.z * p.z);
            if (dist >= 0.75f && dist <= 1.15f)
              hit = t;
          }
        }
      }
      else
      {
        // AABB test covering arrow/scale arrow meshes with generous padding
        hit = RayAABBIntersect(localRay, glm::vec3(-0.12f, -1.1f, -0.12f), glm::vec3(0.12f, 0.05f, 0.12f));
      }

      if (hit && *hit < closestT)
      {
        closestT = *hit;
        m_HoveredAxis = static_cast<GizmoAxis>(i + 1);
      }
    }
  }

  void GizmoRenderer::Flush(VkCommandBuffer cmd, VkDescriptorSet frameDescriptor)
  {
    // Phase 1: Solid requests
    bool solidBound = false;
    for (auto& req : m_Requests)
    {
      if (req.mode != GizmoRenderMode::Solid) continue;

      if (!solidBound)
      {
        auto& pipeline = m_PSOCache->Get(m_SolidPipeline);
        pipeline.Bind(cmd);
        pipeline.BindDescriptorSets(cmd, { frameDescriptor }, 0);
        solidBound = true;
      }

      GizmoMesh* mesh = nullptr;
      switch (req.shape)
      {
        case GizmoShape::SolidArrow:      mesh = &m_SolidArrowMesh; break;
        case GizmoShape::SolidScaleArrow: mesh = &m_SolidScaleArrowMesh; break;
        case GizmoShape::SolidRing:       mesh = &m_SolidRingMesh; break;
        default: continue;
      }

      auto& pipeline = m_PSOCache->Get(m_SolidPipeline);
      GizmoPushConstants pc { req.transform, req.color };
      pipeline.PushConstants(cmd, &pc);

      VkBuffer vb = mesh->vertexBuffer.Get();
      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
      vkCmdBindIndexBuffer(cmd, mesh->indexBuffer.Get(), 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }

    // Phase 2: Wire requests
    bool wireBound = false;
    for (auto& req : m_Requests)
    {
      if (req.mode != GizmoRenderMode::Wire) continue;

      if (!wireBound)
      {
        auto& pipeline = m_PSOCache->Get(m_WirePipeline);
        pipeline.Bind(cmd);
        vkCmdSetLineWidth(cmd, 2.0f);
        pipeline.BindDescriptorSets(cmd, { frameDescriptor }, 0);
        wireBound = true;
      }

      GizmoMesh* mesh = nullptr;
      switch (req.shape)
      {
        case GizmoShape::Sphere: mesh = &m_SphereMesh; break;
        case GizmoShape::Cone:   mesh = &m_ConeMesh; break;
        case GizmoShape::Arrow:  mesh = &m_ArrowMesh; break;
        default: continue;
      }

      auto& pipeline = m_PSOCache->Get(m_WirePipeline);
      GizmoPushConstants pc { req.transform, req.color };
      pipeline.PushConstants(cmd, &pc);

      VkBuffer vb = mesh->vertexBuffer.Get();
      VkDeviceSize offset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
      vkCmdBindIndexBuffer(cmd, mesh->indexBuffer.Get(), 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cmd, mesh->indexCount, 1, 0, 0, 0);
    }

    // Phase 3: Sprites
    if (!m_SpriteRequests.empty())
    {
      auto& pipeline = m_PSOCache->Get(m_SpritePipeline);
      pipeline.Bind(cmd);
      pipeline.BindDescriptorSets(cmd, { frameDescriptor }, 0);

      for (auto& req : m_SpriteRequests)
      {
        auto it = m_SpriteEntries.find(req.codepoint);
        if (it == m_SpriteEntries.end()) continue;

        float ar = it->second.aspectRatio;
        pipeline.BindDescriptorSets(cmd, { it->second.descriptorSet.Get() }, 1);

        // Shadow pass
        SpritePushConstants shadowPc;
        shadowPc.positionAndScale = glm::vec4(req.position, req.size);
        shadowPc.color = req.color;
        shadowPc.shadow = glm::vec4(0.06f, ar, 0.7f, 1.0f);
        pipeline.PushConstants(cmd, &shadowPc);
        vkCmdDraw(cmd, 4, 1, 0, 0);

        // Main sprite
        SpritePushConstants pc;
        pc.positionAndScale = glm::vec4(req.position, req.size);
        pc.color = req.color;
        pc.shadow = glm::vec4(0.0f, ar, 0.0f, 0.0f);
        pipeline.PushConstants(cmd, &pc);
        vkCmdDraw(cmd, 4, 1, 0, 0);
      }
    }
  }
}
