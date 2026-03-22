#pragma once

#include "Pch.h"
#include "VulkanImage.h"

#include <functional>
#include <string>
#include <vector>

namespace YAEngine
{
  struct RenderContext;

  using RGHandle = uint32_t;
  static constexpr RGHandle RG_INVALID_HANDLE = UINT32_MAX;

  struct RGResourceDesc
  {
    std::string name;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    float widthScale = 1.0f;
    float heightScale = 1.0f;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageUsageFlags additionalUsage = 0;
  };

  struct RGExecuteContext
  {
    VkCommandBuffer cmd;
    VkExtent2D extent;
    void* userData = nullptr;
  };

  using RGCallback = std::function<void(const RGExecuteContext&)>;

  struct RGPassInfo
  {
    std::string name;
    std::vector<RGHandle> inputs;
    std::vector<RGHandle> colorOutputs;
    RGHandle depthOutput = RG_INVALID_HANDLE;
    bool clearColor = true;
    bool clearDepth = true;
    bool externalFramebuffer = false;
    VkFormat externalFormat = VK_FORMAT_UNDEFINED;
    VkImageLayout finalColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    RGCallback execute;
  };

  class RenderGraph
  {
  public:

    void Init(const RenderContext& ctx, VkExtent2D extent);
    void Destroy();

    RGHandle CreateResource(const RGResourceDesc& desc);
    RGHandle ImportResource(const std::string& name, VulkanImage& image);

    uint32_t AddPass(const RGPassInfo& info);

    void Compile();

    void SetPassFramebuffer(uint32_t pass, VkFramebuffer fb);
    void SetPassInput(uint32_t pass, uint32_t slot, RGHandle resource);
    void SetPassColorOutput(uint32_t pass, uint32_t slot, RGHandle resource);

    void Execute(VkCommandBuffer cmd, void* userData = nullptr);
    void Resize(VkExtent2D newExtent);

    VulkanImage& GetResource(RGHandle handle);
    VkRenderPass GetPassRenderPass(uint32_t pass) const;
    VkExtent2D GetExtent() const { return m_Extent; }
    void SetResourceLayout(RGHandle handle, VkImageLayout layout);

  private:

    struct Resource
    {
      RGResourceDesc desc;
      VulkanImage image;
      VulkanImage* externalImage = nullptr;
      bool managed = true;
      VkImageUsageFlags usage = 0;
    };

    struct CompiledPass
    {
      RGPassInfo info;
      VkRenderPass renderPass = VK_NULL_HANDLE;
      VkFramebuffer framebuffer = VK_NULL_HANDLE;
      VulkanImage privateDepth;
      VkFramebuffer overrideFramebuffer = VK_NULL_HANDLE;
    };

    void DetermineResourceUsage();
    void AllocateResources();
    void BuildRenderPasses();
    void BuildFramebuffers();
    std::vector<uint32_t> TopologicalSort() const;
    void InsertBarriers(VkCommandBuffer cmd, uint32_t passIndex);
    VulkanImage& ResolveResource(RGHandle handle);

    const RenderContext* m_Ctx = nullptr;
    VkExtent2D m_Extent {};

    std::vector<Resource> m_Resources;
    std::vector<CompiledPass> m_Passes;
    std::vector<uint32_t> m_ExecutionOrder;
    std::vector<VkImageLayout> m_CurrentLayouts;

    bool m_Compiled = false;
  };
}
