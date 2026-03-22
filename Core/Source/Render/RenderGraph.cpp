#include "RenderGraph.h"

#include "RenderContext.h"
#include "ImageBarrier.h"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace YAEngine
{
  void RenderGraph::Init(const RenderContext& ctx, VkExtent2D extent)
  {
    m_Ctx = &ctx;
    m_Extent = extent;
  }

  void RenderGraph::Destroy()
  {
    for (auto& pass : m_Passes)
    {
      if (pass.framebuffer != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(m_Ctx->device, pass.framebuffer, nullptr);
        pass.framebuffer = VK_NULL_HANDLE;
      }
      if (pass.renderPass != VK_NULL_HANDLE)
      {
        vkDestroyRenderPass(m_Ctx->device, pass.renderPass, nullptr);
        pass.renderPass = VK_NULL_HANDLE;
      }
      if (pass.privateDepth.IsValid())
      {
        pass.privateDepth.Destroy(*m_Ctx);
      }
    }

    for (auto& res : m_Resources)
    {
      if (res.managed && res.image.IsValid())
      {
        res.image.Destroy(*m_Ctx);
      }
    }

    m_Resources.clear();
    m_Passes.clear();
    m_ExecutionOrder.clear();
    m_CurrentLayouts.clear();
    m_Compiled = false;
  }

  RGHandle RenderGraph::CreateResource(const RGResourceDesc& desc)
  {
    RGHandle handle = static_cast<RGHandle>(m_Resources.size());
    Resource res;
    res.desc = desc;
    res.managed = true;
    m_Resources.push_back(std::move(res));
    return handle;
  }

  RGHandle RenderGraph::ImportResource(const std::string& name, VulkanImage& image)
  {
    RGHandle handle = static_cast<RGHandle>(m_Resources.size());
    Resource res;
    res.desc.name = name;
    res.desc.format = image.GetFormat();
    res.externalImage = &image;
    res.managed = false;
    m_Resources.push_back(std::move(res));
    return handle;
  }

  uint32_t RenderGraph::AddPass(const RGPassInfo& info)
  {
    uint32_t index = static_cast<uint32_t>(m_Passes.size());
    CompiledPass pass;
    pass.info = info;
    m_Passes.push_back(std::move(pass));
    return index;
  }

  void RenderGraph::Compile()
  {
    DetermineResourceUsage();
    AllocateResources();
    BuildRenderPasses();
    BuildFramebuffers();
    m_ExecutionOrder = TopologicalSort();

    m_CurrentLayouts.resize(m_Resources.size(), VK_IMAGE_LAYOUT_UNDEFINED);
    m_Compiled = true;
  }

  void RenderGraph::DetermineResourceUsage()
  {
    for (auto& res : m_Resources)
    {
      res.usage = res.desc.additionalUsage;
    }

    for (const auto& pass : m_Passes)
    {
      for (auto handle : pass.info.inputs)
      {
        m_Resources[handle].usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
      }
      for (auto handle : pass.info.colorOutputs)
      {
        m_Resources[handle].usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      }
      if (pass.info.depthOutput != RG_INVALID_HANDLE)
      {
        m_Resources[pass.info.depthOutput].usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
      }
    }
  }

  void RenderGraph::AllocateResources()
  {
    for (auto& res : m_Resources)
    {
      if (!res.managed) continue;

      uint32_t width = std::max(1u, static_cast<uint32_t>(m_Extent.width * res.desc.widthScale));
      uint32_t height = std::max(1u, static_cast<uint32_t>(m_Extent.height * res.desc.heightScale));

      ImageDesc imageDesc;
      imageDesc.width = width;
      imageDesc.height = height;
      imageDesc.format = res.desc.format;
      imageDesc.usage = res.usage;
      imageDesc.aspectMask = res.desc.aspect;

      bool needsSampler = (res.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;

      if (needsSampler)
      {
        SamplerDesc samplerDesc;
        if (res.desc.aspect == VK_IMAGE_ASPECT_DEPTH_BIT)
        {
          samplerDesc.magFilter = VK_FILTER_NEAREST;
          samplerDesc.minFilter = VK_FILTER_NEAREST;
        }
        res.image.Init(*m_Ctx, imageDesc, &samplerDesc);
      }
      else
      {
        res.image.Init(*m_Ctx, imageDesc);
      }
    }
  }

  void RenderGraph::BuildRenderPasses()
  {
    for (auto& pass : m_Passes)
    {
      std::vector<VkAttachmentDescription> attachments;
      std::vector<VkAttachmentReference> colorRefs;

      // Determine primary color format
      VkFormat primaryFormat;
      if (!pass.info.colorOutputs.empty())
      {
        primaryFormat = m_Resources[pass.info.colorOutputs[0]].desc.format;
      }
      else
      {
        primaryFormat = pass.info.externalFormat;
      }

      // Attachment 0: primary color
      {
        VkAttachmentDescription att{};
        att.format = primaryFormat;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = pass.info.clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = pass.info.clearColor
          ? VK_IMAGE_LAYOUT_UNDEFINED
          : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.finalLayout = pass.info.finalColorLayout;
        attachments.push_back(att);

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs.push_back(ref);
      }

      // Attachment 1: depth
      {
        VkAttachmentDescription att{};
        att.format = VK_FORMAT_D32_SFLOAT;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = pass.info.clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = pass.info.clearDepth
          ? VK_IMAGE_LAYOUT_UNDEFINED
          : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments.push_back(att);
      }

      VkAttachmentReference depthRef{};
      depthRef.attachment = 1;
      depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

      // Attachments 2+: additional color outputs
      for (size_t i = 1; i < pass.info.colorOutputs.size(); i++)
      {
        VkFormat fmt = m_Resources[pass.info.colorOutputs[i]].desc.format;

        VkAttachmentDescription att{};
        att.format = fmt;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = pass.info.clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = pass.info.clearColor
          ? VK_IMAGE_LAYOUT_UNDEFINED
          : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments.push_back(att);

        VkAttachmentReference ref{};
        ref.attachment = static_cast<uint32_t>(attachments.size() - 1);
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs.push_back(ref);
      }

      VkSubpassDescription subpass{};
      subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
      subpass.pColorAttachments = colorRefs.data();
      subpass.pDepthStencilAttachment = &depthRef;

      VkSubpassDependency dep{};
      dep.srcSubpass = VK_SUBPASS_EXTERNAL;
      dep.dstSubpass = 0;
      dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
      dep.srcAccessMask = 0;
      dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
      dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

      VkRenderPassCreateInfo rpInfo{};
      rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
      rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
      rpInfo.pAttachments = attachments.data();
      rpInfo.subpassCount = 1;
      rpInfo.pSubpasses = &subpass;
      rpInfo.dependencyCount = 1;
      rpInfo.pDependencies = &dep;

      if (vkCreateRenderPass(m_Ctx->device, &rpInfo, nullptr, &pass.renderPass) != VK_SUCCESS)
      {
        throw std::runtime_error("Failed to create render pass: " + pass.info.name);
      }
    }
  }

  void RenderGraph::BuildFramebuffers()
  {
    for (auto& pass : m_Passes)
    {
      if (pass.info.externalFramebuffer) continue;
      if (pass.info.colorOutputs.empty()) continue;

      // Create private depth if no managed depth output
      if (pass.info.depthOutput == RG_INVALID_HANDLE)
      {
        ImageDesc depthDesc;
        depthDesc.width = m_Extent.width;
        depthDesc.height = m_Extent.height;
        depthDesc.format = VK_FORMAT_D32_SFLOAT;
        depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthDesc.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        pass.privateDepth.Init(*m_Ctx, depthDesc);
      }

      // Build image view array: [color0, depth, color1, color2, ...]
      std::vector<VkImageView> views;

      // Color 0
      views.push_back(ResolveResource(pass.info.colorOutputs[0]).GetView());

      // Depth
      if (pass.info.depthOutput != RG_INVALID_HANDLE)
      {
        views.push_back(ResolveResource(pass.info.depthOutput).GetView());
      }
      else
      {
        views.push_back(pass.privateDepth.GetView());
      }

      // Additional colors
      for (size_t i = 1; i < pass.info.colorOutputs.size(); i++)
      {
        views.push_back(ResolveResource(pass.info.colorOutputs[i]).GetView());
      }

      VkFramebufferCreateInfo fbInfo{};
      fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbInfo.renderPass = pass.renderPass;
      fbInfo.attachmentCount = static_cast<uint32_t>(views.size());
      fbInfo.pAttachments = views.data();
      fbInfo.width = m_Extent.width;
      fbInfo.height = m_Extent.height;
      fbInfo.layers = 1;

      if (vkCreateFramebuffer(m_Ctx->device, &fbInfo, nullptr, &pass.framebuffer) != VK_SUCCESS)
      {
        throw std::runtime_error("Failed to create framebuffer: " + pass.info.name);
      }
    }
  }

  std::vector<uint32_t> RenderGraph::TopologicalSort() const
  {
    uint32_t n = static_cast<uint32_t>(m_Passes.size());

    // Map: resource → pass that writes it
    std::unordered_map<RGHandle, uint32_t> writers;
    for (uint32_t i = 0; i < n; i++)
    {
      for (auto handle : m_Passes[i].info.colorOutputs)
      {
        writers[handle] = i;
      }
      if (m_Passes[i].info.depthOutput != RG_INVALID_HANDLE)
      {
        writers[m_Passes[i].info.depthOutput] = i;
      }
    }

    // Build adjacency: if pass B reads a resource written by pass A → edge A→B
    std::vector<std::vector<uint32_t>> adj(n);
    std::vector<uint32_t> inDegree(n, 0);

    for (uint32_t i = 0; i < n; i++)
    {
      for (auto input : m_Passes[i].info.inputs)
      {
        auto it = writers.find(input);
        if (it != writers.end() && it->second != i)
        {
          adj[it->second].push_back(i);
          inDegree[i]++;
        }
      }
    }

    // Kahn's BFS
    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < n; i++)
    {
      if (inDegree[i] == 0) q.push(i);
    }

    std::vector<uint32_t> order;
    order.reserve(n);

    while (!q.empty())
    {
      uint32_t u = q.front();
      q.pop();
      order.push_back(u);

      for (auto v : adj[u])
      {
        if (--inDegree[v] == 0)
        {
          q.push(v);
        }
      }
    }

    if (order.size() != n)
    {
      throw std::runtime_error("Render graph has a cycle!");
    }

    return order;
  }

  void RenderGraph::InsertBarriers(VkCommandBuffer cmd, uint32_t passIndex)
  {
    auto& pass = m_Passes[passIndex];

    // Transition inputs to SHADER_READ_ONLY_OPTIMAL
    for (auto handle : pass.info.inputs)
    {
      auto& layout = m_CurrentLayouts[handle];
      if (layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;

      auto& res = m_Resources[handle];
      auto& image = ResolveResource(handle);

      TransitionImageLayout(cmd, image.GetImage(),
        layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        res.desc.aspect);

      layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }

  void RenderGraph::Execute(VkCommandBuffer cmd, void* userData)
  {
    RGExecuteContext ctx;
    ctx.cmd = cmd;
    ctx.extent = m_Extent;
    ctx.userData = userData;

    for (auto passIndex : m_ExecutionOrder)
    {
      auto& pass = m_Passes[passIndex];

      // Insert barriers for inputs
      InsertBarriers(cmd, passIndex);

      // Determine framebuffer
      VkFramebuffer fb = pass.overrideFramebuffer != VK_NULL_HANDLE
        ? pass.overrideFramebuffer : pass.framebuffer;

      // Compute attachment count: colors + depth
      uint32_t attachmentCount = static_cast<uint32_t>(
        std::max(pass.info.colorOutputs.size(), static_cast<size_t>(1)) + 1);

      std::vector<VkClearValue> clearValues(attachmentCount);
      clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
      clearValues[1].depthStencil = {1.0f, 0};
      for (uint32_t i = 2; i < attachmentCount; i++)
      {
        clearValues[i].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
      }

      VkRenderPassBeginInfo rpInfo{};
      rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpInfo.renderPass = pass.renderPass;
      rpInfo.framebuffer = fb;
      rpInfo.renderArea.offset = {0, 0};
      rpInfo.renderArea.extent = m_Extent;
      rpInfo.clearValueCount = attachmentCount;
      rpInfo.pClearValues = clearValues.data();

      vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

      pass.info.execute(ctx);

      vkCmdEndRenderPass(cmd);

      // Update tracked layouts after render pass
      for (size_t i = 0; i < pass.info.colorOutputs.size(); i++)
      {
        auto handle = pass.info.colorOutputs[i];
        // Attachment 0 gets finalColorLayout; additional attachments always end as COLOR_ATTACHMENT_OPTIMAL
        VkImageLayout layout = (i == 0)
          ? pass.info.finalColorLayout
          : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        m_CurrentLayouts[handle] = layout;
        ResolveResource(handle).SetLayout(layout);
      }
      if (pass.info.depthOutput != RG_INVALID_HANDLE)
      {
        m_CurrentLayouts[pass.info.depthOutput] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        ResolveResource(pass.info.depthOutput).SetLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
      }
    }
  }

  void RenderGraph::SetPassFramebuffer(uint32_t pass, VkFramebuffer fb)
  {
    m_Passes[pass].overrideFramebuffer = fb;
  }

  void RenderGraph::SetPassInput(uint32_t pass, uint32_t slot, RGHandle resource)
  {
    assert(pass < m_Passes.size() && "SetPassInput: pass index out of range");
    assert(slot < m_Passes[pass].info.inputs.size() && "SetPassInput: slot out of range");
    m_Passes[pass].info.inputs[slot] = resource;
  }

  void RenderGraph::SetPassColorOutput(uint32_t pass, uint32_t slot, RGHandle resource)
  {
    assert(pass < m_Passes.size() && "SetPassColorOutput: pass index out of range");
    assert(slot < m_Passes[pass].info.colorOutputs.size() && "SetPassColorOutput: slot out of range");
    m_Passes[pass].info.colorOutputs[slot] = resource;
  }

  VulkanImage& RenderGraph::GetResource(RGHandle handle)
  {
    return ResolveResource(handle);
  }

  VkRenderPass RenderGraph::GetPassRenderPass(uint32_t pass) const
  {
    return m_Passes[pass].renderPass;
  }

  void RenderGraph::SetResourceLayout(RGHandle handle, VkImageLayout layout)
  {
    m_CurrentLayouts[handle] = layout;
    ResolveResource(handle).SetLayout(layout);
  }

  VulkanImage& RenderGraph::ResolveResource(RGHandle handle)
  {
    auto& res = m_Resources[handle];
    return res.managed ? res.image : *res.externalImage;
  }

  void RenderGraph::Resize(VkExtent2D newExtent)
  {
    vkDeviceWaitIdle(m_Ctx->device);
    m_Extent = newExtent;

    // Destroy managed resources
    for (auto& res : m_Resources)
    {
      if (res.managed && res.image.IsValid())
      {
        res.image.Destroy(*m_Ctx);
      }
    }

    // Destroy non-external framebuffers and private depths
    for (auto& pass : m_Passes)
    {
      if (!pass.info.externalFramebuffer && pass.framebuffer != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(m_Ctx->device, pass.framebuffer, nullptr);
        pass.framebuffer = VK_NULL_HANDLE;
      }
      if (pass.privateDepth.IsValid())
      {
        pass.privateDepth.Destroy(*m_Ctx);
      }
    }

    // Recreate
    AllocateResources();
    BuildFramebuffers();

    // Reset layouts
    std::fill(m_CurrentLayouts.begin(), m_CurrentLayouts.end(), VK_IMAGE_LAYOUT_UNDEFINED);
  }
}
