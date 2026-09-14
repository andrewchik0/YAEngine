#include "StreamlineIntegration.h"

#include "Utils/Log.h"

#ifdef YA_DLSS
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_helpers.h>
#include <sl_helpers_vk.h>
#endif

namespace YAEngine
{
#ifdef YA_DLSS
  namespace
  {
    // NGX refuses an application id it was not issued for and then disables every NGX
    // feature. Until NVIDIA hands out a per-title id, the project id path is the supported
    // way in: SL initializes NGX from the engine name, its version and this GUID.
    constexpr const char* kStreamlineProjectId = "1a4e35a2-4e5b-4f0c-9a3d-6f2c8b7d1e04";

    sl::Feature ToStreamlineFeature(StreamlineFeature feature)
    {
      switch (feature)
      {
        case StreamlineFeature::DLSS: return sl::kFeatureDLSS;
        case StreamlineFeature::RayReconstruction: return sl::kFeatureDLSS_RR;
      }

      return sl::kFeatureDLSS;
    }

    const char* GetFeatureName(StreamlineFeature feature)
    {
      switch (feature)
      {
        case StreamlineFeature::DLSS: return "DLSS";
        case StreamlineFeature::RayReconstruction: return "DLSS Ray Reconstruction";
      }

      return "unknown";
    }

    sl::DLSSMode ToStreamlineMode(DLSSQuality quality)
    {
      switch (quality)
      {
        case DLSSQuality::DLAA: return sl::DLSSMode::eDLAA;
        case DLSSQuality::Quality: return sl::DLSSMode::eMaxQuality;
        case DLSSQuality::Balanced: return sl::DLSSMode::eBalanced;
        case DLSSQuality::Performance: return sl::DLSSMode::eMaxPerformance;
        case DLSSQuality::UltraPerformance: return sl::DLSSMode::eUltraPerformance;
      }

      return sl::DLSSMode::eOff;
    }

    // The engine enum is dense and the SDK one is not - presets A, B and C were removed - so
    // this is a real mapping and not a cast.
    sl::DLSSDPreset ToStreamlinePreset(RayReconstructionPreset preset)
    {
      switch (preset)
      {
        case RayReconstructionPreset::Default: return sl::DLSSDPreset::eDefault;
        case RayReconstructionPreset::D: return sl::DLSSDPreset::ePresetD;
        case RayReconstructionPreset::E: return sl::DLSSDPreset::ePresetE;
        case RayReconstructionPreset::F: return sl::DLSSDPreset::ePresetF;
        case RayReconstructionPreset::G: return sl::DLSSDPreset::ePresetG;
        case RayReconstructionPreset::H: return sl::DLSSDPreset::ePresetH;
        case RayReconstructionPreset::I: return sl::DLSSDPreset::ePresetI;
        case RayReconstructionPreset::J: return sl::DLSSDPreset::ePresetJ;
        case RayReconstructionPreset::K: return sl::DLSSDPreset::ePresetK;
        case RayReconstructionPreset::L: return sl::DLSSDPreset::ePresetL;
        case RayReconstructionPreset::M: return sl::DLSSDPreset::ePresetM;
        case RayReconstructionPreset::N: return sl::DLSSDPreset::ePresetN;
        case RayReconstructionPreset::O: return sl::DLSSDPreset::ePresetO;
        case RayReconstructionPreset::Count: break;
      }

      return sl::DLSSDPreset::eDefault;
    }

    // The engine drives a single scene view, so every tag, constant and evaluate call
    // refers to the same Streamline viewport.
    constexpr uint32_t kStreamlineViewport = 0;

    // sl::float4x4 is row major and multiplies row vectors, GLM is column major and
    // multiplies column vectors, so the row-major image of a GLM matrix is its
    // transpose - which is exactly the raw GLM column layout.
    sl::float4x4 ToStreamlineMatrix(const glm::mat4& m)
    {
      sl::float4x4 out {};
      for (uint32_t i = 0; i < 4; i++)
        out.setRow(i, sl::float4(m[i][0], m[i][1], m[i][2], m[i][3]));

      return out;
    }

    sl::Resource ToStreamlineResource(const DLSSImage& image)
    {
      sl::Resource resource(sl::ResourceType::eTex2d, image.image, nullptr, image.view,
        static_cast<uint32_t>(image.layout));
      resource.width = image.width;
      resource.height = image.height;
      resource.nativeFormat = static_cast<uint32_t>(image.format);
      resource.mipLevels = 1;
      resource.arrayLayers = 1;
      resource.flags = 0;
      // Streamline derives what a resource can be bound as from its format, not from
      // this field, so the two aspects the graph guarantees are enough.
      resource.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
      return resource;
    }

    sl::SubresourceRange ToStreamlineSubresource(const DLSSImage& image)
    {
      sl::SubresourceRange range {};
      range.aspectMask = image.aspect;
      range.baseMipLevel = 0;
      range.levelCount = VK_REMAINING_MIP_LEVELS;
      range.baseArrayLayer = 0;
      range.layerCount = VK_REMAINING_ARRAY_LAYERS;
      return range;
    }

