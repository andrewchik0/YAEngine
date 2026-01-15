#include "VulkanRenderPass.h"

#include <format>

namespace YAEngine
{
  void VulkanRenderPass::Init(VkDevice device, VkFormat swapChainImageFormat, VmaAllocator allocator, VkImageLayout finalImageLayout, bool clear, bool secondaryBuffer)
  {
    m_Device = device;
    m_SwapChainImageFormat = swapChainImageFormat;
    m_Allocator = allocator;
    m_ImageLayout = finalImageLayout;
    b_SecondaryBuffer = secondaryBuffer;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = finalImageLayout;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription secondaryColorAttachment{};
    secondaryColorAttachment.format = swapChainImageFormat;
    secondaryColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    secondaryColorAttachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    secondaryColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    secondaryColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    secondaryColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    secondaryColorAttachment.initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    secondaryColorAttachment.finalLayout = finalImageLayout;

    VkAttachmentReference secondaryColorAttachmentRef{};
    secondaryColorAttachmentRef.attachment = 2;
    secondaryColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference attachmentRefs[2];
    attachmentRefs[0] = colorAttachmentRef;
    if (secondaryBuffer)
    {
      attachmentRefs[1] = secondaryColorAttachmentRef;
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1 + secondaryBuffer;
    subpass.pColorAttachments = attachmentRefs;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[3];
    attachments[0] = colorAttachment;
    attachments[1] = depthAttachment;
    if (secondaryBuffer)
    {
      attachments[2] = secondaryColorAttachment;
    }

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2 + secondaryBuffer;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
    {
      throw std::runtime_error("failed to create render pass!");
    }
  }

  void VulkanRenderPass::Destroy()
  {
    vkDeviceWaitIdle(m_Device);
    vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
  }

  void VulkanRenderPass::Recreate(bool clear, bool secondaryBuffer)
  {
    Destroy();
    Init(m_Device, m_SwapChainImageFormat, m_Allocator, m_ImageLayout, clear, secondaryBuffer);
  }

  void VulkanRenderPass::Begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, VkExtent2D swapChainExtent)
  {
    VkClearValue clearValues[3];
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    clearValues[2].color = {{ 0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0,0};
    renderPassInfo.renderArea.extent = swapChainExtent;
    renderPassInfo.clearValueCount = 2 + b_SecondaryBuffer;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  }

  void VulkanRenderPass::End(VkCommandBuffer commandBuffer)
  {
    vkCmdEndRenderPass(commandBuffer);
  }
}
