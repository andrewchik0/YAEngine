#include "Render.h"

#include <imgui_impl_glfw.h>
#include <ImGui/imgui_impl_vulkan.h>

#include "BloomData.h"
#include "DebugMarker.h"
#include "ExposureData.h"
#include "ImageBarrier.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void Render::WriteRayTracingSceneDescriptors(uint32_t frameIndex)
  {
    // Rewritten at this frame's own slot every frame rather than once at setup. The slot's
    // fence was waited on in BeginFrame, so nothing is still reading the set, and this
    // absorbs the structure and the record buffer being replaced outright whenever the
    // instance capacity grows.
    auto& rtDebug = m_Graph.GetResource(m_RTDebug);

    m_RTDebugDescriptorSets[frameIndex].Writer()
      .WriteAccelerationStructure(0, m_TlasBuilder.Get(frameIndex))
      .WriteStorageBuffer(1, m_TlasBuilder.GetRecordBuffer(frameIndex),
        m_TlasBuilder.GetRecordBufferSize(frameIndex))
      .WriteStorageBuffer(2, m_MaterialTable.GetBuffer(frameIndex),
        m_MaterialTable.GetBufferSize(frameIndex))
      .WriteStorageImage(3, rtDebug.GetView())
      .Flush();
  }

  void Render::WritePathTraceDescriptors(uint32_t frameIndex)
  {
    // Rewritten every frame at this frame's own slot, exactly like the debug views' set:
    // bindings 0-2 name buffers the TLAS builder may have replaced outright when capacity
    // grew, and the rest are graph resources a resize reallocates.
    auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
    auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
    auto& mainDepth = m_Graph.GetResource(m_MainDepth);
    auto& noisy = m_Graph.GetResource(m_PathTraceNoisy);
    auto& accumulation = m_Graph.GetResource(m_PathTraceAccum);
    auto& hitDistance = m_Graph.GetResource(m_PTHitDistance);

    m_PathTraceDescriptorSets[frameIndex].Writer()
      .WriteAccelerationStructure(0, m_TlasBuilder.Get(frameIndex))
      .WriteStorageBuffer(1, m_TlasBuilder.GetRecordBuffer(frameIndex),
        m_TlasBuilder.GetRecordBufferSize(frameIndex))
      .WriteStorageBuffer(2, m_MaterialTable.GetBuffer(frameIndex),
        m_MaterialTable.GetBufferSize(frameIndex))
      .WriteCombinedImageSampler(3, gbuffer0.GetView(), gbuffer0.GetSampler(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      .WriteCombinedImageSampler(4, gbuffer1.GetView(), gbuffer1.GetSampler(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      .WriteCombinedImageSampler(5, mainDepth.GetView(), mainDepth.GetSampler(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      // The very cubemap the IBL sets bind at set 3 binding 3, so the sky behind a traced
      // pixel and behind a rasterized one is one texture.
      .WriteCombinedImageSampler(6, m_SkyboxView, m_SkyboxSampler)
      .WriteStorageBuffer(7, m_LightBuffer.GetBuffer(frameIndex), sizeof(LightBuffer))
      .WriteStorageImage(8, noisy.GetView())
      .WriteStorageImage(9, accumulation.GetView())
      .WriteStorageImage(10, hitDistance.GetView())
      .Flush();
  }

  void Render::WritePathTraceGuideDescriptors(uint32_t frameIndex)
  {
    // Every binding names a graph resource a resize reallocates, so the set is rewritten at
    // this frame's own slot rather than once at setup - the same rule the tracer's set
    // above follows, for the same reason.
    auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
    auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
    auto& mainDepth = m_Graph.GetResource(m_MainDepth);
    auto& diffuseAlbedo = m_Graph.GetResource(m_PTDiffuseAlbedo);
    auto& specularAlbedo = m_Graph.GetResource(m_PTSpecularAlbedo);
    auto& normalRoughness = m_Graph.GetResource(m_PTNormalRoughness);

    m_PathTraceGuideDescriptorSets[frameIndex].Writer()
      .WriteCombinedImageSampler(0, gbuffer0.GetView(), gbuffer0.GetSampler(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      .WriteCombinedImageSampler(1, gbuffer1.GetView(), gbuffer1.GetSampler(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      .WriteCombinedImageSampler(2, mainDepth.GetView(), mainDepth.GetSampler(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      .WriteStorageImage(3, diffuseAlbedo.GetView())
      .WriteStorageImage(4, specularAlbedo.GetView())
      .WriteStorageImage(5, normalRoughness.GetView())
      // Read straight off the Render-owned resources rather than through a cached view the
      // way the skybox is: CubicTextureResources::Init loads the LUT unconditionally and it
      // is never replaced, so by the time any frame writes this set it is always the same
      // live texture. The IBL set binds the very same image at set 3 binding 2.
      .WriteCombinedImageSampler(6,
        m_CubicResources.brdfLut.GetView(), m_CubicResources.brdfLut.GetSampler())
      .Flush();
  }

  void Render::SetupRenderGraph(VkExtent2D renderExtent, VkExtent2D outputExtent)
  {
    auto& ctx = m_Backend.GetContext();
    auto swapFormat = m_Backend.GetSwapChain().GetFormat();

    m_Graph.Init(ctx, renderExtent, outputExtent);

    m_GBuffer0 = m_Graph.CreateResource({
      .name = "gbuffer0",
      .format = VK_FORMAT_R8G8B8A8_UNORM
    });
    m_GBuffer1 = m_Graph.CreateResource({
      .name = "gbuffer1",
      .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32
    });
    m_MainDepth = m_Graph.CreateResource({
      .name = "mainDepth",
      .format = VK_FORMAT_D32_SFLOAT,
      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT
    });
    m_MainVelocity = m_Graph.CreateResource({
      .name = "mainVelocity",
      .format = VK_FORMAT_R16G16_SFLOAT
    });

    m_LitColor = m_Graph.CreateResource({
      .name = "litColor",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    // Linear view depth pyramid GTAO marches. Point filtering is required: interpolating
    // between neighbouring depths inside a mip invents surfaces that are not there.
    m_GTAODepth = m_Graph.CreateResource({
      .name = "gtaoDepth",
      .format = VK_FORMAT_R16_SFLOAT,
      .filter = VK_FILTER_NEAREST,
      .mipLevels = GTAO_DEPTH_MIP_LEVELS
    });
    m_GTAOWorkingAO = m_Graph.CreateResource({
      .name = "gtaoWorkingAO",
      .format = VK_FORMAT_R8_UNORM,
      .filter = VK_FILTER_NEAREST
    });
    m_GTAOEdges = m_Graph.CreateResource({
      .name = "gtaoEdges",
      .format = VK_FORMAT_R8_UNORM,
      .filter = VK_FILTER_NEAREST
    });
    m_AOFinal = m_Graph.CreateResource({
      .name = "aoFinal",
      .format = VK_FORMAT_R8_UNORM
    });
    // Last frame's TAA history reprojected to this frame, validity in alpha. Mip
    // structure mirrors gtaoDepth so the march reads both at the same level; the
    // default linear filter stays - unlike depth, radiance wants to interpolate.
    m_SSGIRadiance = m_Graph.CreateResource({
      .name = "ssgiRadiance",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .mipLevels = GTAO_DEPTH_MIP_LEVELS
    });
    m_SSGIWorking = m_Graph.CreateResource({
      .name = "ssgiWorking",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .filter = VK_FILTER_NEAREST
    });
    m_SSGIFinal = m_Graph.CreateResource({
      .name = "ssgiFinal",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    m_SSGIBentWorking = m_Graph.CreateResource({
      .name = "ssgiBentWorking",
      .format = VK_FORMAT_R8G8_UNORM,
      .filter = VK_FILTER_NEAREST
    });
    m_SSGIBentFinal = m_Graph.CreateResource({
      .name = "ssgiBentFinal",
      .format = VK_FORMAT_R8G8_UNORM
    });
    m_SSRColor = m_Graph.CreateResource({
      .name = "ssrColor",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });
    m_TAAHistory0 = m_Graph.CreateResource({
      .name = "taaHistory0",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    });
    m_TAAHistory1 = m_Graph.CreateResource({
      .name = "taaHistory1",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
    });

    // DLSS writes this through a storage view, so the swapchain image can never be the
    // target: it is created without STORAGE usage. TRANSFER_DST is there because
    // Streamline clears the tagged output itself when it (re)builds its instance.
    m_DLSSOutput = m_Graph.CreateResource({
      .name = "dlssOutput",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .resolution = RGResolution::Output
    });

    // Visualization of one inline ray query per pixel. Allocated like every other graph
    // resource, on a device that can never fill it too: the tonemap pass samples it from a
    // statically accessed binding, which has to hold a live view whatever the hardware is.
    m_RTDebug = m_Graph.CreateResource({
      .name = "rtDebug",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT
    });

    // One path traced sample per pixel: radiance in rgb, primary hit distance in alpha for
    // stage 5's ray reconstruction. Allocated on every device for the same reason rtDebug
    // is - the tonemap pass samples it from a statically accessed binding.
    // Point filtered, both of them. R32G32B32A32_SFLOAT is not a format Vulkan requires to
    // support linear filtering, so the accumulation image may not ask for it - and the two
    // views have to be resampled identically or they would stop being comparable in the
    // DLSS modes, where the render extent is below the output one and the tonemap pass
    // samples these at output resolution.
    // TRANSFER_DST on both: neither is ever cleared by a pass, so ClearPathTraceOutputs
    // blacks them out wherever they are allocated - a frame that resolves the accumulation
    // image without the trace having run would otherwise present raw allocation contents.
    m_PathTraceNoisy = m_Graph.CreateResource({
      .name = "pathTraceNoisy",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .filter = VK_FILTER_NEAREST
    });
    // The reference image, a running mean over every sample since the last reset. RGBA32F
    // rather than fp16: a mean over thousands of samples stops moving in half precision long
    // before the image has converged, because 1/(n+1) of a new sample falls below the
    // spacing of what is already there. It persists across frames and is never cleared - the
    // sample index in the push constant is what resets it.
    //
    // The path tracing render path resolves into this image, so bloom, the histogram and
    // the tone map all sample it. Point filtering is what makes that safe on every device:
    // R32G32B32A32_SFLOAT is required to be SAMPLED, never to be filtered linearly, and the
    // path forces render extent == output extent so every consumer but the bloom downsample
    // reads it texel for texel anyway.
    m_PathTraceAccum = m_Graph.CreateResource({
      .name = "pathTraceAccum",
      .format = VK_FORMAT_R32G32B32A32_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .filter = VK_FILTER_NEAREST
    });

    // Ray reconstruction guides, written from the G-buffer while the path tracing render
    // path is effective. Allocated on every device for the same reason rtDebug is: the
    // tonemap pass samples all three from statically accessed bindings.
    m_PTDiffuseAlbedo = m_Graph.CreateResource({
      .name = "ptDiffuseAlbedo",
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .filter = VK_FILTER_NEAREST
    });
    m_PTSpecularAlbedo = m_Graph.CreateResource({
      .name = "ptSpecularAlbedo",
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .filter = VK_FILTER_NEAREST
    });
    // World-space normal in rgb, roughness in a - the PACKED layout sl_dlss_d.h names
    // ("App needs to write Roughness to w channel of Normal resource"), which is what
    // EvaluateRayReconstruction selects with DLSSDNormalRoughnessMode::ePacked.
    m_PTNormalRoughness = m_Graph.CreateResource({
      .name = "ptNormalRoughness",
      .format = VK_FORMAT_R16G16B16A16_SFLOAT,
      .filter = VK_FILTER_NEAREST
    });
    // Primary hit distance, the fourth guide - and the only one the G-buffer cannot supply,
    // because it is a property of the ray rather than of the surface it landed on. Written
    // by pt_main.rgen next to the value it already puts in the noisy alpha, since a
    // Streamline tag names one whole resource and cannot point at a channel of another.
    // TRANSFER_DST for the same reason the two images above have it: no pass ever clears it.
    m_PTHitDistance = m_Graph.CreateResource({
      .name = "ptHitDistance",
      .format = VK_FORMAT_R16_SFLOAT,
      .additionalUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .filter = VK_FILTER_NEAREST
    });

    uint32_t hizMipCount = static_cast<uint32_t>(
      std::floor(std::log2(std::max(renderExtent.width, renderExtent.height)))) + 1;
    m_HiZResource = m_Graph.CreateResource({
      .name = "hiZ",
      .format = VK_FORMAT_R32_SFLOAT,
      .filter = VK_FILTER_NEAREST,
      .mipLevels = hizMipCount
    });

    // 1. Depth prepass - fills depth buffer first (wins on overdraw)
    m_DepthPrepassIndex = m_Graph.AddPass({
      .name = "DepthPrepass",
      .depthOutput = m_MainDepth,
      .clearDepth = true,
      .depthOnly = true,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        DrawMeshesDepthOnly(ctx.cmd, m_Backend.GetCurrentFrameIndex(), *frame);
      }
    });

    m_GBufferPassIndex = m_Graph.AddPass({
      .name = "GBufferPass",
      .inputs = {},
      .colorOutputs = {m_GBuffer0, m_GBuffer1, m_MainVelocity},
      .depthOutput = m_MainDepth,
      .clearColor = true,
      .clearDepth = false,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        DrawMeshes(ctx.cmd, m_Backend.GetCurrentFrameIndex(), *frame);
      }
    });

    // 2b. Ray query debug view (compute). The TLAS is built before the graph and its build
    // barrier already targets the compute stage, so this could sit anywhere; it is declared
    // here because it consumes nothing the graph produces and so sorts to the front of the
    // frame alongside the other compute passes.
    //
    // isEnabled is re-evaluated every frame, and everything it tests is known by the time
    // Execute runs: the TLAS build precedes the graph, so IsValid already describes this
    // frame. False drops the pass whole - no descriptor writes, no dispatch, no barrier -
    // which is what makes the view cost nothing while it is off.
    m_RTDebugPassIndex = m_Graph.AddPass({
      .name = "RayQueryDebug",
      .storageOutputs = {m_RTDebug},
      .isCompute = true,
      .isEnabled = [this]() {
        return m_CurrentTexture == DEBUG_VIEW_RAY_QUERY
          && m_Backend.GetContext().rayQuerySupported
          && b_RayTracingEnabled
          && m_TlasBuilder.IsValid(m_Backend.GetCurrentFrameIndex())
          && m_MaterialTable.IsValid(m_Backend.GetCurrentFrameIndex());
      },
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        WriteRayTracingSceneDescriptors(currentFrame);

        auto& pipeline = m_PSOCache.GetCompute(m_RTDebugPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_RTDebugDescriptorSets[currentFrame].Get()}, 1);

        // Bound once here rather than kept resident: the table is global and never
        // rebuilt, so this costs one vkCmdBindDescriptorSets and the pass owes nothing
        // to whatever bound set 2 before it.
        auto* bindless = m_Backend.GetContext().bindlessTextures;
        if (bindless != nullptr && bindless->IsValid())
          pipeline.BindDescriptorSets(ctx.cmd, {bindless->GetSet()},
            BindlessTextureRegistry::BINDLESS_TEXTURE_SET);

        uint32_t w = m_Graph.GetExtent().width;
        uint32_t h = m_Graph.GetExtent().height;
        pipeline.Dispatch(ctx.cmd, (w + 7) / 8, (h + 7) / 8, 1);

        b_RTDebugValid = true;
      }
    });

    // 2c. The same visualization traced through the ray tracing pipeline instead: rgen,
    // one hit group and one miss shader off a shader binding table. It writes the same
    // image as the pass above and is mutually exclusive with it - each tests its own view
    // id, and only one of the two can be selected - so the two never race for RTDebug.
    //
    // shaderStage is what makes the graph's barriers correct here: the storage image
    // transition it needs is a ray tracing stage one, not a compute one, and the pass that
    // samples RTDebug afterwards has to wait on the same stage.
    m_RTPipelineDebugPassIndex = m_Graph.AddPass({
      .name = "RTPipelineDebug",
      .storageOutputs = {m_RTDebug},
      .isCompute = true,
      .shaderStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
      .isEnabled = [this]() {
        return m_CurrentTexture == DEBUG_VIEW_RT_PIPELINE
          && IsRayTracingPipelineAvailable()
          && b_RayTracingEnabled
          && m_TlasBuilder.IsValid(m_Backend.GetCurrentFrameIndex())
          && m_MaterialTable.IsValid(m_Backend.GetCurrentFrameIndex());
      },
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        WriteRayTracingSceneDescriptors(currentFrame);

        auto& pipeline = m_PSOCache.GetRayTracing(m_RTPipelineDebugPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_RTDebugDescriptorSets[currentFrame].Get()}, 1);

        // The pipeline is only ever registered on a device with a live bindless table, so
        // this set is not optional the way it is for the ray query pass above.
        pipeline.BindDescriptorSets(ctx.cmd, {m_Backend.GetContext().bindlessTextures->GetSet()},
          BindlessTextureRegistry::BINDLESS_TEXTURE_SET);

        // One invocation per pixel exactly, not a rounded-up tile count: a trace launch is
        // sized in rays, so there is nothing for a workgroup to overshoot.
        pipeline.TraceRays(ctx.cmd, m_Graph.GetExtent().width, m_Graph.GetExtent().height);

        b_RTDebugValid = true;
      }
    });

    // 2d. The path tracer. One sample per pixel per frame, feeding the two path traced debug
    // views: PT Noisy shows the sample, PT Reference the running mean the same dispatch
    // keeps. One pass serves both - which of the two images the tonemap pass then displays
    // is the only difference, and whether the mean is updated at all rides on a push
    // constant rather than on a second pipeline.
    //
    // Its first path vertex is read out of the G-buffer instead of traced, which is why the
    // pass declares those as inputs: that ORDERS it after the G-buffer pass, where the
    // debug views above sort to the front of the frame because they consume nothing.
    m_PathTracePassIndex = m_Graph.AddPass({
      .name = "PathTrace",
      .inputs = {m_GBuffer0, m_GBuffer1, m_MainDepth},
      .storageOutputs = {m_PathTraceNoisy, m_PathTraceAccum, m_PTHitDistance},
      .isCompute = true,
      .shaderStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
      .isEnabled = [this]() { return IsPathTracePassEnabled(); },
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        WritePathTraceDescriptors(currentFrame);

        // Two things the graph's own barriers do not cover, both for the same reason - the
        // shared barrier table in ImageBarrier.h is written in terms of the fragment and
        // compute stages, and only the compute bit is substituted for shaderStage:
        //
        // - A sampled input is made visible to VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, and
        //   this pass reads the G-buffer and depth from the ray tracing stage.
        // - The accumulation image is read-modify-written, and it is the PREVIOUS frame's
        //   trace that wrote what this one loads. The transition chain the graph emits
        //   around it establishes write-then-read but names SHADER_WRITE as its
        //   destination, not the read this pass also performs.
        //
        // One global barrier rather than a change to the table, which every raster pass
        // shares - and whose ray tracing stage bit would not even be legal on a device
        // without the extension.
        VkMemoryBarrier traceInputBarrier {};
        traceInputBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        traceInputBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
          | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        traceInputBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
            | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
          VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
          0, 1, &traceInputBarrier, 0, nullptr, 0, nullptr);

        auto& pipeline = m_PSOCache.GetRayTracing(m_PathTracePipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_PathTraceDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_Backend.GetContext().bindlessTextures->GetSet()},
          BindlessTextureRegistry::BINDLESS_TEXTURE_SET);

        PathTraceConstants pc {};
        pc.maxBounces = m_PathTraceMaxBounces;
        // -1 is how the shader is told to leave the accumulation image alone rather than
        // overwrite it with a one-sample mean, which is what the noisy view alone wants.
        // The render path always accumulates - the mean is the image it presents.
        pc.sampleIndex = IsPathTraceAccumulating() ? m_PathTraceSampleIndex : -1;
        pc.fireflyClamp = m_PathTraceFireflyClamp;
        pc.debugMode = GetPathTraceDebugMode();
        pipeline.PushConstants(ctx.cmd, &pc);

        // One invocation per pixel exactly, not a rounded-up tile count: a trace launch is
        // sized in rays, so there is nothing for a workgroup to overshoot.
        pipeline.TraceRays(ctx.cmd, m_Graph.GetExtent().width, m_Graph.GetExtent().height);

        b_PathTraceOutputValid = true;
      }
    });

    // 2e. Ray reconstruction guides. A plain compute pass over the G-buffer rather than an
    // epilogue in pt_main.rgen, for two reasons: the raygen shader returns early on sky,
    // unlit and emissive texels, so folding the stores in would mean restructuring its
    // control flow for a consumer that does not exist yet, and its already forked set 1
    // would grow three more storage images. What that costs is one full-screen G-buffer
    // read plus three stores, and only on frames the path tracing render path owns - the
    // isEnabled below is what keeps it at exactly zero everywhere else.
    m_PathTraceGuidesPassIndex = m_Graph.AddPass({
      .name = "PathTraceGuides",
      .inputs = {m_GBuffer0, m_GBuffer1, m_MainDepth},
      .storageOutputs = {m_PTDiffuseAlbedo, m_PTSpecularAlbedo, m_PTNormalRoughness},
      .isCompute = true,
      .isEnabled = [this]() { return IsPathTracingActive(); },
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        WritePathTraceGuideDescriptors(currentFrame);

        // The same gap the path tracing pass covers by hand, for the same reason: the
        // shared barrier table makes a sampled input visible to the FRAGMENT stage, and
        // this pass reads the G-buffer and depth from a compute shader. A pass whose input
        // is already in SHADER_READ_ONLY emits no barrier at all, so the dependency on the
        // G-buffer write has to be stated here rather than relied on.
        VkMemoryBarrier gbufferBarrier {};
        gbufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        gbufferBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
          | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        gbufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          0, 1, &gbufferBarrier, 0, nullptr, 0, nullptr);

        auto& pipeline = m_PSOCache.GetCompute(m_PathTraceGuidesPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_PathTraceGuideDescriptorSets[currentFrame].Get()}, 1);

        uint32_t w = m_Graph.GetExtent().width;
        uint32_t h = m_Graph.GetExtent().height;
        pipeline.Dispatch(ctx.cmd, (w + 7) / 8, (h + 7) / 8, 1);
      }
    });

    // 3. GTAO depth prefilter - the only pass that reads the reversed-Z depth buffer. It
    // writes positive linear view depth plus four reduced mips, so the sampling passes below
    // never have to know the projection convention.
    m_GTAODepthPrefilterPassIndex = m_Graph.AddPass({
      .name = "GTAODepthPrefilter",
      .inputs = {m_MainDepth},
      .storageOutputs = {m_GTAODepth},
      .isCompute = true,
      // Nothing downstream of the AO chain runs while the path tracer owns the frame:
      // the tracer integrates occlusion for real and the deferred pass that consumes
      // AOFinal is off.
      .isEnabled = [this]() { return !IsPathTracingActive(); },
      .altName = "SSGIDepthPrefilter",
      .useAltName = [this]() { return b_SSGIEnabled && b_AOEnabled; },
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_AOEnabled) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);

        m_GTAOPrefilterDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto& pipeline = m_PSOCache.GetCompute(m_GTAOPrefilterPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_GTAOPrefilterDescriptorSets[currentFrame].Get()}, 1);

        // Each invocation owns a 2x2 block and one 8x8 group covers a 16x16 pixel tile.
        uint32_t w = m_Graph.GetExtent().width;
        uint32_t h = m_Graph.GetExtent().height;
        pipeline.Dispatch(ctx.cmd, (w + 15) / 16, (h + 15) / 16, 1);
      }
    });

    // 3b. SSGI radiance prefilter - reprojects last frame's TAA history to this
    // frame's pixels (validity in alpha) and folds the validity-weighted mip
    // chain, so the GTAO march below picks its radiance with one mipped fetch.
    m_SSGIRadiancePrefilterPassIndex = m_Graph.AddPass({
      .name = "SSGIRadiancePrefilter",
      // Slot 0 is the ping-pong history read handle, retargeted every frame from
      // Draw; History1 here only names the static declaration.
      .inputs = {m_TAAHistory1, m_MainVelocity, m_MainDepth},
      .storageOutputs = {m_SSGIRadiance},
      .isCompute = true,
      // Its source is the previous frame's resolved image, which the path tracer's own
      // accumulation supersedes, and its consumer is the GTAO pass, which is off.
      .isEnabled = [this]() { return !IsPathTracingActive(); },
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_AOEnabled || !b_SSGIEnabled) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        // Sampled purely through normalized UVs, so an output-resolution source needs
        // nothing beyond this binding change.
        auto& history = m_Graph.GetResource(GetPreviousResolvedColorHandle());
        auto& velocity = m_Graph.GetResource(m_MainVelocity);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);

        auto& set = m_SSGIPrefilterDescriptorSets[currentFrame];
        set.WriteCombinedImageSampler(0,
          history.GetView(), history.GetSampler(), history.GetLayout());
        set.WriteCombinedImageSampler(1,
          velocity.GetView(), velocity.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        set.WriteCombinedImageSampler(2,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto& pipeline = m_PSOCache.GetCompute(m_SSGIRadiancePrefilterPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {set.Get()}, 1);

        int invalidate = b_SSGIInvalidatePending ? 1 : 0;
        pipeline.PushConstants(ctx.cmd, &invalidate);
        b_SSGIInvalidatePending = false;

        // Each invocation owns a 2x2 block and one 8x8 group covers a 16x16 pixel tile.
        uint32_t w = m_Graph.GetExtent().width;
        uint32_t h = m_Graph.GetExtent().height;
        pipeline.Dispatch(ctx.cmd, (w + 15) / 16, (h + 15) / 16, 1);
      }
    });

    // 4. GTAO main pass - horizon search per slice, writing raw visibility and the edge mask
    // the denoise below needs. The SSGI permutation swaps the inner loop for a visibility
    // bitmask and additionally gathers screen radiance and a bent normal; both permutations
    // write the same attachment set.
    m_GTAOPassIndex = m_Graph.AddPass({
      .name = "GTAOPass",
      .inputs = {m_GTAODepth, m_GBuffer1, m_SSGIRadiance},
      .colorOutputs = {m_GTAOWorkingAO, m_GTAOEdges, m_SSGIWorking, m_SSGIBentWorking},
      .isEnabled = [this]() { return !IsPathTracingActive(); },
      .altName = "SSGIPass",
      .useAltName = [this]() { return b_SSGIEnabled && b_AOEnabled; },
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_AOEnabled) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& gtaoDepth = m_Graph.GetResource(m_GTAODepth);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& ssgiRadiance = m_Graph.GetResource(m_SSGIRadiance);

        auto& pipeline = b_SSGIEnabled
          ? m_PSOCache.Get(m_GTAOSSGIPipeline)
          : m_PSOCache.Get(m_GTAOPipeline);
        pipeline.Bind(ctx.cmd);
        m_GTAOPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          gtaoDepth.GetView(), gtaoDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_GTAOPassDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_GTAOPassDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          ssgiRadiance.GetView(), ssgiRadiance.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_GTAOPassDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad(ctx.cmd);
      }
    });

    // 5. GTAO denoise - edge-aware, driven by the edge mask rather than by depth. A single
    // pass is enough while TAA is integrating the result; switching denoising off is done
    // through denoiseBlurBeta, not by skipping this pass. The SSGI channels are filtered
    // here with the SAME weights as AO - a separate pass would let the screen part and the
    // fallback weight drift apart along silhouettes and fringe the composition.
    m_GTAODenoisePassIndex = m_Graph.AddPass({
      .name = "GTAODenoise",
      .inputs = {m_GTAOWorkingAO, m_GTAOEdges, m_SSGIWorking, m_SSGIBentWorking},
      .colorOutputs = {m_AOFinal, m_SSGIFinal, m_SSGIBentFinal},
      .isEnabled = [this]() { return !IsPathTracingActive(); },
      .altName = "SSGIDenoise",
      .useAltName = [this]() { return b_SSGIEnabled && b_AOEnabled; },
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_AOEnabled) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& workingAO = m_Graph.GetResource(m_GTAOWorkingAO);
        auto& edges = m_Graph.GetResource(m_GTAOEdges);
        auto& ssgiWorking = m_Graph.GetResource(m_SSGIWorking);
        auto& bentWorking = m_Graph.GetResource(m_SSGIBentWorking);

        auto& pipeline = m_PSOCache.Get(m_GTAODenoisePipeline);
        pipeline.Bind(ctx.cmd);
        m_GTAODenoiseDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          workingAO.GetView(), workingAO.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_GTAODenoiseDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          edges.GetView(), edges.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_GTAODenoiseDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          ssgiWorking.GetView(), ssgiWorking.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_GTAODenoiseDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          bentWorking.GetView(), bentWorking.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_GTAODenoiseDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad(ctx.cmd);
      }
    });

    m_LightCullPassIndex = m_Graph.AddPass({
      .name = "LightCull",
      .inputs = {m_MainDepth},
      .isCompute = true,
      // The tile list only ever feeds the deferred and forward transparent passes, and
      // both are off while the path tracer owns the frame - it picks its light per path
      // vertex out of the full LightBuffer instead.
      .isEnabled = [this]() { return !IsPathTracingActive(); },
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);

        m_LightCullInputDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto& pipeline = m_PSOCache.GetCompute(m_LightCullPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_LightCullInputDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_TileLightBuffer.GetDescriptorSet(currentFrame)}, 2);

        pipeline.Dispatch(ctx.cmd,
          m_TileLightBuffer.GetTileCountX(),
          m_TileLightBuffer.GetTileCountY(), 1);

        VkBufferMemoryBarrier bufferBarrier {};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.buffer = m_TileLightBuffer.GetBuffer(currentFrame);
        bufferBarrier.offset = 0;
        bufferBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0, 0, nullptr, 1, &bufferBarrier, 0, nullptr);
      }
    });

    // 6. Deferred Lighting - fullscreen IBL + analytical lights from G-buffer
    m_DeferredLightingPassIndex = m_Graph.AddPass({
      .name = "DeferredLighting",
      .inputs = {m_GBuffer0, m_GBuffer1, m_MainDepth, m_AOFinal, m_SSGIFinal, m_SSGIBentFinal},
      .colorOutputs = {m_LitColor},
      // The pass the path tracer replaces. LitColor - and SSRColor after it - therefore
      // hold the last raster frame while that path is effective; nothing reads either,
      // because every consumer is disabled alongside this one.
      .isEnabled = [this]() { return !IsPathTracingActive(); },
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);
        auto& aoFinal = m_Graph.GetResource(m_AOFinal);
        auto& ssgiFinal = m_Graph.GetResource(m_SSGIFinal);
        auto& ssgiBentFinal = m_Graph.GetResource(m_SSGIBentFinal);

        auto& pipeline = m_PSOCache.Get(m_DeferredLightingPipeline);
        pipeline.Bind(ctx.cmd);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          gbuffer0.GetView(), gbuffer0.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          aoFinal.GetView(), aoFinal.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          ssgiFinal.GetView(), ssgiFinal.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_DeferredLightingDescriptorSets[currentFrame].WriteCombinedImageSampler(5,
          ssgiBentFinal.GetView(), ssgiBentFinal.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_DeferredLightingDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_DeferredLightingLightDescriptorSets[currentFrame].Get()}, 2);
        pipeline.BindDescriptorSets(ctx.cmd, {m_IBLDescriptorSets[currentFrame].Get()}, 3);
        DrawQuad(ctx.cmd);
      }
    });

    m_HiZPassIndex = m_Graph.AddPass({
      .name = "HiZPass",
      .inputs = {m_MainDepth},
      .storageOutputs = {m_HiZResource},
      .isCompute = true,
      // Built for the SSR march alone, which the path tracer's bounce rays replace.
      .isEnabled = [this]() { return !IsPathTracingActive(); },
      .execute = [this](const RGExecuteContext& ctx) {
        uint32_t mipCount = m_Graph.GetResourceDesc(m_HiZResource).mipLevels;
        uint32_t w = m_Graph.GetExtent().width;
        uint32_t h = m_Graph.GetExtent().height;

        auto& hizPipeline = m_PSOCache.GetCompute(m_HiZPipeline);
        hizPipeline.Bind(ctx.cmd);

        for (uint32_t mip = 0; mip < mipCount; mip++)
        {
          uint32_t dstW = std::max(1u, w >> mip);
          uint32_t dstH = std::max(1u, h >> mip);

          uint32_t srcW = (mip == 0) ? w : std::max(1u, w >> (mip - 1));
          uint32_t srcH = (mip == 0) ? h : std::max(1u, h >> (mip - 1));

          struct { int srcMip; int dstWidth; int dstHeight; int srcWidth; int srcHeight; } pc;
          pc.srcMip = (mip == 0) ? -1 : static_cast<int>(mip - 1);
          pc.dstWidth = static_cast<int>(dstW);
          pc.dstHeight = static_cast<int>(dstH);
          pc.srcWidth = static_cast<int>(srcW);
          pc.srcHeight = static_cast<int>(srcH);

          hizPipeline.BindDescriptorSets(ctx.cmd, {m_HiZDescriptorSets[mip].Get()}, 0);
          hizPipeline.PushConstants(ctx.cmd, &pc);
          hizPipeline.Dispatch(ctx.cmd, (dstW + 15) / 16, (dstH + 15) / 16, 1);

          if (mip < mipCount - 1)
          {
            VkImageMemoryBarrier imgBarrier{};
            imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imgBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            imgBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            imgBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imgBarrier.image = m_Graph.GetResource(m_HiZResource).GetImage();
            imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgBarrier.subresourceRange.baseMipLevel = mip;
            imgBarrier.subresourceRange.levelCount = 1;
            imgBarrier.subresourceRange.baseArrayLayer = 0;
            imgBarrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(ctx.cmd,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              0, 0, nullptr, 0, nullptr, 1, &imgBarrier);
          }
        }
      }
    });

    m_SSRPassIndex = m_Graph.AddPass({
      .name = "SSRPass",
      .inputs = {m_LitColor, m_MainDepth, m_GBuffer1, m_GBuffer0, m_HiZResource},
      .colorOutputs = {m_SSRColor},
      // Traced reflections are what the path tracer's specular lobe produces, so the
      // screen space approximation has nothing to add and no consumer left.
      .isEnabled = [this]() { return !IsPathTracingActive(); },
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        auto& litColor = m_Graph.GetResource(m_LitColor);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& hiZ = m_Graph.GetResource(m_HiZResource);

        auto& pipeline = m_PSOCache.Get(m_SSRPipeline);
        pipeline.Bind(ctx.cmd);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          litColor.GetView(), litColor.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          gbuffer0.GetView(), gbuffer0.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SSRPassDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          hiZ.GetView(), hiZ.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SSRPassDescriptorSets[currentFrame].Get()}, 1);
        DrawQuad(ctx.cmd);
      }
    });


    // Transparency draws into the anti-aliasing input rather than on top of the resolve,
    // so it finally goes through the same temporal filter as opaque geometry. Depth is
    // tested against the opaque result but never written, which keeps m_MainDepth
    // opaque-only for every consumer that samples it afterwards.
    m_ForwardTransparentPassIndex = m_Graph.AddPass({
      .name = "ForwardTransparent",
      .inputs = {m_MainDepth},
      .colorOutputs = {m_SSRColor},
      .depthOutput = m_MainDepth,
      .clearColor = false,
      .clearDepth = false,
      // Transparent geometry is simply absent from a path traced frame in v1: it draws
      // into SSRColor, which nothing resolves any more, and the TLAS carries it under mask
      // 0x02 which the tracer never traces against. Stage 6 is what composites it - either
      // by tracing that mask or by drawing this pass over the resolved image.
      .isEnabled = [this]() { return !IsPathTracingActive(); },
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        DrawTransparent(ctx.cmd, m_Backend.GetCurrentFrameIndex(), *frame);
      }
    });

    m_TAAPassIndex = m_Graph.AddPass({
      .name = "TAAPass",
      .inputs = {m_SSRColor, m_TAAHistory1, m_MainVelocity, m_MainDepth},
      .colorOutputs = {m_TAAHistory0},
      .externalFramebuffer = true,
      // None also runs the pass: taa.frag falls back to a passthrough copy when the
      // taaEnabled uniform is 0, which keeps the resolved image written for downstream
      // consumers. Only the DLSS evaluate replaces the pass outright - and the path
      // tracer, whose accumulation image IS the resolve and whose input this pass would
      // read out of a stale SSRColor.
      .isEnabled = [this]() {
        return !IsDLSSMode(m_EffectiveAntialiasingMode) && !IsPathTracingActive();
      },
      .execute = [this](const RGExecuteContext& ctx) {
        auto& ssrColor = m_Graph.GetResource(m_SSRColor);
        auto historyReadHandle = m_TAAIndex == 0 ? m_TAAHistory1 : m_TAAHistory0;
        auto& historyPrev = m_Graph.GetResource(historyReadHandle);
        auto& velocity = m_Graph.GetResource(m_MainVelocity);
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& pipeline = m_PSOCache.Get(m_TAAPipeline);
        pipeline.Bind(ctx.cmd);
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          ssrColor.GetView(), ssrColor.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          historyPrev.GetView(), historyPrev.GetSampler(), historyPrev.GetLayout());
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          velocity.GetView(), velocity.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_TAADescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_TAADescriptorSets[currentFrame].Get()}, 1);
        DrawQuad(ctx.cmd);
      }
    });

    // Streamline's own upscale, the alternative to the pass above. Compute-flagged so
    // the graph runs it outside a VkRenderPass instance and puts the tagged inputs in
    // SHADER_READ_ONLY and the output in GENERAL before the callback records anything.
    m_DLSSEvaluatePassIndex = m_Graph.AddPass({
      .name = "DLSSEvaluate",
      .inputs = {m_SSRColor, m_MainDepth, m_MainVelocity},
      .storageOutputs = {m_DLSSOutput},
      .isCompute = true,
      // Super resolution upscales a rasterized frame, and there is none while the path
      // tracer owns it - the ray reconstruction pass below is the resolve that belongs to
      // a traced one, and the two are mutually exclusive by construction.
      .isEnabled = [this]() {
        return IsDLSSMode(m_EffectiveAntialiasingMode) && !IsPathTracingActive();
      },
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        RunDLSSEvaluate(ctx.cmd, *frame);
      }
    });

    // 11c. The path tracer's own resolve: denoise, upscale and anti-alias in one evaluate.
    // It sits in the same graph slot as the super resolution pass above and writes the same
    // image, so everything downstream keeps reading DLSSOutput and never learns which of
    // the two filled it. A separate pass rather than a branch inside that one because the
    // tagged inputs are different: the noisy sample and the four guides instead of the
    // rasterized SSRColor, and declaring those in the raster pass would only make the graph
    // order and barrier frames that never touch them.
    m_DLSSRayReconstructionPassIndex = m_Graph.AddPass({
      .name = "DLSSRayReconstruction",
      .inputs = {m_PathTraceNoisy, m_MainDepth, m_MainVelocity, m_PTDiffuseAlbedo,
        m_PTSpecularAlbedo, m_PTNormalRoughness, m_PTHitDistance},
      .storageOutputs = {m_DLSSOutput},
      .isCompute = true,
      .isEnabled = [this]() { return IsRayReconstructionResolve(); },
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        RunRayReconstructionEvaluate(ctx.cmd, *frame);
      }
    });

    m_BloomPassIndex = m_Graph.AddPass({
      .name = "BloomPass",
      // In TAA modes this sources the previous frame's resolved image, not the raw
      // jittered lit frame, so unfiltered flicker never reaches TAA. Both histories are
      // declared as inputs: History0's static writer orders this pass after the resolve,
      // History1 covers the ping-pong buffer actually sampled on alternate frames so it
      // still gets a barrier. The DLSS output orders it after the evaluate, and the path
      // tracer's accumulation image after the trace.
      .inputs = {m_TAAHistory0, m_TAAHistory1, m_DLSSOutput, m_PathTraceAccum},
      .isCompute = true,
      .resolution = RGResolution::Output,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_BloomEnabled) return;

        // Keyed on the handle the resolve actually landed in rather than on the mode, so
        // the path tracer picks index 2 or index 3 by the same rule that decides what the
        // tone map reads - there is no second place where the two could disagree.
        // GetPreviousResolvedColorHandle is the right question: in TAA modes bloom sources
        // the PREVIOUS frame's history so unfiltered flicker never reaches the resolve,
        // while DLSS keeps one output image that this frame's evaluate has already
        // written, and so does the path tracer's accumulation image.
        RGHandle bloomSrc = GetPreviousResolvedColorHandle();
        uint32_t bloomSrcSet = 0;
        if (bloomSrc == m_DLSSOutput)
          bloomSrcSet = BLOOM_SRC_DLSS;
        else if (bloomSrc == m_PathTraceAccum)
          bloomSrcSet = BLOOM_SRC_PATHTRACE;
        else
          bloomSrcSet = bloomSrc == m_TAAHistory1 ? 1 : 0;

        uint32_t baseW = m_Graph.GetOutputExtent().width;
        uint32_t baseH = m_Graph.GetOutputExtent().height;
        uint32_t mipCount = BLOOM_MIP_COUNT;

        TransitionImageLayout(ctx.cmd, m_BloomImage.GetImage(),
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount);

        auto& downPipeline = m_PSOCache.GetCompute(m_BloomDownsamplePipeline);
        downPipeline.Bind(ctx.cmd);

        for (uint32_t mip = 0; mip < mipCount; mip++)
        {
          uint32_t srcW = (mip == 0) ? baseW : std::max(1u, baseW / 2 >> (mip - 1));
          uint32_t srcH = (mip == 0) ? baseH : std::max(1u, baseH / 2 >> (mip - 1));
          uint32_t dstW = std::max(1u, baseW / 2 >> mip);
          uint32_t dstH = std::max(1u, baseH / 2 >> mip);

          BloomPushConstants pc;
          pc.srcWidth = static_cast<int>(srcW);
          pc.srcHeight = static_cast<int>(srcH);
          pc.mipLevel = static_cast<int>(mip);
          pc.filterRadius = 0.0f;
          pc.threshold = m_BloomThreshold;
          pc.softKnee = m_BloomSoftKnee;

          downPipeline.BindDescriptorSets(ctx.cmd, {mip == 0
            ? m_BloomHistorySrcSets[bloomSrcSet].Get()
            : m_BloomDownsampleDescriptorSets[mip].Get()}, 0);
          downPipeline.PushConstants(ctx.cmd, &pc);
          downPipeline.Dispatch(ctx.cmd, (dstW + 15) / 16, (dstH + 15) / 16, 1);

          VkImageMemoryBarrier imgBarrier{};
          imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          imgBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
          imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          imgBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
          imgBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
          imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          imgBarrier.image = m_BloomImage.GetImage();
          imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          imgBarrier.subresourceRange.baseMipLevel = mip;
          imgBarrier.subresourceRange.levelCount = 1;
          imgBarrier.subresourceRange.baseArrayLayer = 0;
          imgBarrier.subresourceRange.layerCount = 1;
          vkCmdPipelineBarrier(ctx.cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imgBarrier);
        }

        auto& upPipeline = m_PSOCache.GetCompute(m_BloomUpsamplePipeline);
        upPipeline.Bind(ctx.cmd);

        for (uint32_t i = 0; i < mipCount - 1; i++)
        {
          uint32_t dstMip = mipCount - 2 - i;
          uint32_t dstW = std::max(1u, baseW / 2 >> dstMip);
          uint32_t dstH = std::max(1u, baseH / 2 >> dstMip);

          BloomPushConstants pc;
          pc.srcWidth = static_cast<int>(dstW);
          pc.srcHeight = static_cast<int>(dstH);
          pc.mipLevel = static_cast<int>(dstMip);
          pc.filterRadius = 1.0f / float(dstW);

          upPipeline.BindDescriptorSets(ctx.cmd, {m_BloomUpsampleDescriptorSets[i].Get()}, 0);
          upPipeline.PushConstants(ctx.cmd, &pc);
          upPipeline.Dispatch(ctx.cmd, (dstW + 15) / 16, (dstH + 15) / 16, 1);

          VkImageMemoryBarrier imgBarrier{};
          imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          imgBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
          imgBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
          imgBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
          imgBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
          imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          imgBarrier.image = m_BloomImage.GetImage();
          imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          imgBarrier.subresourceRange.baseMipLevel = dstMip;
          imgBarrier.subresourceRange.levelCount = 1;
          imgBarrier.subresourceRange.baseArrayLayer = 0;
          imgBarrier.subresourceRange.layerCount = 1;
          vkCmdPipelineBarrier(ctx.cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &imgBarrier);
        }

        // Final transition to SHADER_READ_ONLY for the tonemap pass to sample
        TransitionImageLayout(ctx.cmd, m_BloomImage.GetImage(),
          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount);
        m_BloomImage.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
    });

    m_HistogramPassIndex = m_Graph.AddPass({
      .name = "HistogramBuild",
      // Slot 0 is retargeted every frame to whichever image the resolve wrote; the DLSS
      // output and the path tracer's accumulation image are also named statically so this
      // pass is ordered after the evaluate and after the trace respectively. A retargeted
      // slot moves the barrier, not the topological edge, which is compiled once.
      .inputs = {m_TAAHistory0, m_DLSSOutput, m_PathTraceAccum},
      .isCompute = true,
      .resolution = RGResolution::Output,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_AutoExposureEnabled || m_GlobalFrameIndex < 4) return;

        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        vkCmdFillBuffer(ctx.cmd, m_HistogramBuffer.Get(), 0,
          HISTOGRAM_BIN_COUNT * sizeof(uint32_t), 0);

        VkBufferMemoryBarrier clearBarrier {};
        clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        clearBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        clearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        clearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        clearBarrier.buffer = m_HistogramBuffer.Get();
        clearBarrier.offset = 0;
        clearBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          0, 0, nullptr, 1, &clearBarrier, 0, nullptr);

        // Write HDR texture into descriptor (input[0] set via SetPassInput for ping-pong)
        auto& resolved = m_Graph.GetResource(GetResolvedColorHandle());
        m_HistogramPassDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          resolved.GetView(), resolved.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto& pipeline = m_PSOCache.GetCompute(m_ExposureHistogramPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_HistogramPassDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_HistogramOutputDescriptorSet.Get()}, 2);

        uint32_t groupsX = (m_FrameUniformBuffer.uniforms.outputWidth + 15) / 16;
        uint32_t groupsY = (m_FrameUniformBuffer.uniforms.outputHeight + 15) / 16;
        pipeline.Dispatch(ctx.cmd, groupsX, groupsY, 1);

        VkBufferMemoryBarrier histBarrier {};
        histBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        histBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        histBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        histBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        histBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        histBarrier.buffer = m_HistogramBuffer.Get();
        histBarrier.offset = 0;
        histBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          0, 0, nullptr, 1, &histBarrier, 0, nullptr);
      }
    });

    m_ExposureAdaptPassIndex = m_Graph.AddPass({
      .name = "ExposureAdapt",
      .isCompute = true,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_AutoExposureEnabled || m_GlobalFrameIndex < 4)
        {
          float unity = 1.0f;
          m_ExposureBuffer.Update(0, &unity, sizeof(float));
          return;
        }

        auto currentFrame = m_Backend.GetCurrentFrameIndex();

        ExposureAdaptPushConstants pc;
        pc.lowPercentile = m_LowPercentile;
        pc.highPercentile = m_HighPercentile;
        pc.adaptSpeedUp = m_AdaptSpeedUp;
        pc.adaptSpeedDown = m_AdaptSpeedDown;
        pc.deltaTime = std::min(m_DeltaTime, 0.1f);

        auto& pipeline = m_PSOCache.GetCompute(m_ExposureAdaptPipeline);
        pipeline.Bind(ctx.cmd);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_ExposureAdaptDescriptorSets[currentFrame].Get()}, 1);
        pipeline.PushConstants(ctx.cmd, &pc);
        pipeline.Dispatch(ctx.cmd, 1, 1, 1);

        VkBufferMemoryBarrier expBarrier {};
        expBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        expBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        expBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        expBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expBarrier.buffer = m_ExposureBuffer.Get();
        expBarrier.offset = 0;
        expBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(ctx.cmd,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          0, 0, nullptr, 1, &expBarrier, 0, nullptr);
      }
    });