    // The per-frame constants both evaluates push. Identical for the two features - they
    // describe the camera and the frame, not the algorithm - so the conventions below are
    // derived once and neither resolve can drift from the other.
    sl::Constants BuildStreamlineConstants(const DLSSEvaluateDesc& desc)
    {
      glm::mat4 invView = glm::inverse(desc.view);
      glm::mat4 invProj = glm::inverse(desc.proj);
      glm::mat4 clipToPrevClip = desc.prevProj * desc.prevView * invView * invProj;

      sl::Constants constants {};
      constants.cameraViewToClip = ToStreamlineMatrix(desc.proj);
      constants.clipToCameraView = ToStreamlineMatrix(invProj);
      constants.clipToLensClip = ToStreamlineMatrix(glm::mat4(1.0f));
      constants.clipToPrevClip = ToStreamlineMatrix(clipToPrevClip);
      constants.prevClipToClip = ToStreamlineMatrix(glm::inverse(clipToPrevClip));
      constants.jitterOffset = { desc.jitterPixels.x, desc.jitterPixels.y };
      constants.mvecScale = { desc.mvecScale.x, desc.mvecScale.y };
      constants.cameraPinholeOffset = { 0.0f, 0.0f };
      constants.cameraPos = { desc.cameraPosition.x, desc.cameraPosition.y, desc.cameraPosition.z };
      constants.cameraUp = { desc.cameraUp.x, desc.cameraUp.y, desc.cameraUp.z };
      constants.cameraRight = { desc.cameraRight.x, desc.cameraRight.y, desc.cameraRight.z };
      constants.cameraFwd = { desc.cameraForward.x, desc.cameraForward.y, desc.cameraForward.z };
      constants.cameraNear = desc.nearPlane;
      constants.cameraFar = desc.farPlane;
      constants.cameraFOV = desc.fov;
      constants.cameraAspectRatio = desc.aspectRatio;
      // Reversed-Z with an infinite far plane: the sky sits at 0.
      constants.depthInverted = sl::Boolean::eTrue;
      constants.cameraMotionIncluded = sl::Boolean::eTrue;
      constants.motionVectors3D = sl::Boolean::eFalse;
      constants.reset = desc.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
      constants.orthographicProjection = sl::Boolean::eFalse;
      constants.motionVectorsDilated = sl::Boolean::eFalse;
      // computeVelocity adds the jitter back before it stores the vector.
      constants.motionVectorsJittered = sl::Boolean::eFalse;
      return constants;
    }

    void OnStreamlineLogMessage(sl::LogType type, const char* message)
    {
      // SL logs at error level for things it recovers from, such as an optional module
      // that is not part of the SDK drop, so its levels do not map onto engine severity.
      // Real failures come back as sl::Result values and are logged by the caller.
      switch (type)
      {
        case sl::LogType::eError:
        case sl::LogType::eWarn: YA_LOG_WARN("Render", "Streamline: %s", message); break;
        default: YA_LOG_VERBOSE("Render", "Streamline: %s", message); break;
      }
    }
  }
#endif

  void StreamlineIntegration::Init(const std::vector<StreamlineFeature>& features)
  {
#ifdef YA_DLSS
    if (features.empty())
      return;

    std::vector<sl::Feature> featuresToLoad;
    featuresToLoad.reserve(features.size());
    for (StreamlineFeature feature : features)
      featuresToLoad.push_back(ToStreamlineFeature(feature));

    sl::Preferences preferences {};
    preferences.logLevel = sl::LogLevel::eDefault;
    preferences.logMessageCallback = OnStreamlineLogMessage;
    // The engine keeps talking to vulkan-1 directly and only reports what it created,
    // so SL must not try to interpose the Vulkan entry points.
    preferences.flags |= sl::PreferenceFlags::eUseManualHooking;
    // Required by slSetTagForFrame, the only tagging API that knows which frame a
    // resource belongs to and therefore the only one safe with frames in flight.
    preferences.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = featuresToLoad.data();
    preferences.numFeaturesToLoad = static_cast<uint32_t>(featuresToLoad.size());
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "1.0.0";
    preferences.projectId = kStreamlineProjectId;
    // Without this slGetFeatureRequirements reports nothing about Vulkan extensions and queues.
    preferences.renderAPI = sl::RenderAPI::eVulkan;

    sl::Result initResult = slInit(preferences);
    if (initResult != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "Streamline init failed (%s), all Streamline features stay unavailable",
        sl::getResultAsStr(initResult));

      for (StreamlineFeature feature : features)
        m_Features.push_back({ .feature = feature, .unavailableReason = sl::getResultAsStr(initResult) });

      return;
    }

    b_Initialized = true;
    YA_LOG_INFO("Render", "Streamline %u.%u.%u initialized", SL_VERSION_MAJOR, SL_VERSION_MINOR, SL_VERSION_PATCH);

