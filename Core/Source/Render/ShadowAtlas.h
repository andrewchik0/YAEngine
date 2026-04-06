#pragma once

#include "VulkanImage.h"
#include "ShadowData.h"

namespace YAEngine
{
  struct RenderContext;

  struct ShadowViewport
  {
    VkViewport viewport;
    VkRect2D scissor;
    glm::vec4 atlasUV; // xy = offset (normalized), zw = size (normalized)
  };

  class ShadowAtlas
  {
  public:
    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    VkRenderPass GetRenderPass() const { return m_RenderPass; }
    VkFramebuffer GetFramebuffer() const { return m_Framebuffer; }
    VkExtent2D GetExtent() const { return { SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE }; }

    VkImageView GetView() const { return m_Image.GetView(); }
    VkSampler GetSampler() const { return m_Image.GetSampler(); }
    VkImage GetImage() const { return m_Image.GetImage(); }

    // CSM: 4 tiles 2048x2048 in top-left 4096x4096 (2x2 grid)
    static ShadowViewport GetCascadeViewport(uint32_t cascadeIndex);

    // Spot: up to 8 tiles 1024x1024 in top-right 4096x4096 (4x2 grid)
    static ShadowViewport GetSpotViewport(uint32_t spotIndex);

    // Point: up to 4x6=24 tiles 512x512 in bottom-left (8x3 grid starting at y=4096)
    static ShadowViewport GetPointFaceViewport(uint32_t pointIndex, uint32_t faceIndex);

  private:
    static ShadowViewport MakeViewport(uint32_t x, uint32_t y, uint32_t size);

    VulkanImage m_Image;
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
  };
}