#ifdef YA_EDITOR
    // Entity ids for picking. One texel per pixel, cleared to zero, so the pass stores
    // id + 1 and a zero always reads back as "nothing here".
    m_PickId = m_Graph.CreateResource({
      .name = "pickId",
      .format = VK_FORMAT_R32_UINT,
      .filter = VK_FILTER_NEAREST
    });

    // Rasterizes entity ids against the depth already produced (no depth write, GEQUAL)
    // for free occlusion. Declaring MainDepth as a depth output pins it into the write
    // chain so the depth resampled for the gizmos below is the one this pass saw.
    m_PickIdPassIndex = m_Graph.AddPass({
      .name = "PickIdPass",
      .colorOutputs = {m_PickId},
      .depthOutput = m_MainDepth,
      .clearColor = true,
      .clearDepth = false,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_PickThisFrame) return;
        auto* frame = static_cast<FrameContext*>(ctx.userData);
        DrawPickIds(ctx.cmd, m_Backend.GetCurrentFrameIndex(), *frame);
      }
    });

    // Pulls the clicked texel into a readback buffer. Compute-flagged because transfer
    // commands cannot be recorded inside a render pass.
    m_PickCopyPassIndex = m_Graph.AddPass({
      .name = "PickIdCopy",
      .inputs = {m_PickId},
      .isCompute = true,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_PickThisFrame) return;
        CopyPickId(ctx.cmd);
      }
    });

    // Scene compose pass - tone mapping to offscreen texture for editor viewport
    m_SceneColor = m_Graph.CreateResource({
      .name = "sceneColor",
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .additionalUsage = VK_IMAGE_USAGE_SAMPLED_BIT,
      .resolution = RGResolution::Output
    });

    m_ComposeDepth = m_Graph.CreateResource({
      .name = "composeDepth",
      .format = VK_FORMAT_D32_SFLOAT,
      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
      .resolution = RGResolution::Output
    });

    // Point-resamples the scene depth to output resolution so the gizmo passes, which
    // draw into the output-res scene color, still occlude against real geometry.
    m_SceneDepthUpscalePassIndex = m_Graph.AddPass({
      .name = "SceneDepthUpscale",
      .inputs = {m_MainDepth},
      .depthOutput = m_ComposeDepth,
      .clearDepth = true,
      .depthOnly = true,
      .isEnabled = [this]() { return b_GizmosEnabled; },
      .execute = [this](const RGExecuteContext& ctx) {
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& mainDepth = m_Graph.GetResource(m_MainDepth);

        auto& pipeline = m_PSOCache.Get(m_DepthCopyPipeline);
        pipeline.Bind(ctx.cmd);
        m_DepthCopyDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          mainDepth.GetView(), mainDepth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_DepthCopyDescriptorSets[currentFrame].Get()}, 0);
        DrawQuad(ctx.cmd);
      }
    });

    m_SceneComposePassIndex = m_Graph.AddPass({
      .name = "SceneComposePass",
      .inputs = {m_TAAHistory0, m_AOFinal, m_GBuffer0, m_GBuffer1, m_MainVelocity, m_SSRColor,
        m_SSGIFinal, m_SSGIRadiance, m_DLSSOutput, m_RTDebug, m_PathTraceNoisy, m_PathTraceAccum,
        m_PTDiffuseAlbedo, m_PTSpecularAlbedo, m_PTNormalRoughness},
      .colorOutputs = {m_SceneColor},
      .execute = [this](const RGExecuteContext& ctx) {
        auto& historyCurrent = m_Graph.GetResource(GetResolvedColorHandle());
        auto& rtDebug = m_Graph.GetResource(m_RTDebug);
        auto& ptNoisy = m_Graph.GetResource(m_PathTraceNoisy);
        auto& ptAccum = m_Graph.GetResource(m_PathTraceAccum);
        auto& ptDiffuseAlbedo = m_Graph.GetResource(m_PTDiffuseAlbedo);
        auto& ptSpecularAlbedo = m_Graph.GetResource(m_PTSpecularAlbedo);
        auto& ptNormalRoughness = m_Graph.GetResource(m_PTNormalRoughness);
        auto& aoFinal = m_Graph.GetResource(m_AOFinal);
        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& velocity = m_Graph.GetResource(m_MainVelocity);
        auto& preResolve = m_Graph.GetResource(m_SSRColor);
        auto& ssgiFinal = m_Graph.GetResource(m_SSGIFinal);
        auto& ssgiRadiance = m_Graph.GetResource(m_SSGIRadiance);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& pipeline = m_PSOCache.Get(m_QuadPipeline);
        pipeline.Bind(ctx.cmd);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          historyCurrent.GetView(), historyCurrent.GetSampler(), historyCurrent.GetLayout());
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          aoFinal.GetView(), aoFinal.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          gbuffer0.GetView(), gbuffer0.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          velocity.GetView(), velocity.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(5,
          preResolve.GetView(), preResolve.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(6,
          ssgiFinal.GetView(), ssgiFinal.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(7,
          ssgiRadiance.GetView(), ssgiRadiance.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(8,
          rtDebug.GetView(), rtDebug.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(9,
          ptNoisy.GetView(), ptNoisy.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(10,
          ptAccum.GetView(), ptAccum.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(11,
          ptDiffuseAlbedo.GetView(), ptDiffuseAlbedo.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(12,
          ptSpecularAlbedo.GetView(), ptSpecularAlbedo.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(13,
          ptNormalRoughness.GetView(), ptNormalRoughness.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SwapChainDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_ExposureReadDescriptorSets[currentFrame].Get()}, 2);
        pipeline.BindDescriptorSets(ctx.cmd, {m_BloomReadDescriptorSet.Get()}, 3);
        DrawQuad(ctx.cmd);
      }
    });

    // Gizmo scene pass - depth-tested gizmos (probe volumes) rendered against scene depth
    m_GizmoScenePassIndex = m_Graph.AddPass({
      .name = "GizmoScenePass",
      .colorOutputs = {m_SceneColor},
      .depthOutput = m_ComposeDepth,
      .clearColor = false,
      .clearDepth = false,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_GizmosEnabled) return;
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        m_GizmoRenderer.FlushDepthTested(ctx.cmd, m_FrameUniformBuffer.GetDescriptorSet(currentFrame));
      }
    });

    // Gizmo overlay pass - manipulators rendered on top (depth cleared so gizmos only test against each other)
    m_GizmoPassIndex = m_Graph.AddPass({
      .name = "GizmoPass",
      .colorOutputs = {m_SceneColor},
      .depthOutput = m_ComposeDepth,
      .clearColor = false,
      .clearDepth = true,
      .execute = [this](const RGExecuteContext& ctx) {
        if (!b_GizmosEnabled) return;
        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        m_GizmoRenderer.FlushOverlay(ctx.cmd, m_FrameUniformBuffer.GetDescriptorSet(currentFrame));
      }
    });

    // Swapchain pass - editor mode: ImGui only (scene displayed via viewport panel)
    m_SwapchainPassIndex = m_Graph.AddPass({
      .name = "SwapchainPass",
      .inputs = {m_SceneColor},
      .colorOutputs = {},
      .externalFramebuffer = true,
      .externalFormat = swapFormat,
      .finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .resolution = RGResolution::Output,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (frame->renderUI) frame->renderUI(frame->renderUIData);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx.cmd);
      }
    });
#else
    // Swapchain pass - production mode: tone mapping + ImGui overlay
    m_SwapchainPassIndex = m_Graph.AddPass({
      .name = "SwapchainPass",
      .inputs = {m_TAAHistory0, m_AOFinal, m_GBuffer0, m_GBuffer1, m_MainVelocity, m_SSRColor,
        m_SSGIFinal, m_SSGIRadiance, m_DLSSOutput, m_RTDebug, m_PathTraceNoisy, m_PathTraceAccum,
        m_PTDiffuseAlbedo, m_PTSpecularAlbedo, m_PTNormalRoughness},
      .colorOutputs = {},
      .externalFramebuffer = true,
      .externalFormat = swapFormat,
      .finalColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .resolution = RGResolution::Output,
      .execute = [this](const RGExecuteContext& ctx) {
        auto* frame = static_cast<FrameContext*>(ctx.userData);

        auto& historyCurrent = m_Graph.GetResource(GetResolvedColorHandle());
        auto& aoFinal = m_Graph.GetResource(m_AOFinal);
        auto& gbuffer0 = m_Graph.GetResource(m_GBuffer0);
        auto& gbuffer1 = m_Graph.GetResource(m_GBuffer1);
        auto& velocity = m_Graph.GetResource(m_MainVelocity);
        auto& preResolve = m_Graph.GetResource(m_SSRColor);
        auto& ssgiFinal = m_Graph.GetResource(m_SSGIFinal);
        auto& ssgiRadiance = m_Graph.GetResource(m_SSGIRadiance);
        auto& rtDebug = m_Graph.GetResource(m_RTDebug);
        auto& ptNoisy = m_Graph.GetResource(m_PathTraceNoisy);
        auto& ptAccum = m_Graph.GetResource(m_PathTraceAccum);
        auto& ptDiffuseAlbedo = m_Graph.GetResource(m_PTDiffuseAlbedo);
        auto& ptSpecularAlbedo = m_Graph.GetResource(m_PTSpecularAlbedo);
        auto& ptNormalRoughness = m_Graph.GetResource(m_PTNormalRoughness);

        auto currentFrame = m_Backend.GetCurrentFrameIndex();
        auto& pipeline = m_PSOCache.Get(m_QuadPipeline);
        pipeline.Bind(ctx.cmd);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(0,
          historyCurrent.GetView(), historyCurrent.GetSampler(), historyCurrent.GetLayout());
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(1,
          aoFinal.GetView(), aoFinal.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(2,
          gbuffer0.GetView(), gbuffer0.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(3,
          gbuffer1.GetView(), gbuffer1.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(4,
          velocity.GetView(), velocity.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(5,
          preResolve.GetView(), preResolve.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(6,
          ssgiFinal.GetView(), ssgiFinal.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(7,
          ssgiRadiance.GetView(), ssgiRadiance.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(8,
          rtDebug.GetView(), rtDebug.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(9,
          ptNoisy.GetView(), ptNoisy.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(10,
          ptAccum.GetView(), ptAccum.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(11,
          ptDiffuseAlbedo.GetView(), ptDiffuseAlbedo.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(12,
          ptSpecularAlbedo.GetView(), ptSpecularAlbedo.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_SwapChainDescriptorSets[currentFrame].WriteCombinedImageSampler(13,
          ptNormalRoughness.GetView(), ptNormalRoughness.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pipeline.BindDescriptorSets(ctx.cmd, {m_FrameUniformBuffer.GetDescriptorSet(currentFrame)}, 0);
        pipeline.BindDescriptorSets(ctx.cmd, {m_SwapChainDescriptorSets[currentFrame].Get()}, 1);
        pipeline.BindDescriptorSets(ctx.cmd, {m_ExposureReadDescriptorSets[currentFrame].Get()}, 2);
        pipeline.BindDescriptorSets(ctx.cmd, {m_BloomReadDescriptorSet.Get()}, 3);
        DrawQuad(ctx.cmd);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (frame->renderUI) frame->renderUI(frame->renderUIData);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ctx.cmd);
      }
    });
#endif

    m_Graph.Compile();
  }

  void Render::CreateTAAFramebuffers()
  {
    auto& ctx = m_Backend.GetContext();

    // Create depth image shared by both TAA framebuffers
    ImageDesc depthDesc;
    depthDesc.width = m_Graph.GetExtent().width;
    depthDesc.height = m_Graph.GetExtent().height;
    depthDesc.format = VK_FORMAT_D32_SFLOAT;
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthDesc.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    m_TAADepth.Init(ctx, depthDesc);

    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_IMAGE,
      m_TAADepth.GetImage(), "TAA Depth");
    YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_IMAGE_VIEW,
      m_TAADepth.GetView(), "TAA Depth View");

    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);

    for (uint32_t i = 0; i < 2; i++)
    {
      RGHandle historyHandle = (i == 0) ? m_TAAHistory0 : m_TAAHistory1;

      VkImageView views[2] = {
        m_Graph.GetResource(historyHandle).GetView(),
        m_TAADepth.GetView()
      };

      VkFramebufferCreateInfo fbInfo{};
      fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbInfo.renderPass = taaRP;
      fbInfo.attachmentCount = 2;
      fbInfo.pAttachments = views;
      fbInfo.width = m_Graph.GetExtent().width;
      fbInfo.height = m_Graph.GetExtent().height;
      fbInfo.layers = 1;

      if (vkCreateFramebuffer(ctx.device, &fbInfo, nullptr, &m_TAAFramebuffers[i]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create TAA framebuffer %d", i);
        throw std::runtime_error("Failed to create TAA framebuffer!");
      }

      YA_DEBUG_NAMEF(ctx.device, VK_OBJECT_TYPE_FRAMEBUFFER,
        m_TAAFramebuffers[i], "TAA FB %u", i);
    }
  }

  void Render::ClearHistoryBuffers()
  {
    // The cleared history is a perfectly valid black image; without this flag SSGI
    // would reproject it with full confidence and gather darkness for a frame
    // instead of falling back to the volumes.
    b_SSGIInvalidatePending = true;

    VkRenderPass taaRP = m_Graph.GetPassRenderPass(m_TAAPassIndex);
    auto extent = m_Graph.GetExtent();

    auto cmd = m_Backend.GetCommandBuffer().BeginSingleTimeCommands();

    TransitionImageLayout(cmd, m_TAADepth.GetImage(),
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_ASPECT_DEPTH_BIT);

    for (uint32_t i = 0; i < 2; i++)
    {
      auto handle = (i == 0) ? m_TAAHistory0 : m_TAAHistory1;
      auto& image = m_Graph.GetResource(handle);

      // initialLayout=UNDEFINED is fine here since we don't need previous contents
      VkClearValue clearValues[2] = {};
      clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
      clearValues[1].depthStencil = {0.0f, 0};

      VkRenderPassBeginInfo rpBegin{};
      rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpBegin.renderPass = taaRP;
      rpBegin.framebuffer = m_TAAFramebuffers[i];
      rpBegin.renderArea.extent = extent;
      rpBegin.clearValueCount = 2;
      rpBegin.pClearValues = clearValues;

      vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdEndRenderPass(cmd);

      // After render pass with finalLayout=COLOR_ATTACHMENT_OPTIMAL,
      // transition to SHADER_READ_ONLY for first frame's use
      TransitionImageLayout(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      image.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      m_Graph.SetResourceLayout(handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    m_Backend.GetCommandBuffer().EndSingleTimeCommands(cmd);
  }

  void Render::ClearPathTraceOutputs()
  {
    // None of the three is ever cleared by a pass - the sample index resets the mean by
    // rewriting it - and the path tracing render path either resolves out of the
    // accumulation image or hands the noisy one and the hit distance to ray
    // reconstruction. A frame that resolves or tags them before the trace has run once (an
    // empty scene, a failed TLAS build) would otherwise present, or denoise against, raw
    // allocation contents; the tonemap pass samples two of them from statically accessed
    // bindings on every device besides.
    const VkClearColorValue black = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
    VkImageSubresourceRange range {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    auto cmd = m_Backend.GetCommandBuffer().BeginSingleTimeCommands();

    for (RGHandle handle : { m_PathTraceNoisy, m_PathTraceAccum, m_PTHitDistance })
    {
      auto& image = m_Graph.GetResource(handle);

      TransitionImageLayout(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdClearColorImage(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
      TransitionImageLayout(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      image.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      m_Graph.SetResourceLayout(handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    m_Backend.GetCommandBuffer().EndSingleTimeCommands(cmd);
  }
}
