#include "ShadowAtlas.h"
#include "DebugMarker.h"
#include "RenderContext.h"
#include "VulkanCommandBuffer.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void ShadowAtlas::Init(const RenderContext& ctx)
  {
    ImageDesc imageDesc;
    imageDesc.width = SHADOW_ATLAS_SIZE;
    imageDesc.height = SHADOW_ATLAS_SIZE;
    imageDesc.format = VK_FORMAT_D32_SFLOAT;
    imageDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageDesc.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    SamplerDesc samplerDesc;
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerDesc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerDesc.compareEnable = true;
    samplerDesc.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    m_Image.Init(ctx, imageDesc, &samplerDesc);

    // Load variant preserves prior contents (initialLayout READ_ONLY, not UNDEFINED -
    // UNDEFINED may discard the whole subresource, not just renderArea).
    VkAttachmentReference depthRef {};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependencies[2] {};
    // Entry: the atlas image is shared across frames in flight, and the implicit
    // entry dependency carries no execution dependency, so without this the pass
    // could start writing depth while the previous frame's lighting pass is
    // still sampling the atlas.
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // Exit: depth writes visible to the passes that sample the atlas.
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    auto createPass = [&](VkAttachmentLoadOp loadOp, VkImageLayout initialLayout) -> VkRenderPass
    {
      VkAttachmentDescription depthAttachment {};
      depthAttachment.format = VK_FORMAT_D32_SFLOAT;
      depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
      depthAttachment.loadOp = loadOp;
      depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      depthAttachment.initialLayout = initialLayout;
      depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

      VkRenderPassCreateInfo rpInfo {};
      rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
      rpInfo.attachmentCount = 1;
      rpInfo.pAttachments = &depthAttachment;
      rpInfo.subpassCount = 1;
      rpInfo.pSubpasses = &subpass;
      rpInfo.dependencyCount = 2;
      rpInfo.pDependencies = dependencies;

      VkRenderPass pass = VK_NULL_HANDLE;
      if (vkCreateRenderPass(ctx.device, &rpInfo, nullptr, &pass) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create shadow atlas render pass");
        throw std::runtime_error("Failed to create shadow atlas render pass!");
      }
      return pass;
    };

    m_RenderPass = createPass(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED);
    m_LoadRenderPass = createPass(VK_ATTACHMENT_LOAD_OP_LOAD,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    VkImageView view = m_Image.GetView();

    VkFramebufferCreateInfo fbInfo {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_RenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &view;
    fbInfo.width = SHADOW_ATLAS_SIZE;
    fbInfo.height = SHADOW_ATLAS_SIZE;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(ctx.device, &fbInfo, nullptr, &m_Framebuffer) != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create shadow atlas framebuffer");
      throw std::runtime_error("Failed to create shadow atlas framebuffer!");
    }

    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_IMAGE,
      m_Image.GetImage(), "ShadowAtlas");
    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_IMAGE_VIEW,
      m_Image.GetView(), "ShadowAtlas View");
    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_RENDER_PASS,
      m_RenderPass, "ShadowAtlas RenderPass");
    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_RENDER_PASS,
      m_LoadRenderPass, "ShadowAtlas Load RenderPass");
    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_FRAMEBUFFER,
      m_Framebuffer, "ShadowAtlas FB");

    // Run an empty render pass to transition image to DEPTH_STENCIL_READ_ONLY_OPTIMAL.
    // Without this, the deferred lighting pass would sample from an UNDEFINED layout
    // on frames where shadow rendering is skipped.
    {
      auto cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      VkClearValue clearValue {};
      clearValue.depthStencil = { 1.0f, 0 };

      VkRenderPassBeginInfo rpBegin {};
      rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpBegin.renderPass = m_RenderPass;
      rpBegin.framebuffer = m_Framebuffer;
      rpBegin.renderArea = { {0, 0}, { SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE } };
      rpBegin.clearValueCount = 1;
      rpBegin.pClearValues = &clearValue;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdEndRenderPass(cmd);

      ctx.commandBuffer->EndSingleTimeCommands(cmd);

      m_Image.SetLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

    YA_LOG_INFO("Render", "Shadow atlas created (%dx%d, D32_SFLOAT)", SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE);
  }

  ShadowViewport ShadowAtlas::MakeViewport(uint32_t x, uint32_t y, uint32_t size)
  {
    float atlasF = float(SHADOW_ATLAS_SIZE);
    return {
      .viewport = {
        .x = float(x), .y = float(y),
        .width = float(size), .height = float(size),
        .minDepth = 0.0f, .maxDepth = 1.0f
      },
      .scissor = {
        .offset = { int32_t(x), int32_t(y) },
        .extent = { size, size }
      },
      .atlasUV = glm::vec4(
        float(x) / atlasF, float(y) / atlasF,
        float(size) / atlasF, float(size) / atlasF)
    };
  }

  ShadowViewport ShadowAtlas::GetCascadeViewport(uint32_t cascadeIndex)
  {
    uint32_t col = cascadeIndex % 2;
    uint32_t row = cascadeIndex / 2;
    return MakeViewport(col * SHADOW_CASCADE_SIZE, row * SHADOW_CASCADE_SIZE, SHADOW_CASCADE_SIZE);
  }

  ShadowViewport ShadowAtlas::GetSpotViewport(uint32_t spotIndex)
  {
    // Top-right quadrant: 4x2 grid of 1024x1024 tiles starting at (4096, 0)
    uint32_t col = spotIndex % 4;
    uint32_t row = spotIndex / 4;
    return MakeViewport(4096 + col * SHADOW_SPOT_SIZE, row * SHADOW_SPOT_SIZE, SHADOW_SPOT_SIZE);
  }

  ShadowViewport ShadowAtlas::GetPointFaceViewport(uint32_t pointIndex, uint32_t faceIndex)
  {
    // Bottom region: 8x3 grid of 512x512 tiles starting at (0, 4096)
    uint32_t linearIndex = pointIndex * 6 + faceIndex;
    uint32_t col = linearIndex % 8;
    uint32_t row = linearIndex / 8;
    return MakeViewport(col * SHADOW_POINT_FACE_SIZE, 4096 + row * SHADOW_POINT_FACE_SIZE, SHADOW_POINT_FACE_SIZE);
  }

  void ShadowAtlas::Destroy(const RenderContext& ctx)
  {
    if (m_Framebuffer)
    {
      vkDestroyFramebuffer(ctx.device, m_Framebuffer, nullptr);
      m_Framebuffer = VK_NULL_HANDLE;
    }
    if (m_RenderPass)
    {
      vkDestroyRenderPass(ctx.device, m_RenderPass, nullptr);
      m_RenderPass = VK_NULL_HANDLE;
    }
    if (m_LoadRenderPass)
    {
      vkDestroyRenderPass(ctx.device, m_LoadRenderPass, nullptr);
      m_LoadRenderPass = VK_NULL_HANDLE;
    }
    m_Image.Destroy(ctx);
  }
}