    for (StreamlineFeature feature : features)
    {
      FeatureState state { .feature = feature };

      // No adapter is known yet, this only answers the OS and driver half of the question.
      sl::AdapterInfo adapterInfo {};
      sl::Result result = slIsFeatureSupported(ToStreamlineFeature(feature), adapterInfo);
      state.supported = result == sl::Result::eOk;

      if (state.supported)
      {
        YA_LOG_INFO("Render", "Streamline feature %s passed the driver check", GetFeatureName(feature));
      }
      else
      {
        state.unavailableReason = sl::getResultAsStr(result);
        YA_LOG_WARN("Render", "Streamline feature %s is unavailable: %s", GetFeatureName(feature),
          state.unavailableReason.c_str());
      }

      m_Features.push_back(state);
    }
#else
    (void)features;
#endif
  }

  void StreamlineIntegration::ApplyRequirements(VulkanRequirements& requirements)
  {
#ifdef YA_DLSS
    if (!b_Initialized)
      return;

    // sl.common creates a VkPrivateDataSlot for its swap chain bookkeeping regardless of
    // which feature is loaded, but leaves privateData out of the requirements it reports.
    VkPhysicalDeviceVulkan13Features privateDataFeature { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    privateDataFeature.privateData = VK_TRUE;
    requirements.MergeVulkan13Features(privateDataFeature);

    // Streamline is handed exactly one graphics and one compute queue by slSetVulkanInfo
    // and shares them across every loaded plugin, so what the device has to reserve is the
    // largest single request, not their sum. The registry accumulates, which is right for
    // independent subsystems and wrong here - hence the max first, one request after.
    uint32_t extraGraphicsQueues = 0;
    uint32_t extraComputeQueues = 0;

    for (const FeatureState& state : m_Features)
    {
      // An unsupported feature must not drag NVIDIA extensions into the device we create.
      if (!state.supported)
        continue;

      sl::FeatureRequirements featureRequirements {};
      sl::Result result = slGetFeatureRequirements(ToStreamlineFeature(state.feature), featureRequirements);
      if (result != sl::Result::eOk)
      {
        YA_LOG_WARN("Render", "Streamline requirements for %s could not be read: %s",
          GetFeatureName(state.feature), sl::getResultAsStr(result));
        continue;
      }

      if (!(featureRequirements.flags & sl::FeatureRequirementFlags::eVulkanSupported))
      {
        YA_LOG_WARN("Render", "Streamline feature %s does not support Vulkan", GetFeatureName(state.feature));
        continue;
      }

      for (uint32_t i = 0; i < featureRequirements.vkNumInstanceExtensions; i++)
        requirements.AddInstanceExtension(featureRequirements.vkInstanceExtensions[i]);

      for (uint32_t i = 0; i < featureRequirements.vkNumDeviceExtensions; i++)
        requirements.AddDeviceExtension(featureRequirements.vkDeviceExtensions[i]);

      requirements.MergeVulkan12Features(
        sl::getVkPhysicalDeviceVulkan12Features(featureRequirements.vkNumFeatures12, featureRequirements.vkFeatures12));
      requirements.MergeVulkan13Features(
        sl::getVkPhysicalDeviceVulkan13Features(featureRequirements.vkNumFeatures13, featureRequirements.vkFeatures13));

      extraGraphicsQueues = std::max(extraGraphicsQueues, featureRequirements.vkNumGraphicsQueuesRequired);
      extraComputeQueues = std::max(extraComputeQueues, featureRequirements.vkNumComputeQueuesRequired);

      YA_LOG_INFO("Render", "Streamline feature %s wants %u instance extension(s), %u device extension(s), %u graphics and %u compute queue(s)",
        GetFeatureName(state.feature),
        featureRequirements.vkNumInstanceExtensions,
        featureRequirements.vkNumDeviceExtensions,
        featureRequirements.vkNumGraphicsQueuesRequired,
        featureRequirements.vkNumComputeQueuesRequired);
    }

    requirements.RequestExtraGraphicsQueues(extraGraphicsQueues);
    requirements.RequestExtraComputeQueues(extraComputeQueues);
#else
    (void)requirements;
#endif
  }

  void StreamlineIntegration::SetVulkanInfo(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                                            uint32_t graphicsQueueFamily, uint32_t graphicsQueueIndex,
                                            uint32_t computeQueueFamily, uint32_t computeQueueIndex)
  {
#ifdef YA_DLSS
    if (!b_Initialized)
      return;

    sl::VulkanInfo info {};
    info.instance = instance;
    info.physicalDevice = physicalDevice;
    info.device = device;
    info.graphicsQueueFamily = graphicsQueueFamily;
    info.graphicsQueueIndex = graphicsQueueIndex;
    info.computeQueueFamily = computeQueueFamily;
    info.computeQueueIndex = computeQueueIndex;

    sl::Result result = slSetVulkanInfo(info);
    if (result != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "Streamline rejected the Vulkan device (%s), all Streamline features stay unavailable",
        sl::getResultAsStr(result));

      for (FeatureState& state : m_Features)
        SetFeatureUnavailable(state, sl::getResultAsStr(result));

      return;
    }

    // Support can only shrink once SL sees the real adapter and the created device.
    for (FeatureState& state : m_Features)
    {
      if (!state.supported)
        continue;

      sl::AdapterInfo adapterInfo {};
      adapterInfo.vkPhysicalDevice = physicalDevice;

      sl::Result featureResult = slIsFeatureSupported(ToStreamlineFeature(state.feature), adapterInfo);

      if (featureResult == sl::Result::eOk)
      {
        YA_LOG_INFO("Render", "Streamline feature %s is available on this adapter", GetFeatureName(state.feature));

        // The NGX half is the model itself - nvngx_dlss.dll or nvngx_dlssd.dll - and it is
        // the only number that says which network actually runs. It is NOT the SDK version
        // the init line above prints: the model DLL ships next to the executable and can be
        // replaced independently of the Streamline drop, and the preset letters are indices
        // into whatever model is loaded. Logged so that question is answered by the log
        // rather than by inspecting files on disk.
        sl::FeatureVersion version {};
        if (slGetFeatureVersion(ToStreamlineFeature(state.feature), version) == sl::Result::eOk)
        {
          YA_LOG_INFO("Render", "Streamline feature %s runs plugin %u.%u.%u over NGX model %u.%u.%u",
            GetFeatureName(state.feature),
            version.versionSL.major, version.versionSL.minor, version.versionSL.build,
            version.versionNGX.major, version.versionNGX.minor, version.versionNGX.build);
        }
      }
      else
      {
        SetFeatureUnavailable(state, sl::getResultAsStr(featureResult));
        YA_LOG_WARN("Render", "Streamline feature %s is not available on this adapter: %s",
          GetFeatureName(state.feature), state.unavailableReason.c_str());
      }
    }
#else
    (void)instance;
    (void)physicalDevice;
    (void)device;
    (void)graphicsQueueFamily;
    (void)graphicsQueueIndex;
    (void)computeQueueFamily;
    (void)computeQueueIndex;
#endif
  }

  void StreamlineIntegration::Shutdown()
  {
#ifdef YA_DLSS
    if (!b_Initialized)
      return;

    sl::Result result = slShutdown();
    if (result != sl::Result::eOk)
      YA_LOG_WARN("Render", "Streamline shutdown failed: %s", sl::getResultAsStr(result));
#endif

    m_Features.clear();
    b_Initialized = false;
    m_DLSSOptionsWidth = 0;
    m_DLSSOptionsHeight = 0;
    b_DLSSEvaluateLogged = false;
    m_RROptionsWidth = 0;
    m_RROptionsHeight = 0;
    b_RREvaluateLogged = false;
  }

  bool StreamlineIntegration::IsFeatureSupported(StreamlineFeature feature) const
  {
    const FeatureState* state = FindFeature(feature);
    return state != nullptr && state->supported;
  }

  bool StreamlineIntegration::GetDLSSSettings(DLSSQuality quality, uint32_t outputWidth, uint32_t outputHeight,
                                              DLSSSettings& settings) const
  {
#ifdef YA_DLSS
    if (!IsDLSSAvailable())
      return false;

    sl::DLSSOptions options {};
    options.mode = ToStreamlineMode(quality);
    options.outputWidth = outputWidth;
    options.outputHeight = outputHeight;

    sl::DLSSOptimalSettings optimalSettings {};
    sl::Result result = slDLSSGetOptimalSettings(options, optimalSettings);
    if (result != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "DLSS optimal settings query failed: %s", sl::getResultAsStr(result));
      return false;
    }

    settings = {
      .renderWidth = optimalSettings.optimalRenderWidth,
      .renderHeight = optimalSettings.optimalRenderHeight,
      .renderWidthMin = optimalSettings.renderWidthMin,
      .renderHeightMin = optimalSettings.renderHeightMin,
      .renderWidthMax = optimalSettings.renderWidthMax,
      .renderHeightMax = optimalSettings.renderHeightMax
    };

    return true;
