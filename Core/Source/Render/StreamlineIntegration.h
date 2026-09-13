#pragma once

#include "Pch.h"
#include "VulkanRequirements.h"

namespace YAEngine
{
  // Streamline features the engine knows about. Adding one only means extending this
  // enum, its sl::Feature mapping and the list passed to Init.
  enum class StreamlineFeature : uint32_t
  {
    DLSS,
    // DLSS Ray Reconstruction (sl::kFeatureDLSS_RR). A separate plugin and a separate
    // availability answer: a device can have super resolution and not have this one.
    RayReconstruction
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

  // Render presets sl_dlss_d.h defines, verbatim. The numbering is NOT contiguous - A, B and
  // C were removed from the SDK - so the enum is a dense UI list and ToStreamlinePreset maps
  // it onto the real values.
  enum class RayReconstructionPreset : uint32_t
  {
    Default,  // whatever an OTA decides
    D,        // default model (transformer)
    E,        // latest transformer model
    F, G, H, I, J, K, L,
    M, N, O,  // the header marks these three "not recommended to use"
    Count
  };

  inline const char* GetRayReconstructionPresetName(RayReconstructionPreset preset)
  {
    switch (preset)
    {
      case RayReconstructionPreset::Default: return "eDefault";
      case RayReconstructionPreset::D: return "ePresetD (default transformer)";
      case RayReconstructionPreset::E: return "ePresetE (latest transformer)";
      case RayReconstructionPreset::F: return "ePresetF";
      case RayReconstructionPreset::G: return "ePresetG";
      case RayReconstructionPreset::H: return "ePresetH";
      case RayReconstructionPreset::I: return "ePresetI";
      case RayReconstructionPreset::J: return "ePresetJ";
      case RayReconstructionPreset::K: return "ePresetK";
      case RayReconstructionPreset::L: return "ePresetL";
      case RayReconstructionPreset::M: return "ePresetM (not recommended)";
      case RayReconstructionPreset::N: return "ePresetN (not recommended)";
      case RayReconstructionPreset::O: return "ePresetO (not recommended)";
      case RayReconstructionPreset::Count: break;
    }

    return "Unknown";
  }

  // The specular guide ray reconstruction is tagged with. Section 4.1.9 of NVIDIA's DLSS-RR
  // guide needs the hit distance only when specular motion vectors are not provided, so the two
  // are alternatives and exactly one is pushed. A temporary developer toggle while the better
  // input is being determined.
  enum class RayReconstructionSpecularGuide : uint32_t
  {
    MotionVectors,  // ptSpecularMotion, computed by pt_main.rgen
    HitDistance,    // ptHitDistance, which RR synthesizes its own from
    Count
  };

  inline const char* GetRayReconstructionSpecularGuideName(RayReconstructionSpecularGuide guide)
  {
    switch (guide)
    {
      case RayReconstructionSpecularGuide::MotionVectors: return "Specular Motion Vectors";
      case RayReconstructionSpecularGuide::HitDistance: return "Specular Hit Distance";
      case RayReconstructionSpecularGuide::Count: break;
    }

    return "Unknown";
  }

  // What the ray reconstruction panel section drives. Not serialized: section 3.13 of the
  // DLSS-RR Integration Guide recommends shipping the default and offering the named presets
  // for experimentation only, so this is a session-lifetime choice.
  //
  // The exposure, camera transpose and hit distance levers that used to sit here were removed
  // once the documentation answered what they were guessing at. Section 3.7 states that
  // exposure, auto-exposure and sharpness "are not supported by DLSS Ray Reconstruction",
  // which is why the two exposure sliders measured no effect at all; the transpose was
  // testing a convention section 3.4.9 states outright ("All matrices are Row Major Order and
  // use left multiplication"); and the hit distance tag now carries what section 3.4.9 asks
  // for, so switching it off only removes information.
  struct RayReconstructionSettings
  {
    // Default rather than a named preset. The preset letters are indices into whatever model
    // the loaded nvngx_dlssd.dll carries, so a letter that named the newest model in one drop
    // names an older one in the next: E was the latest transformer in 310.7.0 and measurably
    // less boiling there, but 310.7.129 (DLSS 4.5) put its second-generation model behind
    // Default, leaving an explicit E pinned to the superseded one. Default always resolves to
    // whatever the installed model considers current, which is also what section 3.13
    // recommends shipping.
    RayReconstructionPreset preset = RayReconstructionPreset::Default;
    RayReconstructionSpecularGuide specularGuide = RayReconstructionSpecularGuide::MotionVectors;
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
  //
  // Both resolves share it: super resolution reads everything down to the flags, and ray
  // reconstruction reads the same plus the four guide images below, which stay empty for
  // the super resolution call.
  struct DLSSEvaluateDesc
  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    StreamlineFrameToken frameToken = nullptr;
    DLSSQuality quality = DLSSQuality::DLAA;

