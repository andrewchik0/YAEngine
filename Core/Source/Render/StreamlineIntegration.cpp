#include "StreamlineIntegration.h"

#include "Utils/Log.h"

#ifdef YA_DLSS
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
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
      }

      return sl::kFeatureDLSS;
    }

    const char* GetFeatureName(StreamlineFeature feature)
    {
      switch (feature)
      {
        case StreamlineFeature::DLSS: return "DLSS";
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

      requirements.RequestExtraGraphicsQueues(featureRequirements.vkNumGraphicsQueuesRequired);
      requirements.RequestExtraComputeQueues(featureRequirements.vkNumComputeQueuesRequired);

      YA_LOG_INFO("Render", "Streamline feature %s wants %u instance extension(s), %u device extension(s), %u graphics and %u compute queue(s)",
        GetFeatureName(state.feature),
        featureRequirements.vkNumInstanceExtensions,
        featureRequirements.vkNumDeviceExtensions,
        featureRequirements.vkNumGraphicsQueuesRequired,
        featureRequirements.vkNumComputeQueuesRequired);
    }
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
