#include "RenderGraph.h"

#include "RenderContext.h"
#include "ImageBarrier.h"
#include "Utils/Log.h"

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
      for (auto handle : pass.info.storageOutputs)
      {
        m_Resources[handle].usage |= VK_IMAGE_USAGE_STORAGE_BIT;
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
      imageDesc.mipLevels = res.desc.mipLevels;

      bool needsSampler = (res.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;

      if (needsSampler)
      {
        SamplerDesc samplerDesc;
        samplerDesc.magFilter = res.desc.filter;
        samplerDesc.minFilter = res.desc.filter;
        samplerDesc.maxLod = static_cast<float>(res.desc.mipLevels - 1);
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
      // Compute passes don't need render passes
      if (pass.info.isCompute) continue;

      // Depth-only pass: single depth attachment, no color
      if (pass.info.depthOnly)
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

        VkAttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0;
        subpass.pColorAttachments = nullptr;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
          | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
          | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
          | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &att;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;

        if (vkCreateRenderPass(m_Ctx->device, &rpInfo, nullptr, &pass.renderPass) != VK_SUCCESS)
        {
          YA_LOG_ERROR("Render", "Failed to create depth-only render pass: %s", pass.info.name.c_str());
          throw std::runtime_error("Failed to create depth-only render pass: " + pass.info.name);
        }
        continue;
      }

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
        YA_LOG_ERROR("Render", "Failed to create render pass: %s", pass.info.name.c_str());
        throw std::runtime_error("Failed to create render pass: " + pass.info.name);
      }
    }
  }

  void RenderGraph::BuildFramebuffers()
  {
    for (auto& pass : m_Passes)
    {
      // Compute passes don't need framebuffers
      if (pass.info.isCompute) continue;

      // Depth-only pass
      if (pass.info.depthOnly)
      {
        if (pass.info.depthOutput != RG_INVALID_HANDLE)
        {
          auto& res = m_Resources[pass.info.depthOutput];
          pass.extent.width = std::max(1u, static_cast<uint32_t>(m_Extent.width * res.desc.widthScale));
          pass.extent.height = std::max(1u, static_cast<uint32_t>(m_Extent.height * res.desc.heightScale));
        }
        else
        {
          pass.extent = m_Extent;
        }

        VkImageView views[1] = {
          ResolveResource(pass.info.depthOutput).GetView()
        };

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = pass.renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = views;
        fbInfo.width = pass.extent.width;
        fbInfo.height = pass.extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_Ctx->device, &fbInfo, nullptr, &pass.framebuffer) != VK_SUCCESS)
        {
          YA_LOG_ERROR("Render", "Failed to create depth-only framebuffer: %s", pass.info.name.c_str());
          throw std::runtime_error("Failed to create depth-only framebuffer: " + pass.info.name);
        }
        continue;
      }

      // Compute per-pass extent from first color output (or global extent for external)
      if (!pass.info.colorOutputs.empty() && !pass.info.externalFramebuffer)
      {
        auto& res = m_Resources[pass.info.colorOutputs[0]];
        pass.extent.width = std::max(1u, static_cast<uint32_t>(m_Extent.width * res.desc.widthScale));
        pass.extent.height = std::max(1u, static_cast<uint32_t>(m_Extent.height * res.desc.heightScale));
      }
      else
      {
        pass.extent = m_Extent;
      }

      if (pass.info.externalFramebuffer) continue;
      if (pass.info.colorOutputs.empty()) continue;

      // Create private depth if no managed depth output (match pass extent)
      if (pass.info.depthOutput == RG_INVALID_HANDLE)
      {
        ImageDesc depthDesc;
        depthDesc.width = pass.extent.width;
        depthDesc.height = pass.extent.height;
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
      fbInfo.width = pass.extent.width;
      fbInfo.height = pass.extent.height;
      fbInfo.layers = 1;

      if (vkCreateFramebuffer(m_Ctx->device, &fbInfo, nullptr, &pass.framebuffer) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create framebuffer: %s", pass.info.name.c_str());
        throw std::runtime_error("Failed to create framebuffer: " + pass.info.name);
      }
    }
  }

  std::vector<uint32_t> RenderGraph::TopologicalSort() const
  {
    uint32_t n = static_cast<uint32_t>(m_Passes.size());

    // Build adjacency with both read->write and write->write dependencies.
    // Single-pass over passes in AddPass order: lastWriter tracks the most recent
    // writer for each resource. Read deps link to the latest writer; write deps
    // chain consecutive writers so ordering is preserved.
    std::vector<std::vector<uint32_t>> adj(n);
    std::vector<uint32_t> inDegree(n, 0);
    std::unordered_map<RGHandle, uint32_t> lastWriter;

    for (uint32_t i = 0; i < n; i++)
    {
      // Read->write: pass i reads resource written by an earlier pass
      for (auto input : m_Passes[i].info.inputs)
      {
        auto it = lastWriter.find(input);
        if (it != lastWriter.end() && it->second != i)
        {
          adj[it->second].push_back(i);
          inDegree[i]++;
        }
      }

      // Write->write: chain consecutive writers of the same resource
      auto trackWrite = [&](RGHandle handle) {
        auto it = lastWriter.find(handle);
        if (it != lastWriter.end() && it->second != i)
        {
          adj[it->second].push_back(i);
          inDegree[i]++;
        }
        lastWriter[handle] = i;
      };

      for (auto handle : m_Passes[i].info.colorOutputs)
        trackWrite(handle);
      for (auto handle : m_Passes[i].info.storageOutputs)
        trackWrite(handle);
      if (m_Passes[i].info.depthOutput != RG_INVALID_HANDLE)
        trackWrite(m_Passes[i].info.depthOutput);
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
      YA_LOG_ERROR("Render", "Render graph has a cycle");
      throw std::runtime_error("Render graph has a cycle!");
    }

    return order;
  }

  void RenderGraph::InsertBarriers(VkCommandBuffer cmd, uint32_t passIndex)
  {
    static constexpr uint32_t MAX_BARRIERS = 16;

    auto& pass = m_Passes[passIndex];

    struct BarrierStages
    {
      VkPipelineStageFlags srcStage;
      VkPipelineStageFlags dstStage;
    };

    std::array<VkImageMemoryBarrier, MAX_BARRIERS> barriers{};
    std::array<BarrierStages, MAX_BARRIERS> stages{};
    uint32_t barrierCount = 0;

    // Transition inputs to SHADER_READ_ONLY_OPTIMAL
    for (auto handle : pass.info.inputs)
    {
      auto& layout = m_CurrentLayouts[handle];
      if (layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;

      auto& res = m_Resources[handle];
      auto& image = ResolveResource(handle);
      auto info = LookupBarrierInfo(layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

      assert(barrierCount < MAX_BARRIERS && "InsertBarriers: too many barriers");
      auto& barrier = barriers[barrierCount];
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = layout;
      barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = image.GetImage();
      barrier.subresourceRange.aspectMask = res.desc.aspect;
      barrier.subresourceRange.baseMipLevel = 0;
      barrier.subresourceRange.levelCount = res.desc.mipLevels;
      barrier.subresourceRange.baseArrayLayer = 0;
      barrier.subresourceRange.layerCount = 1;
      barrier.srcAccessMask = info.srcAccess;
      barrier.dstAccessMask = info.dstAccess;
      stages[barrierCount] = { info.srcStage, info.dstStage };
      barrierCount++;

      layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      image.SetLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // For compute passes: transition storage outputs to GENERAL
    if (pass.info.isCompute)
    {
      for (auto handle : pass.info.storageOutputs)
      {
        auto& layout = m_CurrentLayouts[handle];
        if (layout == VK_IMAGE_LAYOUT_GENERAL) continue;

        auto& res = m_Resources[handle];
        auto& image = ResolveResource(handle);
        auto info = LookupBarrierInfo(layout, VK_IMAGE_LAYOUT_GENERAL);

        assert(barrierCount < MAX_BARRIERS && "InsertBarriers: too many barriers");
        auto& barrier = barriers[barrierCount];
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.GetImage();
        barrier.subresourceRange.aspectMask = res.desc.aspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = res.desc.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = info.srcAccess;
        barrier.dstAccessMask = info.dstAccess;
        stages[barrierCount] = { info.srcStage, info.dstStage };
        barrierCount++;

        layout = VK_IMAGE_LAYOUT_GENERAL;
        image.SetLayout(VK_IMAGE_LAYOUT_GENERAL);
      }
    }

    // Transition non-clearing depth output to DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    if (!pass.info.isCompute && pass.info.depthOutput != RG_INVALID_HANDLE && !pass.info.clearDepth)
    {
      auto handle = pass.info.depthOutput;
      auto& layout = m_CurrentLayouts[handle];
      if (layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
      {
        auto& res = m_Resources[handle];
        auto& image = ResolveResource(handle);
        auto info = LookupBarrierInfo(layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        assert(barrierCount < MAX_BARRIERS && "InsertBarriers: too many barriers");
        auto& barrier = barriers[barrierCount];
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.GetImage();
        barrier.subresourceRange.aspectMask = res.desc.aspect;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = res.desc.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = info.srcAccess;
        barrier.dstAccessMask = info.dstAccess;
        stages[barrierCount] = { info.srcStage, info.dstStage };
        barrierCount++;

        layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        image.SetLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
      }
    }

    // Transition non-clearing color outputs to COLOR_ATTACHMENT_OPTIMAL
    if (!pass.info.isCompute && !pass.info.clearColor)
    {
      for (auto handle : pass.info.colorOutputs)
      {
        auto& layout = m_CurrentLayouts[handle];
        if (layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
          auto& res = m_Resources[handle];
          auto& image = ResolveResource(handle);
          auto info = LookupBarrierInfo(layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

          assert(barrierCount < MAX_BARRIERS && "InsertBarriers: too many barriers");
          auto& barrier = barriers[barrierCount];
          barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          barrier.oldLayout = layout;
          barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          barrier.image = image.GetImage();
          barrier.subresourceRange.aspectMask = res.desc.aspect;
          barrier.subresourceRange.baseMipLevel = 0;
          barrier.subresourceRange.levelCount = res.desc.mipLevels;
          barrier.subresourceRange.baseArrayLayer = 0;
          barrier.subresourceRange.layerCount = 1;
          barrier.srcAccessMask = info.srcAccess;
          barrier.dstAccessMask = info.dstAccess;
          stages[barrierCount] = { info.srcStage, info.dstStage };
          barrierCount++;

          layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          image.SetLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
      }
    }

    // Group barriers by matching stage pairs and issue one vkCmdPipelineBarrier per group
    for (uint32_t i = 0; i < barrierCount; )
    {
      VkPipelineStageFlags srcStage = stages[i].srcStage;
      VkPipelineStageFlags dstStage = stages[i].dstStage;

      // Collect contiguous run of barriers with the same stages starting at i
      // Also swap matching barriers from later positions into the group
      uint32_t groupStart = i;
      uint32_t groupEnd = i + 1;

      for (uint32_t j = groupEnd; j < barrierCount; j++)
      {
        if (stages[j].srcStage == srcStage && stages[j].dstStage == dstStage)
        {
          if (j != groupEnd)
          {
            std::swap(barriers[j], barriers[groupEnd]);
            std::swap(stages[j], stages[groupEnd]);
          }
          groupEnd++;
        }
      }

      vkCmdPipelineBarrier(
        cmd,
        srcStage, dstStage,
        0,
        0, nullptr,
        0, nullptr,
        groupEnd - groupStart, &barriers[groupStart]
      );

      i = groupEnd;
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

      // Use overrideExtent if set, otherwise use pass extent from compilation
      VkExtent2D passExtent = (pass.overrideExtent.width > 0 && pass.overrideExtent.height > 0)
        ? pass.overrideExtent : pass.extent;
      ctx.extent = passExtent;

      // Insert barriers for inputs (and storage outputs for compute)
      InsertBarriers(cmd, passIndex);

      // Compute pass: no render pass, just execute
      if (pass.info.isCompute)
      {
        pass.info.execute(ctx);

        // Update tracked layouts for storage outputs
        for (auto handle : pass.info.storageOutputs)
        {
          m_CurrentLayouts[handle] = VK_IMAGE_LAYOUT_GENERAL;
          ResolveResource(handle).SetLayout(VK_IMAGE_LAYOUT_GENERAL);
        }
        continue;
      }

      // Depth-only pass
      if (pass.info.depthOnly)
      {
        VkFramebuffer fb = pass.overrideFramebuffer != VK_NULL_HANDLE
          ? pass.overrideFramebuffer : pass.framebuffer;

        VkClearValue clearValue{};
        clearValue.depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = pass.renderPass;
        rpInfo.framebuffer = fb;
        rpInfo.renderArea.offset = {0, 0};
        rpInfo.renderArea.extent = passExtent;
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = static_cast<float>(passExtent.width);
        viewport.height = static_cast<float>(passExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = passExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        pass.info.execute(ctx);

        vkCmdEndRenderPass(cmd);

        if (pass.info.depthOutput != RG_INVALID_HANDLE)
        {
          m_CurrentLayouts[pass.info.depthOutput] = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
          ResolveResource(pass.info.depthOutput).SetLayout(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        }
        continue;
      }

      // Regular graphics pass
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
      rpInfo.renderArea.extent = passExtent;
      rpInfo.clearValueCount = attachmentCount;
      rpInfo.pClearValues = clearValues.data();

      vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

      // Set viewport and scissor to match pass extent
      VkViewport viewport{};
      viewport.width = static_cast<float>(passExtent.width);
      viewport.height = static_cast<float>(passExtent.height);
      viewport.minDepth = 0.0f;
      viewport.maxDepth = 1.0f;
      vkCmdSetViewport(cmd, 0, 1, &viewport);

      VkRect2D scissor{};
      scissor.extent = passExtent;
      vkCmdSetScissor(cmd, 0, 1, &scissor);

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

  void RenderGraph::SetPassExtent(uint32_t pass, VkExtent2D extent)
  {
    m_Passes[pass].overrideExtent = extent;
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

  VkImage RenderGraph::GetResourceImage(RGHandle handle)
  {
    return ResolveResource(handle).GetImage();
  }

  const RGResourceDesc& RenderGraph::GetResourceDesc(RGHandle handle) const
  {
    return m_Resources[handle].desc;
  }

  void RenderGraph::SetResourceMipLevels(RGHandle handle, uint32_t mipLevels)
  {
    m_Resources[handle].desc.mipLevels = mipLevels;
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
      if (!pass.info.externalFramebuffer && !pass.info.isCompute && pass.framebuffer != VK_NULL_HANDLE)
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
