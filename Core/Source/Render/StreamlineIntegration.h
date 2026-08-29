#pragma once

#include "Pch.h"
#include "VulkanRequirements.h"

namespace YAEngine
{
  // Streamline features the engine knows about. Adding one only means extending this
  // enum, its sl::Feature mapping and the list passed to Init.
  enum class StreamlineFeature : uint32_t
  {
    DLSS
  };

  enum class DLSSQuality : uint32_t
  {
    DLAA,
    Quality,
    Balanced,
    Performance,
    UltraPerformance
  };

  struct DLSSSettings
  {
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t renderWidthMin = 0;
    uint32_t renderHeightMin = 0;
    uint32_t renderWidthMax = 0;
    uint32_t renderHeightMax = 0;
  };

  // Opaque sl::FrameToken*, only valid while Streamline is initialized.
  using StreamlineFrameToken = void*;

  // One Vulkan image handed to Streamline. Streamline cannot query a VkImage, so every
  // field it would otherwise read has to be filled in by the engine. layout must be the
  // layout the image is really in when the evaluate call runs.
  struct DLSSImage
  {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat format = VK_FORMAT_UNDEFINED;
    // Streamline assumes a colour view unless the tag carries a subresource range, so
    // the depth buffer has to say so explicitly.
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t width = 0;
    uint32_t height = 0;
  };

  // Everything one slEvaluateFeature call needs. Matrices are the engine's own
  // column-vector GLM matrices and must NOT carry the camera jitter.
  struct DLSSEvaluateDesc
  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    StreamlineFrameToken frameToken = nullptr;
    DLSSQuality quality = DLSSQuality::DLAA;

    DLSSImage colorIn;        // render-res jittered HDR
    DLSSImage colorOut;       // output-res storage image
    DLSSImage depth;          // render-res, depth-only view
    DLSSImage motionVectors;  // render-res

    glm::mat4 view { 1.0f };
    glm::mat4 proj { 1.0f };
    glm::mat4 prevView { 1.0f };
    glm::mat4 prevProj { 1.0f };

    glm::vec3 cameraPosition { 0.0f };
    glm::vec3 cameraRight { 1.0f, 0.0f, 0.0f };
    glm::vec3 cameraUp { 0.0f, 1.0f, 0.0f };
    glm::vec3 cameraForward { 0.0f, 0.0f, -1.0f };

    // Subpixel offset of this frame's samples, in render-resolution pixels, screen
    // space with Y down, range [-0.5, 0.5].
    glm::vec2 jitterPixels { 0.0f };
    // Multiplied into the tagged motion vectors so they land in [-1,1] screen units
    // pointing at the previous frame.
    glm::vec2 mvecScale { 1.0f };

    float nearPlane = 0.0f;
    float farPlane = 0.0f;
    float fov = 0.0f;          // vertical, radians
    float aspectRatio = 1.0f;
    // Drops the accumulated history for one frame.
    bool reset = false;
  };

  // Bootstraps Streamline alongside the engine's own Vulkan setup (manual hooking: the
  // engine keeps calling vulkan-1 directly and only tells SL what it created).
  //
  // Every entry point is a no-op returning "unavailable" when YA_DLSS is off or when any
  // SL call failed, so call sites never need to guard.
  class StreamlineIntegration
  {
  public:

    // Must run before the Vulkan instance exists: SL loads its plugins here.
    void Init(const std::vector<StreamlineFeature>& features);
    // Must run before the Vulkan instance is created, feeds SL's Vulkan needs into the registry.
    void ApplyRequirements(VulkanRequirements& requirements);
    // Must run right after the logical device is created.
    void SetVulkanInfo(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                       uint32_t graphicsQueueFamily, uint32_t graphicsQueueIndex,
                       uint32_t computeQueueFamily, uint32_t computeQueueIndex);
    // Must run before the Vulkan device and instance are destroyed.
    void Shutdown();

    bool IsInitialized() const { return b_Initialized; }
    bool IsFeatureSupported(StreamlineFeature feature) const;
    bool IsDLSSAvailable() const { return IsFeatureSupported(StreamlineFeature::DLSS); }

    // Why a feature is unavailable, for the UI. Empty while it is available.
    const std::string& GetUnavailableReason(StreamlineFeature feature) const;

    // Render resolution DLSS wants for the given output size, false when unavailable.
    bool GetDLSSSettings(DLSSQuality quality, uint32_t outputWidth, uint32_t outputHeight, DLSSSettings& settings) const;
    // Per-frame handle every sl* evaluate call needs, nullptr when unavailable.
    StreamlineFrameToken GetFrameToken(uint32_t frameIndex);

    // Tags the four buffers, pushes the per-frame constants and runs the upscale into
    // desc.colorOut on desc.cmd. Must be called outside a VkRenderPass instance.
    bool EvaluateDLSS(const DLSSEvaluateDesc& desc);
    // Releases the DLSS instance so the next evaluate rebuilds it. Needed when the
    // extents change; the caller owns waiting for the GPU first.
    void ReleaseDLSSResources();

  private:

    struct FeatureState
    {
      StreamlineFeature feature = StreamlineFeature::DLSS;
      bool loaded = false;
      bool supported = false;
      std::string unavailableReason;
    };

    const FeatureState* FindFeature(StreamlineFeature feature) const;
    void SetFeatureUnavailable(FeatureState& state, const char* reason);

    std::vector<FeatureState> m_Features;
    bool b_Initialized = false;

    // Last options handed to slDLSSSetOptions, so the plugin is only told about real
    // changes instead of once per frame.
    DLSSQuality m_DLSSOptionsQuality = DLSSQuality::DLAA;
    uint32_t m_DLSSOptionsWidth = 0;
    uint32_t m_DLSSOptionsHeight = 0;
    bool b_DLSSEvaluateLogged = false;
  };
}
