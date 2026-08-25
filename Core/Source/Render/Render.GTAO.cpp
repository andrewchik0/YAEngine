#include "Render.h"

#include "DebugMarker.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    // Hilbert-curve pixel index (ported from XeGTAO/Intel, MIT, via shadertoy.com/view/3tB3z3):
    // walking the R2 sequence along this curve instead of scanline order gives the noise its
    // blue-noise separation between neighbouring pixels.
    uint32_t HilbertIndex(uint32_t posX, uint32_t posY)
    {
      uint32_t index = 0;
      for (uint32_t curLevel = GTAO_HILBERT_WIDTH / 2; curLevel > 0; curLevel /= 2)
      {
        uint32_t regionX = (posX & curLevel) > 0 ? 1u : 0u;
        uint32_t regionY = (posY & curLevel) > 0 ? 1u : 0u;
        index += curLevel * curLevel * ((3u * regionX) ^ regionY);

        if (regionY == 0)
        {
          if (regionX == 1)
          {
            posX = uint32_t(GTAO_HILBERT_WIDTH - 1) - posX;
            posY = uint32_t(GTAO_HILBERT_WIDTH - 1) - posY;
          }

          uint32_t temp = posX;
          posX = posY;
          posY = temp;
        }
      }
      return index;
    }

    void QualityLevelToSampleCounts(int qualityLevel, int& outSliceCount, int& outStepsPerSlice)
    {
      switch (qualityLevel)
      {
        case GTAO_QUALITY_LOW:    outSliceCount = 1; outStepsPerSlice = 2; break;
        case GTAO_QUALITY_MEDIUM: outSliceCount = 2; outStepsPerSlice = 2; break;
        case GTAO_QUALITY_ULTRA:  outSliceCount = 9; outStepsPerSlice = 3; break;
        case GTAO_QUALITY_HIGH:
        default:                  outSliceCount = 3; outStepsPerSlice = 3; break;
      }
    }
  }

  void Render::InitGTAOStaticResources()
  {
    auto& ctx = m_Backend.GetContext();

    // Stored as float rather than an integer format so the sampler stays a plain sampler2D:
    // the largest index is 4095 and is exact in fp32.
    {
      std::array<float, GTAO_HILBERT_WIDTH * GTAO_HILBERT_WIDTH> hilbertData;
      for (uint32_t y = 0; y < GTAO_HILBERT_WIDTH; y++)
      {
        for (uint32_t x = 0; x < GTAO_HILBERT_WIDTH; x++)
          hilbertData[y * GTAO_HILBERT_WIDTH + x] = float(HilbertIndex(x, y));
      }

      m_GTAOHilbertLUT.Load(ctx, hilbertData.data(),
        GTAO_HILBERT_WIDTH, GTAO_HILBERT_WIDTH, sizeof(float), VK_FORMAT_R32_SFLOAT);
      YA_DEBUG_NAME(ctx.device, VK_OBJECT_TYPE_IMAGE,
        m_GTAOHilbertLUT.GetImage(), "GTAO Hilbert LUT");
    }

    // One buffer per frame in flight: the noise index advances every frame, so a single
    // buffer would be rewritten while an earlier frame still reads it.
    m_GTAOConstantsUBOs.resize(m_Backend.GetMaxFramesInFlight());
    for (size_t i = 0; i < m_GTAOConstantsUBOs.size(); i++)
    {
      m_GTAOConstantsUBOs[i].Create(ctx, sizeof(GTAOConstants));
      YA_DEBUG_NAMEF(ctx.device, VK_OBJECT_TYPE_BUFFER,
        m_GTAOConstantsUBOs[i].Get(), "GTAO Constants UBO %zu", i);
    }
  }

  void Render::CreateGTAOResources()
  {
    auto& ctx = m_Backend.GetContext();
    auto depthImage = m_Graph.GetResourceImage(m_GTAODepth);

    // The prefilter writes all five mips in one dispatch, so it needs a single-mip storage
    // view per level - the sampled view the graph owns covers the whole chain and cannot be
    // bound as a storage image.
    m_GTAODepthMipViews.resize(GTAO_DEPTH_MIP_LEVELS);
    for (uint32_t mip = 0; mip < GTAO_DEPTH_MIP_LEVELS; mip++)
    {
      VkImageViewCreateInfo viewInfo{};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.image = depthImage;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = VK_FORMAT_R16_SFLOAT;
      viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.subresourceRange.baseMipLevel = mip;
      viewInfo.subresourceRange.levelCount = 1;
      viewInfo.subresourceRange.baseArrayLayer = 0;
      viewInfo.subresourceRange.layerCount = 1;

      if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_GTAODepthMipViews[mip]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create GTAO depth mip view %u", mip);
        throw std::runtime_error("Failed to create GTAO depth mip view!");
      }

      YA_DEBUG_NAMEF(ctx.device, VK_OBJECT_TYPE_IMAGE_VIEW,
        m_GTAODepthMipViews[mip], "GTAO Depth Mip %u", mip);
    }

    for (auto& set : m_GTAOPrefilterDescriptorSets)
    {
      for (uint32_t mip = 0; mip < GTAO_DEPTH_MIP_LEVELS; mip++)
        set.WriteStorageImage(2 + mip, m_GTAODepthMipViews[mip], VK_IMAGE_LAYOUT_GENERAL);
    }
  }

  void Render::DestroyGTAOResources()
  {
    auto& ctx = m_Backend.GetContext();

    for (auto view : m_GTAODepthMipViews)
    {
      if (view != VK_NULL_HANDLE)
        vkDestroyImageView(ctx.device, view, nullptr);
    }
    m_GTAODepthMipViews.clear();
  }

  void Render::UpdateGTAOConstants(uint32_t frameIndex)
  {
    const auto& uniforms = m_FrameUniformBuffer.uniforms;

    float width = float(std::max(uniforms.screenWidth, 1));
    float height = float(std::max(uniforms.screenHeight, 1));

    // Diagonals only (symmetric frustum) beats re-deriving FOV/aspect. abs() is load-bearing:
    // proj[1][1] is Vulkan-Y-flip negated, but the main pass's screen march assumes GTAO space
    // has Y up - using the raw sign would invert that and mirror every sample.
    float tanHalfFOVX = 1.0f / uniforms.proj[0][0];
    float tanHalfFOVY = 1.0f / std::abs(uniforms.proj[1][1]);

    GTAOConstants consts{};
    consts.viewportPixelSize = glm::vec2(1.0f / width, 1.0f / height);

    // Maps screen UV to a view ray in GTAO space: X right, Y up, Z forward and positive.
    // The y terms are negated because the screen v axis runs the other way.
    consts.ndcToViewMul = glm::vec2(tanHalfFOVX * 2.0f, tanHalfFOVY * -2.0f);
    consts.ndcToViewAdd = glm::vec2(tanHalfFOVX * -1.0f, tanHalfFOVY * 1.0f);
    consts.ndcToViewMulPixelSize = consts.ndcToViewMul * consts.viewportPixelSize;

    consts.effectRadius = m_AORadius;
    consts.effectFalloffRange = m_AOFalloffRange;
    consts.radiusMultiplier = m_AORadiusMultiplier;
    consts.finalValuePower = m_AOFinalValuePower;

    // A huge center weight makes the denoise a no-op without needing a second pipeline,
    // which is how the reference switches it off too.
    consts.denoiseBlurBeta = b_AODenoiseEnabled ? 1.2f : 1e4f;
    consts.sampleDistributionPower = m_AOSampleDistributionPower;
    consts.thinOccluderCompensation = m_AOThinOccluderCompensation;
    consts.depthMipSamplingOffset = m_AODepthMipSamplingOffset;

    // Without TAA there is nothing to integrate the rotating noise, so it has to stay put
    // or the image crawls.
    consts.noiseIndex = b_TAAEnabled ? int(m_GlobalFrameIndex % 64) : 0;
    QualityLevelToSampleCounts(m_AOQualityLevel, consts.sliceCount, consts.stepsPerSlice);
    consts.padding0 = 0;

    m_GTAOConstantsUBOs[frameIndex].Update(consts);
  }
}