    DLSSImage colorIn;        // render-res jittered HDR
    DLSSImage colorOut;       // output-res storage image
    DLSSImage depth;          // render-res, depth-only view
    DLSSImage motionVectors;  // render-res

    // Ray reconstruction only. All render-res; EvaluateRayReconstruction tags whichever
    // of them carries a live VkImage and treats an empty one as "not provided", which is
    // legal for the two optional specular guides and nothing else. Of those two only the
    // one rrSettings.specularGuide names is tagged.
    DLSSImage diffuseAlbedo;          // demodulation guide
    DLSSImage specularAlbedo;         // demodulation guide
    DLSSImage normalRoughness;        // world normal xyz + roughness w, one image
    DLSSImage specularHitDistance;    // optional
    DLSSImage specularMotionVectors;  // optional, MainVelocity's convention and mvecScale

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

    // Read only by EvaluateRayReconstruction.
    RayReconstructionSettings rrSettings;
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
    bool IsRayReconstructionAvailable() const
    {
      return IsFeatureSupported(StreamlineFeature::RayReconstruction);
    }

    // Why a feature is unavailable, for the UI. Empty while it is available.
    const std::string& GetUnavailableReason(StreamlineFeature feature) const;

    // Render resolution DLSS wants for the given output size, false when unavailable.
    bool GetDLSSSettings(DLSSQuality quality, uint32_t outputWidth, uint32_t outputHeight, DLSSSettings& settings) const;
    // The same question for ray reconstruction, which answers it through its own plugin
    // (slDLSSDGetOptimalSettings) and may well answer it differently for the same mode.
    bool GetRayReconstructionSettings(DLSSQuality quality, uint32_t outputWidth, uint32_t outputHeight,
                                      DLSSSettings& settings) const;
    // Per-frame handle every sl* evaluate call needs, nullptr when unavailable.
    StreamlineFrameToken GetFrameToken(uint32_t frameIndex);

    // Tags the four buffers, pushes the per-frame constants and runs the upscale into
    // desc.colorOut on desc.cmd. Must be called outside a VkRenderPass instance.
    bool EvaluateDLSS(const DLSSEvaluateDesc& desc);
    // The ray reconstruction half: the same four buffers plus the guides, and the same
    // per-frame constants, denoising and upscaling the noisy sample in one step. Must be
    // called outside a VkRenderPass instance too.
    bool EvaluateRayReconstruction(const DLSSEvaluateDesc& desc);
    // Releases the DLSS instance so the next evaluate rebuilds it. Needed when the
    // extents change; the caller owns waiting for the GPU first.
    void ReleaseDLSSResources();
    // The same for the ray reconstruction instance, which is built for its own pair of
    // extents and is not the one above.
    void ReleaseRayReconstructionResources();

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

    // The same for ray reconstruction. Only the LOGGING is driven off these: unlike
    // DLSSOptions, DLSSDOptions carries the view matrices, which move every frame, so
    // slDLSSDSetOptions cannot be skipped on an unchanged mode - see
    // EvaluateRayReconstruction.
    DLSSQuality m_RROptionsQuality = DLSSQuality::DLAA;
    uint32_t m_RROptionsWidth = 0;
    uint32_t m_RROptionsHeight = 0;
    bool b_RREvaluateLogged = false;
  };
}
