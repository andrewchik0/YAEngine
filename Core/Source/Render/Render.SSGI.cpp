#include "Render.h"

#include "DebugMarker.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void Render::CreateSSGIResources()
  {
    auto& ctx = m_Backend.GetContext();
    auto radianceImage = m_Graph.GetResourceImage(m_SSGIRadiance);

    // The radiance prefilter writes all five mips in one dispatch, so it needs a
    // single-mip storage view per level - same reason as the GTAO depth pyramid.
    m_SSGIRadianceMipViews.resize(GTAO_DEPTH_MIP_LEVELS);
    for (uint32_t mip = 0; mip < GTAO_DEPTH_MIP_LEVELS; mip++)
    {
      VkImageViewCreateInfo viewInfo{};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.image = radianceImage;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.subresourceRange.baseMipLevel = mip;
      viewInfo.subresourceRange.levelCount = 1;
      viewInfo.subresourceRange.baseArrayLayer = 0;
      viewInfo.subresourceRange.layerCount = 1;

      if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_SSGIRadianceMipViews[mip]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create SSGI radiance mip view %u", mip);
        throw std::runtime_error("Failed to create SSGI radiance mip view!");
      }

      YA_DEBUG_NAMEF(ctx.device, VK_OBJECT_TYPE_IMAGE_VIEW,
        m_SSGIRadianceMipViews[mip], "SSGI Radiance Mip %u", mip);
    }

    for (auto& set : m_SSGIPrefilterDescriptorSets)
    {
      for (uint32_t mip = 0; mip < GTAO_DEPTH_MIP_LEVELS; mip++)
        set.WriteStorageImage(3 + mip, m_SSGIRadianceMipViews[mip], VK_IMAGE_LAYOUT_GENERAL);
    }

    // A resize replaced the radiance image, so whatever reprojection it held is gone.
    b_SSGIInvalidatePending = true;
  }

  void Render::DestroySSGIResources()
  {
    auto& ctx = m_Backend.GetContext();

    for (auto view : m_SSGIRadianceMipViews)
    {
      if (view != VK_NULL_HANDLE)
        vkDestroyImageView(ctx.device, view, nullptr);
    }
    m_SSGIRadianceMipViews.clear();
  }
}
