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
  static constexpr uint32_t RG_INVALID_PASS = UINT32_MAX;

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
  using RGAfterPassCallback = std::function<void(VkCommandBuffer cmd)>;

  struct RGPassInfo
  {
    std::string name;
    std::vector<RGHandle> inputs;
    std::vector<RGHandle> colorOutputs;
    std::vector<RGHandle> storageOutputs;
    RGHandle depthOutput = RG_INVALID_HANDLE;
    bool clearColor = true;
    // What every colour output of a clearColor pass is cleared to.
    VkClearColorValue clearColorValue = {{ 0.0f, 0.0f, 0.0f, 1.0f }};
    bool clearDepth = true;
    bool depthOnly = false;
    bool isCompute = false;
    // Which pipeline stage a compute-kind pass actually runs its shaders in. The barrier
    // table names VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT for every storage image transition,
    // which is wrong for a vkCmdTraceRaysKHR pass - it reads and writes from
    // VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR - so the graph substitutes this mask for
    // the compute bit on whichever side of a transition belongs to the pass. Only consulted
    // while isCompute is set.
    VkPipelineStageFlags shaderStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
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
    // The extent a resource is allocated at: its resolution's extent times its width and height scale.
    VkExtent2D GetResourceExtent(RGHandle handle) const { return ScaledExtent(m_Resources[handle].desc); }
    void SetResourceLayout(RGHandle handle, VkImageLayout layout);

    VkImage GetResourceImage(RGHandle handle);
    const RGResourceDesc& GetResourceDesc(RGHandle handle) const;
    void SetResourceMipLevels(RGHandle handle, uint32_t mipLevels);

    // Linear scan over the resource list, so a target added to SetupRenderGraph becomes
    // addressable by name for free. Deliberately not cached: a map would be one more thing
    // to keep in sync across resizes, and the only caller runs once per frame capture.
    RGHandle FindResource(std::string_view name) const;
    // Enumeration for frame capture's target discovery: handles 0..count-1 are valid and
    // GetResourceDesc answers for each of them.
    uint32_t GetResourceCount() const { return static_cast<uint32_t>(m_Resources.size()); }
    // False for an imported image, which the graph neither allocates nor resizes.
    bool IsResourceManaged(RGHandle handle) const { return m_Resources[handle].managed; }

    // Pass enumeration for frame capture. Pass indices are AddPass order; the execution order
    // lists them the way Execute runs them.
    const RGPassInfo& GetPassInfo(uint32_t pass) const { return m_Passes[pass].info; }
    const std::vector<uint32_t>& GetExecutionOrder() const { return m_ExecutionOrder; }
    // What Execute would decide for the pass in the current state.
    bool IsPassEnabled(uint32_t pass) const;
    // Matches the primary name or the alt name; RG_INVALID_PASS when neither does.
    uint32_t FindPass(std::string_view name) const;
    // The layout the barrier tracking holds, which is what the next pass transitions from.
    VkImageLayout GetResourceLayout(RGHandle handle) const { return m_CurrentLayouts[handle]; }

    // Execute invokes the callback right after the armed pass has finished recording, outside
    // any render pass instance. The callback has to leave every image in the layout
    // GetResourceLayout reports. Disarmed, the cost is one index comparison per pass.
    void ArmAfterPass(uint32_t pass, RGAfterPassCallback callback);
    void DisarmAfterPass();

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
    // The shader stage the pass that last wrote each resource as a storage image ran in.
    // Index-parallel to m_CurrentLayouts, and the source side of the substitution described
    // on RGPassInfo::shaderStage - a later reader has to wait on the stage that actually
    // produced the image, not on the compute stage the barrier table assumes.
    std::vector<VkPipelineStageFlags> m_ResourceWriteStages;

    uint32_t m_AfterPass = RG_INVALID_PASS;
    RGAfterPassCallback m_AfterPassCallback;

    bool m_Compiled = false;

#ifdef YA_EDITOR
    GpuProfiler* m_GpuProfiler = nullptr;
#endif
  };
}
