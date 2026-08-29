#pragma once

#include "Pch.h"
#include "VulkanImage.h"

namespace YAEngine
{
  struct RenderContext;
#ifdef YA_EDITOR
  class GpuProfiler;
#endif

  using RGHandle = uint32_t;
  static constexpr RGHandle RG_INVALID_HANDLE = UINT32_MAX;

  // Which of the graph's two extents a resource or pass is sized against. Everything the
  // scene is rasterized and shaded into is Render; everything downstream of the upscaler
  // is Output. The two are equal unless a DLSS upscale mode is active.
  enum class RGResolution : uint8_t
  {
    Render,
    Output
  };

  struct RGResourceDesc
  {
    std::string name;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    float widthScale = 1.0f;
    float heightScale = 1.0f;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageUsageFlags additionalUsage = 0;
    VkFilter filter = VK_FILTER_LINEAR;
    uint32_t mipLevels = 1;
    RGResolution resolution = RGResolution::Render;
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
    std::vector<RGHandle> storageOutputs;
    RGHandle depthOutput = RG_INVALID_HANDLE;
    bool clearColor = true;
    bool clearDepth = true;
    bool depthOnly = false;
    bool isCompute = false;
    bool externalFramebuffer = false;
    VkFormat externalFormat = VK_FORMAT_UNDEFINED;
    VkImageLayout finalColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Only consulted for passes that have no color output of their own to be sized
    // against: compute passes and passes drawing into an external framebuffer.
    RGResolution resolution = RGResolution::Render;
    // Skips the whole pass, render pass and barriers included, when it returns false.
    // Empty means the pass always runs. A pass that only returns early from execute
    // still begins its render pass, which an alternative resolve must not do: its
    // framebuffer is not even rebound while the other one owns the frame.
    std::function<bool()> isEnabled;
    // Label the GPU zone and the debug marker carry while useAltName returns true.
    // Only the per-frame labels swap - render pass and framebuffer object names are
    // built once at setup and keep the primary name.
    std::string altName;
    std::function<bool()> useAltName;
    RGCallback execute;
  };

  class RenderGraph
  {
  public:

    void Init(const RenderContext& ctx, VkExtent2D renderExtent, VkExtent2D outputExtent);
    void Destroy();

    RGHandle CreateResource(const RGResourceDesc& desc);
    RGHandle ImportResource(const std::string& name, VulkanImage& image);

    uint32_t AddPass(const RGPassInfo& info);

    void Compile();

    void SetPassFramebuffer(uint32_t pass, VkFramebuffer fb);
    void SetPassExtent(uint32_t pass, VkExtent2D extent);
    void SetPassInput(uint32_t pass, uint32_t slot, RGHandle resource);
    void SetPassColorOutput(uint32_t pass, uint32_t slot, RGHandle resource);

    void Execute(VkCommandBuffer cmd, void* userData = nullptr);
#ifdef YA_EDITOR
    void SetGpuProfiler(GpuProfiler* profiler) { m_GpuProfiler = profiler; }
#endif
    void Resize(VkExtent2D renderExtent, VkExtent2D outputExtent);

    VulkanImage& GetResource(RGHandle handle);
    VkRenderPass GetPassRenderPass(uint32_t pass) const;
    VkExtent2D GetExtent() const { return m_Extent; }
    VkExtent2D GetOutputExtent() const { return m_OutputExtent; }
    void SetResourceLayout(RGHandle handle, VkImageLayout layout);

    VkImage GetResourceImage(RGHandle handle);
    const RGResourceDesc& GetResourceDesc(RGHandle handle) const;
    void SetResourceMipLevels(RGHandle handle, uint32_t mipLevels);

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
      VkExtent2D overrideExtent {};
      VkExtent2D extent {};
    };

    void DetermineResourceUsage();
    void AllocateResources();
    void BuildRenderPasses();
    void BuildFramebuffers();
    std::vector<uint32_t> TopologicalSort() const;
    void InsertBarriers(VkCommandBuffer cmd, uint32_t passIndex);
    VulkanImage& ResolveResource(RGHandle handle);
    VkExtent2D BaseExtent(RGResolution resolution) const;
    VkExtent2D ScaledExtent(const RGResourceDesc& desc) const;

    const RenderContext* m_Ctx = nullptr;
    VkExtent2D m_Extent {};
    VkExtent2D m_OutputExtent {};

    std::vector<Resource> m_Resources;
    std::vector<CompiledPass> m_Passes;
    std::vector<uint32_t> m_ExecutionOrder;
    std::vector<VkImageLayout> m_CurrentLayouts;

    bool m_Compiled = false;

#ifdef YA_EDITOR
    GpuProfiler* m_GpuProfiler = nullptr;
#endif
  };
}