#else
    (void)quality;
    (void)outputWidth;
    (void)outputHeight;
    (void)settings;
    return false;
#endif
  }

  bool StreamlineIntegration::GetRayReconstructionSettings(DLSSQuality quality, uint32_t outputWidth,
                                                           uint32_t outputHeight, DLSSSettings& settings) const
  {
#ifdef YA_DLSS
    if (!IsRayReconstructionAvailable())
      return false;

    // Deliberately NOT the super resolution query: ray reconstruction runs its own network
    // and sizes its input itself, so the same quality mode can want a different render
    // extent here than slDLSSGetOptimalSettings reported for the same output size.
    sl::DLSSDOptions options {};
    options.mode = ToStreamlineMode(quality);
    options.outputWidth = outputWidth;
    options.outputHeight = outputHeight;

    sl::DLSSDOptimalSettings optimalSettings {};
    sl::Result result = slDLSSDGetOptimalSettings(options, optimalSettings);
    if (result != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "Ray reconstruction optimal settings query failed: %s",
        sl::getResultAsStr(result));
      return false;
    }

    settings = {
      .renderWidth = optimalSettings.optimalRenderWidth,
      .renderHeight = optimalSettings.optimalRenderHeight,
      .renderWidthMin = optimalSettings.renderWidthMin,
      .renderHeightMin = optimalSettings.renderHeightMin,
      .renderWidthMax = optimalSettings.renderWidthMax,
      .renderHeightMax = optimalSettings.renderHeightMax
    };

    return true;
