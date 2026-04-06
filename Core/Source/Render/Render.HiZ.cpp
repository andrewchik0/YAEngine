#include "Render.h"

#include "DebugMarker.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void Render::CreateHiZResources()
  {
    auto& ctx = m_Backend.GetContext();
    auto hizImage = m_Graph.GetResourceImage(m_HiZResource);
    auto& hizDesc = m_Graph.GetResourceDesc(m_HiZResource);
    uint32_t mipCount = hizDesc.mipLevels;

    m_HiZMipViews.resize(mipCount);
    for (uint32_t mip = 0; mip < mipCount; mip++)
    {
      VkImageViewCreateInfo viewInfo{};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.image = hizImage;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = VK_FORMAT_R32_SFLOAT;
      viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.subresourceRange.baseMipLevel = mip;
      viewInfo.subresourceRange.levelCount = 1;
      viewInfo.subresourceRange.baseArrayLayer = 0;
      viewInfo.subresourceRange.layerCount = 1;

      if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &m_HiZMipViews[mip]) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create Hi-Z mip view %d", mip);
        throw std::runtime_error("Failed to create Hi-Z mip view!");
      }

      YA_DEBUG_NAMEF(ctx.device, VK_OBJECT_TYPE_IMAGE_VIEW,
        m_HiZMipViews[mip], "HiZ Mip %u", mip);
    }

    SetDescription hizSetDesc = {
      .set = 0,
      .bindings = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT }
      }
    };

    m_HiZDescriptorSets.resize(mipCount);
    for (uint32_t mip = 0; mip < mipCount; mip++)
    {
      if (mip == 0)
        m_HiZDescriptorSets[mip].Init(ctx, hizSetDesc);
      else
        m_HiZDescriptorSets[mip].Init(ctx, m_HiZDescriptorSets[0].GetLayout());
    }

    auto& depth = m_Graph.GetResource(m_MainDepth);
    auto& hiZ = m_Graph.GetResource(m_HiZResource);

    for (uint32_t mip = 0; mip < mipCount; mip++)
    {
      if (mip == 0)
      {
        // Mip 0: read from depth buffer
        m_HiZDescriptorSets[mip].WriteCombinedImageSampler(0,
          depth.GetView(), depth.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
      else
      {
        // Mip N: read from full Hi-Z texture (in GENERAL layout during compute)
        m_HiZDescriptorSets[mip].WriteCombinedImageSampler(0,
          hiZ.GetView(), hiZ.GetSampler(), VK_IMAGE_LAYOUT_GENERAL);
      }

      // Storage image write for this mip level
      m_HiZDescriptorSets[mip].WriteStorageImage(1, m_HiZMipViews[mip], VK_IMAGE_LAYOUT_GENERAL);
    }
  }

  void Render::DestroyHiZResources()
  {
    auto& ctx = m_Backend.GetContext();

    for (auto& set : m_HiZDescriptorSets)
      set.Destroy();
    m_HiZDescriptorSets.clear();

    for (auto view : m_HiZMipViews)
    {
      if (view != VK_NULL_HANDLE)
        vkDestroyImageView(ctx.device, view, nullptr);
    }
    m_HiZMipViews.clear();
  }
}
