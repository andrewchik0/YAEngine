#include "Render.h"

#include "Utils/Log.h"

// Debug-only frame capture. Dumps raw render targets for N consecutive frames so temporal
// instability can be measured offline instead of judged by eye. Armed via YA_CAPTURE_DIR.

namespace YAEngine
{
  namespace
  {
    uint32_t BytesPerPixel(VkFormat format)
    {
      switch (format)
      {
      case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
      case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
      case VK_FORMAT_R8G8B8A8_UNORM:
      case VK_FORMAT_R8G8B8A8_SRGB:       return 4;
      case VK_FORMAT_R16G16_SFLOAT:       return 4;
      case VK_FORMAT_R8_UNORM:            return 1;
      default:                            return 0;
      }
    }

    // Deliberately not using TransitionImageLayout: it asserts on layout pairs it has no
    // barrier recipe for, and capture touches images in whatever state the graph left them.
    void CaptureBarrier(VkCommandBuffer cmd, VkImage image,
                        VkImageLayout oldLayout, VkImageLayout newLayout)
    {
      VkImageMemoryBarrier barrier{};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = oldLayout;
      barrier.newLayout = newLayout;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = image;
      barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
      barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

      vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
  }

  void Render::InitFrameCapture()
  {
    const char* dir = std::getenv("YA_CAPTURE_DIR");
    if (dir == nullptr || *dir == '\0')
      return;

    m_CaptureDir = dir;

    auto readCount = [](const char* name, int& target) {
      const char* value = std::getenv(name);
      if (value == nullptr) return;

      int parsed = std::atoi(value);
      if (parsed > 0)
        target = parsed;
      else
        YA_LOG_WARN("Render", "Capture: ignoring %s='%s', expected a positive integer", name, value);
    };

    readCount("YA_CAPTURE_WARMUP", m_CaptureWarmup);
    readCount("YA_CAPTURE_FRAMES", m_CaptureFramesLeft);

    std::error_code ec;
    std::filesystem::create_directories(m_CaptureDir, ec);

    YA_LOG_INFO("Render", "Frame capture armed: dir='%s' warmup=%d frames=%d",
      m_CaptureDir.c_str(), m_CaptureWarmup, m_CaptureFramesLeft);
  }

  void Render::CaptureFrame()
  {
    if (m_CaptureDir.empty() || m_CaptureFramesLeft <= 0)
      return;

    if (m_CaptureWarmup > 0)
    {
      m_CaptureWarmup--;
      return;
    }

    auto& ctx = m_Backend.GetContext();
    vkDeviceWaitIdle(ctx.device);

    // historyWrite for this frame - m_TAAIndex has not been advanced yet at the call site
    RGHandle taaOutput = m_TAAIndex == 0 ? m_TAAHistory0 : m_TAAHistory1;

    struct Target { const char* name; RGHandle handle; };
    std::vector<Target> targets = {
      { "lit",     m_LitColor },   // deferred lighting output, before SSR and TAA
      { "ssr",     m_SSRColor },   // TAA input
      { "taa",     taaOutput  },   // TAA output, this frame's history
    };
#ifdef YA_EDITOR
    // Final image: TAA result plus everything tonemap adds on top of it
    targets.push_back({ "final", m_SceneColor });
#endif

    VkExtent2D extent = m_Graph.GetExtent();

    for (const auto& target : targets)
    {
      auto& image = m_Graph.GetResource(target.handle);
      VkFormat format = m_Graph.GetResourceDesc(target.handle).format;
      uint32_t bpp = BytesPerPixel(format);

      if (bpp == 0)
      {
        YA_LOG_WARN("Render", "Capture: unsupported format %d for '%s'", int(format), target.name);
        continue;
      }

      // After a resize the graph recreates images with UNDEFINED layout. Restoring to
      // UNDEFINED is illegal, and the contents would be garbage anyway.
      VkImageLayout original = image.GetLayout();
      if (original == VK_IMAGE_LAYOUT_UNDEFINED)
      {
        YA_LOG_WARN("Render", "Capture: '%s' has no valid layout yet, skipping", target.name);
        continue;
      }

      VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * bpp;
      auto staging = VulkanBuffer::CreateReadback(ctx, size);

      VkCommandBuffer cmd = ctx.commandBuffer->BeginSingleTimeCommands();

      CaptureBarrier(cmd, image.GetImage(), original, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

      VkBufferImageCopy region{};
      region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      region.imageExtent = { extent.width, extent.height, 1 };
      vkCmdCopyImageToBuffer(cmd, image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.Get(), 1, &region);

      CaptureBarrier(cmd, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, original);

      ctx.commandBuffer->EndSingleTimeCommands(cmd);

      char path[512];
      std::snprintf(path, sizeof(path), "%s/%s_%03d.bin",
        m_CaptureDir.c_str(), target.name, m_CaptureIndex);

      std::ofstream out(path, std::ios::binary);
      if (out)
        out.write(static_cast<const char*>(staging.GetMapped()), std::streamsize(size));
      else
        YA_LOG_ERROR("Render", "Capture: failed to open '%s' for writing", path);
      out.close();

      staging.Destroy(ctx);

      if (m_CaptureIndex == 0)
      {
        char manifest[512];
        std::snprintf(manifest, sizeof(manifest), "%s/manifest.txt", m_CaptureDir.c_str());
        // Truncate on the first target of the first frame so a re-run into the same
        // directory does not accumulate stale entries
        std::ofstream m(manifest, b_CaptureManifestOpen ? std::ios::app : std::ios::trunc);
        m << target.name << " " << extent.width << " " << extent.height
          << " " << int(format) << " " << bpp << "\n";
        b_CaptureManifestOpen = true;
      }
    }

    m_CaptureIndex++;
    m_CaptureFramesLeft--;

    if (m_CaptureFramesLeft == 0)
      YA_LOG_INFO("Render", "Frame capture complete: %d frames in '%s'",
        m_CaptureIndex, m_CaptureDir.c_str());
  }
}