#else
    (void)quality;
    (void)outputWidth;
    (void)outputHeight;
    (void)settings;
    return false;
#endif
  }

  StreamlineFrameToken StreamlineIntegration::GetFrameToken(uint32_t frameIndex)
  {
#ifdef YA_DLSS
    if (!b_Initialized)
      return nullptr;

    sl::FrameToken* token = nullptr;
    sl::Result result = slGetNewFrameToken(token, &frameIndex);
    if (result != sl::Result::eOk)
      return nullptr;

    return token;
#else
    (void)frameIndex;
    return nullptr;
#endif
  }

  bool StreamlineIntegration::EvaluateDLSS(const DLSSEvaluateDesc& desc)
  {
#ifdef YA_DLSS
    if (!IsDLSSAvailable() || desc.cmd == VK_NULL_HANDLE || desc.frameToken == nullptr)
      return false;

    sl::FrameToken& frame = *static_cast<sl::FrameToken*>(desc.frameToken);
    sl::ViewportHandle viewport(kStreamlineViewport);

    // A zero width means nothing has been pushed yet - a real output is never 0 wide.
    if (m_DLSSOptionsWidth == 0 || desc.quality != m_DLSSOptionsQuality
      || desc.colorOut.width != m_DLSSOptionsWidth || desc.colorOut.height != m_DLSSOptionsHeight)
    {
      sl::DLSSOptions options {};
      options.mode = ToStreamlineMode(desc.quality);
      options.outputWidth = desc.colorOut.width;
      options.outputHeight = desc.colorOut.height;
      // The engine feeds pre-exposure linear HDR and never tags an exposure buffer,
      // so DLSS derives the exposure itself.
      options.colorBuffersHDR = sl::Boolean::eTrue;
      options.useAutoExposure = sl::Boolean::eTrue;

      sl::Result result = slDLSSSetOptions(viewport, options);
      if (result != sl::Result::eOk)
      {
        YA_LOG_WARN("Render", "DLSS options could not be set: %s", sl::getResultAsStr(result));
        return false;
      }

      m_DLSSOptionsQuality = desc.quality;
      m_DLSSOptionsWidth = desc.colorOut.width;
      m_DLSSOptionsHeight = desc.colorOut.height;
    }

    sl::Resource colorIn = ToStreamlineResource(desc.colorIn);
    sl::Resource colorOut = ToStreamlineResource(desc.colorOut);
    sl::Resource depth = ToStreamlineResource(desc.depth);
    sl::Resource motionVectors = ToStreamlineResource(desc.motionVectors);

    sl::SubresourceRange colorInRange = ToStreamlineSubresource(desc.colorIn);
    sl::SubresourceRange colorOutRange = ToStreamlineSubresource(desc.colorOut);
    sl::SubresourceRange depthRange = ToStreamlineSubresource(desc.depth);
    sl::SubresourceRange motionVectorsRange = ToStreamlineSubresource(desc.motionVectors);

    colorIn.next = &colorInRange;
    colorOut.next = &colorOutRange;
    depth.next = &depthRange;
    motionVectors.next = &motionVectorsRange;

    sl::Extent renderExtent { 0, 0, desc.colorIn.width, desc.colorIn.height };
    sl::Extent outputExtent { 0, 0, desc.colorOut.width, desc.colorOut.height };

    // eValidUntilEvaluate: the graph owns these images and reuses them later in the
    // frame, but nothing touches them between the tag and the evaluate below.
    sl::ResourceTag tags[] = {
      sl::ResourceTag { &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent },
      sl::ResourceTag { &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &outputExtent },
      sl::ResourceTag { &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent },
      sl::ResourceTag { &motionVectors, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &renderExtent }
    };

    sl::Result tagResult = slSetTagForFrame(frame, viewport, tags,
      static_cast<uint32_t>(std::size(tags)), desc.cmd);
    if (tagResult != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "DLSS resources could not be tagged: %s", sl::getResultAsStr(tagResult));
      return false;
    }

    sl::Constants constants = BuildStreamlineConstants(desc);

    sl::Result constantsResult = slSetConstants(constants, frame, viewport);
    if (constantsResult != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "DLSS constants could not be set: %s", sl::getResultAsStr(constantsResult));
      return false;
    }

    const sl::BaseStructure* inputs[] = { &viewport };
    sl::Result evaluateResult = slEvaluateFeature(sl::kFeatureDLSS, frame, inputs,
      static_cast<uint32_t>(std::size(inputs)), desc.cmd);
    if (evaluateResult != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "DLSS evaluate failed: %s", sl::getResultAsStr(evaluateResult));
      return false;
    }

    if (!b_DLSSEvaluateLogged)
    {
      b_DLSSEvaluateLogged = true;
      YA_LOG_INFO("Render", "DLSS evaluate running: %ux%u -> %ux%u",
        desc.colorIn.width, desc.colorIn.height, desc.colorOut.width, desc.colorOut.height);
    }

    // Streamline binds its own pipeline and descriptor sets on this command buffer and,
    // with manual hooking, cannot put back what it replaced. Nothing has to be restored
    // here because every render graph pass rebinds both before it records anything.
    return true;
