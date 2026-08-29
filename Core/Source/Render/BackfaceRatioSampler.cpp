#ifdef YA_EDITOR

#include "BackfaceRatioSampler.h"

#include "Render.h"
#include "VulkanCommandBuffer.h"
#include "FrameContext.h"
#include "Utils/Log.h"
#include "Utils/Projection.h"

namespace YAEngine
{
  void BackfaceRatioSampler::Init(Render& render, uint32_t resolution)
  {
    m_Render = &render;
    m_Ctx = &render.GetContext();
    m_Resolution = resolution;

    m_FrameUBO.Init(*m_Ctx);
    SetupGraph(resolution);

    // The render pass only exists once the graph is compiled, so the pipelines that
    // draw into it cannot be registered from Render::InitPipelines like every other
    // pipeline in the engine.
    m_Render->InitBackfaceMaskPipelines(m_Graph.GetPassRenderPass(m_MaskPassIndex));
  }

  void BackfaceRatioSampler::Destroy()
  {
    if (!m_Ctx) return;

    m_FrameUBO.Destroy(*m_Ctx);
    m_Graph.Destroy();

    m_Ctx = nullptr;
    m_Render = nullptr;
    m_Resolution = 0;
  }

  void BackfaceRatioSampler::SetupGraph(uint32_t resolution)
  {
    m_Graph.Init(*m_Ctx, { resolution, resolution }, { resolution, resolution });

    m_Depth = m_Graph.CreateResource({
      .name = "backfaceDepth",
      .format = VK_FORMAT_D32_SFLOAT,
      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT
    });
    // One channel is all the winding needs, and the readback walks it byte by byte.
    m_Mask = m_Graph.CreateResource({
      .name = "backfaceMask",
      .format = VK_FORMAT_R8_UNORM,
      .additionalUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    });

    // Cleared to zero, which is also what "no geometry in this direction" means, so
    // sky and empty space need no drawing at all.
    m_MaskPassIndex = m_Graph.AddPass({
      .name = "BackfaceMask",
      .colorOutputs = { m_Mask },
      .depthOutput = m_Depth,
      .clearColor = true,
      .clearDepth = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        m_Render->DrawMeshesBackfaceMask(ctx.cmd, *frame, m_FrameUBO.GetDescriptorSet(0));
      }
    });

    m_Graph.Compile();
  }

  VulkanImage& BackfaceRatioSampler::RenderFace(FrameContext& frame,
    const glm::vec3& position, const glm::mat4& faceView)
  {
    glm::mat4 proj = MakeReversedInfinitePerspective(glm::radians(90.0f), 1.0f, 0.01f);
    proj[1][1] *= -1.0f;

    glm::mat4 view = glm::translate(faceView, -position);

    // mesh_depth.vert reads view and proj only. The rest of the block stays at the
    // zero it was constructed with - nothing in this pass samples it, and the mask
    // is a geometric fact that no render setting may influence.
    auto& uniforms = m_FrameUBO.uniforms;
    uniforms.view = view;
    uniforms.proj = proj;
    uniforms.invProj = glm::inverse(proj);
    uniforms.invView = glm::inverse(view);
    uniforms.nearPlane = 0.01f;
    uniforms.farPlane = 1000.0f;
    uniforms.cameraPosition = position;
    uniforms.cameraDirection = glm::normalize(-glm::vec3(glm::inverse(faceView)[2]));
    uniforms.fov = glm::radians(90.0f);
    uniforms.screenWidth = int(m_Resolution);
    uniforms.screenHeight = int(m_Resolution);

    m_FrameUBO.SetUp(0);

    // The caller copies the mask out between faces, which moves it out of the layout
    // the graph last recorded. Resetting the tracking keeps the next barrier honest.
    m_Graph.SetResourceLayout(m_Depth, VK_IMAGE_LAYOUT_UNDEFINED);
    m_Graph.SetResourceLayout(m_Mask, VK_IMAGE_LAYOUT_UNDEFINED);

    VkCommandBuffer cmd = m_Ctx->commandBuffer->BeginSingleTimeCommands();
    m_Graph.Execute(cmd, &frame);
    m_Ctx->commandBuffer->EndSingleTimeCommands(cmd);

    return m_Graph.GetResource(m_Mask);
  }
}

#endif