#else
    (void)desc;
    return false;
#endif
  }

  bool StreamlineIntegration::EvaluateRayReconstruction(const DLSSEvaluateDesc& desc)
  {
#ifdef YA_DLSS
    if (!IsRayReconstructionAvailable() || desc.cmd == VK_NULL_HANDLE || desc.frameToken == nullptr)
      return false;

    // The three demodulation guides are not optional: without them the denoiser has no
    // surface to separate the radiance from and there is nothing to fall back to.
    if (desc.diffuseAlbedo.image == VK_NULL_HANDLE || desc.specularAlbedo.image == VK_NULL_HANDLE
      || desc.normalRoughness.image == VK_NULL_HANDLE)
    {
      YA_LOG_WARN("Render", "Ray reconstruction evaluate skipped: a guide buffer is missing");
      return false;
    }

    sl::FrameToken& frame = *static_cast<sl::FrameToken*>(desc.frameToken);
    sl::ViewportHandle viewport(desc.viewport);

    sl::DLSSDOptions options {};
    options.mode = ToStreamlineMode(desc.quality);
    // PROTOTYPE: the reflection layer instance carries its Fresnel weight in the colour alpha.
    options.alphaUpscalingEnabled = desc.alphaUpscaling ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    options.outputWidth = desc.colorOut.width;
    options.outputHeight = desc.colorOut.height;
    // Mandatory, not a choice: ProgrammingGuideDLSS_RR.md 5.0 says RR only supports HDR
    // input. The same section is why exposure is handled below by two scalars rather than
    // by useAutoExposure the way super resolution does it - RR ignores that option outright
    // and documents no exposure buffer to tag.
    options.colorBuffersHDR = sl::Boolean::eTrue;
    // sl_dlss_d.h: ePacked is "App needs to write Roughness to w channel of Normal
    // resource", which is exactly what pt_guides.comp writes into ptNormalRoughness -
    // one RGBA16F, world normal in xyz and roughness in w. eUnpacked would demand two
    // separate resources and two separate tags.
    options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
    // The companion to kBufferTypeSpecularHitDistance, per section 3.4.9 of the DLSS-RR
    // Integration Guide - RR only builds specular motion vectors out of the pair while that
    // guide is the one tagged - which is why the options move every frame rather than being
    // pushed lazily the way slDLSSSetOptions is. Section 3.4.9 also settles the convention this
    // used to guess at - "All matrices are Row Major Order and use left multiplication" -
    // which is exactly what ToStreamlineMatrix produces out of a GLM matrix.
    //
    // Only worldToCameraView actually reaches the network. The plugin forwards it as
    // pInWorldToViewMatrix, but takes pInViewToClipMatrix from sl::Constants::cameraViewToClip
    // instead of from the field below, so the view to clip transform the guide asks for is
    // the one BuildStreamlineConstants already pushes. cameraViewToWorld is filled in
    // regardless: it is part of the documented struct, and a correct value costs one inverse.
    options.worldToCameraView = ToStreamlineMatrix(desc.view);
    options.cameraViewToWorld = ToStreamlineMatrix(glm::inverse(desc.view));

    // One selection driving every quality mode, since only the one matching options.mode is
    // ever consulted. Section 3.13 recommends shipping the default.
    sl::DLSSDPreset preset = ToStreamlinePreset(desc.rrSettings.preset);
    options.dlaaPreset = preset;
    options.qualityPreset = preset;
    options.balancedPreset = preset;
    options.performancePreset = preset;
    options.ultraPerformancePreset = preset;
    options.ultraQualityPreset = preset;

    sl::Result optionsResult = slDLSSDSetOptions(viewport, options);
    if (optionsResult != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "Ray reconstruction options could not be set: %s",
        sl::getResultAsStr(optionsResult));
      return false;
    }

    // A zero width means nothing has been pushed yet - a real output is never 0 wide - so
    // this is true on the first evaluate of a run and on every mode or extent change after.
    const bool optionsChanged = m_RROptionsWidth == 0 || desc.quality != m_RROptionsQuality
      || desc.colorOut.width != m_RROptionsWidth || desc.colorOut.height != m_RROptionsHeight;

    m_RROptionsQuality = desc.quality;
    m_RROptionsWidth = desc.colorOut.width;
    m_RROptionsHeight = desc.colorOut.height;

    sl::Extent renderExtent { 0, 0, desc.colorIn.width, desc.colorIn.height };
    sl::Extent outputExtent { 0, 0, desc.colorOut.width, desc.colorOut.height };

    // Named locals rather than a container: every tag below holds a POINTER to its
    // resource, so the objects have to sit still until slSetTagForFrame has read them.
    sl::Resource colorIn = ToStreamlineResource(desc.colorIn);
    sl::Resource colorOut = ToStreamlineResource(desc.colorOut);
    sl::Resource depth = ToStreamlineResource(desc.depth);
    sl::Resource motionVectors = ToStreamlineResource(desc.motionVectors);
    sl::Resource diffuseAlbedo = ToStreamlineResource(desc.diffuseAlbedo);
    sl::Resource specularAlbedo = ToStreamlineResource(desc.specularAlbedo);
    sl::Resource normalRoughness = ToStreamlineResource(desc.normalRoughness);
    sl::Resource specularHitDistance = ToStreamlineResource(desc.specularHitDistance);
    sl::Resource specularMotionVectors = ToStreamlineResource(desc.specularMotionVectors);

    sl::SubresourceRange colorInRange = ToStreamlineSubresource(desc.colorIn);
    sl::SubresourceRange colorOutRange = ToStreamlineSubresource(desc.colorOut);
    sl::SubresourceRange depthRange = ToStreamlineSubresource(desc.depth);
    sl::SubresourceRange motionVectorsRange = ToStreamlineSubresource(desc.motionVectors);
    sl::SubresourceRange diffuseAlbedoRange = ToStreamlineSubresource(desc.diffuseAlbedo);
    sl::SubresourceRange specularAlbedoRange = ToStreamlineSubresource(desc.specularAlbedo);
    sl::SubresourceRange normalRoughnessRange = ToStreamlineSubresource(desc.normalRoughness);
    sl::SubresourceRange specularHitDistanceRange = ToStreamlineSubresource(desc.specularHitDistance);
    sl::SubresourceRange specularMotionVectorsRange = ToStreamlineSubresource(desc.specularMotionVectors);

    colorIn.next = &colorInRange;
    colorOut.next = &colorOutRange;
    depth.next = &depthRange;
    motionVectors.next = &motionVectorsRange;
    diffuseAlbedo.next = &diffuseAlbedoRange;
    specularAlbedo.next = &specularAlbedoRange;
    normalRoughness.next = &normalRoughnessRange;
    specularHitDistance.next = &specularHitDistanceRange;
    specularMotionVectors.next = &specularMotionVectorsRange;

    // eValidUntilEvaluate for every one of them, exactly as the super resolution path
    // does: the graph owns these images and reuses them later in the frame, but nothing
    // touches them between the tag and the evaluate below.
    constexpr sl::ResourceLifecycle kLifecycle = sl::ResourceLifecycle::eValidUntilEvaluate;

    std::vector<sl::ResourceTag> tags;
    tags.reserve(8);
    tags.emplace_back(&colorIn, sl::kBufferTypeScalingInputColor, kLifecycle, &renderExtent);
    tags.emplace_back(&colorOut, sl::kBufferTypeScalingOutputColor, kLifecycle, &outputExtent);
    tags.emplace_back(&depth, sl::kBufferTypeDepth, kLifecycle, &renderExtent);
    tags.emplace_back(&motionVectors, sl::kBufferTypeMotionVectors, kLifecycle, &renderExtent);
    tags.emplace_back(&diffuseAlbedo, sl::kBufferTypeAlbedo, kLifecycle, &renderExtent);
    tags.emplace_back(&specularAlbedo, sl::kBufferTypeSpecularAlbedo, kLifecycle, &renderExtent);
    tags.emplace_back(&normalRoughness, sl::kBufferTypeNormalRoughness, kLifecycle, &renderExtent);
    // Both specular guides are optional and they are alternatives: section 4.1.9 of the DLSS-RR
    // guide needs the hit distance only when specular motion vectors are not provided. So only
    // the one the settings name is pushed, never both, and an empty image drops it too.
    const RayReconstructionSpecularGuide specularGuide = desc.rrSettings.specularGuide;
    if (specularGuide == RayReconstructionSpecularGuide::MotionVectors
      && desc.specularMotionVectors.image != VK_NULL_HANDLE)
      tags.emplace_back(&specularMotionVectors, sl::kBufferTypeSpecularMotionVectors, kLifecycle, &renderExtent);
    else if (specularGuide == RayReconstructionSpecularGuide::HitDistance
      && desc.specularHitDistance.image != VK_NULL_HANDLE)
      tags.emplace_back(&specularHitDistance, sl::kBufferTypeSpecularHitDistance, kLifecycle, &renderExtent);

    uint32_t tagCount = static_cast<uint32_t>(tags.size());
    sl::Result tagResult = slSetTagForFrame(frame, viewport, tags.data(), tagCount, desc.cmd);
    if (tagResult != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "Ray reconstruction resources could not be tagged: %s",
        sl::getResultAsStr(tagResult));
      return false;
    }

    sl::Constants constants = BuildStreamlineConstants(desc);

    sl::Result constantsResult = slSetConstants(constants, frame, viewport);
    if (constantsResult != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "Ray reconstruction constants could not be set: %s",
        sl::getResultAsStr(constantsResult));
      return false;
    }

    const sl::BaseStructure* inputs[] = { &viewport };
    sl::Result evaluateResult = slEvaluateFeature(sl::kFeatureDLSS_RR, frame, inputs,
      static_cast<uint32_t>(std::size(inputs)), desc.cmd);
    if (evaluateResult != sl::Result::eOk)
    {
      YA_LOG_WARN("Render", "Ray reconstruction evaluate failed: %s",
        sl::getResultAsStr(evaluateResult));
      return false;
    }

    const bool layerFirstLog = desc.viewport != 0 && !b_RRLayerEvaluateLogged;
    if (optionsChanged || !b_RREvaluateLogged || layerFirstLog)
    {
      b_RREvaluateLogged = true;
      if (desc.viewport != 0)
        b_RRLayerEvaluateLogged = true;
      // PROTOTYPE: SL's estimate is global rather than per viewport (see sl.dlss_d getData), so
      // the second instance's cost is the difference against a single-viewport run.
      sl::DLSSDState state {};
      uint64_t vramBytes = slDLSSDGetState(viewport, state) == sl::Result::eOk
        ? state.estimatedVRAMUsageInBytes : 0;
      YA_LOG_INFO("Render", "Ray reconstruction evaluate running (viewport %u): %ux%u -> %ux%u, %u tag(s), SL VRAM estimate %llu MB",
        desc.viewport, desc.colorIn.width, desc.colorIn.height, desc.colorOut.width,
        desc.colorOut.height, tagCount, static_cast<unsigned long long>(vramBytes / (1024 * 1024)));
    }

    // Same caveat as the super resolution evaluate: Streamline leaves its own pipeline and
    // descriptor sets bound and every graph pass rebinds both before it records anything.
    return true;
#else
    (void)desc;
    return false;
#endif
  }

  void StreamlineIntegration::ReleaseDLSSResources()
  {
#ifdef YA_DLSS
    if (!IsDLSSAvailable())
      return;

    sl::Result result = slFreeResources(sl::kFeatureDLSS, sl::ViewportHandle(kStreamlineViewport));
    // Nothing was allocated yet on the first extent change of a run, which is not an error.
    if (result != sl::Result::eOk && result != sl::Result::eErrorInvalidParameter)
      YA_LOG_WARN("Render", "DLSS resources could not be released: %s", sl::getResultAsStr(result));

    m_DLSSOptionsWidth = 0;
    m_DLSSOptionsHeight = 0;
    b_DLSSEvaluateLogged = false;
#endif
  }

  void StreamlineIntegration::ReleaseRayReconstructionResources()
  {
#ifdef YA_DLSS
    if (!IsRayReconstructionAvailable())
      return;

    // PROTOTYPE: viewport 1 is the dielectric reflection layer instance.
    for (uint32_t viewportId : { kStreamlineViewport, kStreamlineViewport + 1 })
    {
      sl::Result result = slFreeResources(sl::kFeatureDLSS_RR, sl::ViewportHandle(viewportId));
      // Nothing was allocated yet on the first extent change of a run, which is not an error.
      if (result != sl::Result::eOk && result != sl::Result::eErrorInvalidParameter)
        YA_LOG_WARN("Render", "Ray reconstruction resources could not be released (viewport %u): %s",
          viewportId, sl::getResultAsStr(result));
    }

    m_RROptionsWidth = 0;
    m_RROptionsHeight = 0;
    b_RREvaluateLogged = false;
    b_RRLayerEvaluateLogged = false;
#endif
  }

  const std::string& StreamlineIntegration::GetUnavailableReason(StreamlineFeature feature) const
  {
    static const std::string empty;

    const FeatureState* state = FindFeature(feature);
    return state != nullptr ? state->unavailableReason : empty;
  }

  void StreamlineIntegration::SetFeatureUnavailable(FeatureState& state, const char* reason)
  {
    state.supported = false;
    state.unavailableReason = reason;
  }

  const StreamlineIntegration::FeatureState* StreamlineIntegration::FindFeature(StreamlineFeature feature) const
  {
    for (const FeatureState& state : m_Features)
    {
      if (state.feature == feature)
        return &state;
    }

    return nullptr;
  }
}
