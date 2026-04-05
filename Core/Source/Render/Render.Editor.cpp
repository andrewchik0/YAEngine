#ifdef YA_EDITOR

#include "Render.h"

#include <ImGui/imgui_impl_vulkan.h>

#include "TileCullData.h"

namespace YAEngine
{
  void Render::CreateSceneImGuiDescriptor()
  {
    auto& sceneImage = m_Graph.GetResource(m_SceneColor);
    m_SceneImGuiDescriptor = ImGui_ImplVulkan_AddTexture(
      sceneImage.GetSampler(),
      sceneImage.GetView(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  void Render::DestroySceneImGuiDescriptor()
  {
    if (m_SceneImGuiDescriptor != VK_NULL_HANDLE)
    {
      ImGui_ImplVulkan_RemoveTexture(m_SceneImGuiDescriptor);
      m_SceneImGuiDescriptor = VK_NULL_HANDLE;
    }
  }

  void Render::RequestViewportResize(uint32_t w, uint32_t h)
  {
    m_PendingViewportWidth = w;
    m_PendingViewportHeight = h;
  }

  void Render::InitShaderHotReload(ThreadPool* threadPool)
  {
    m_ShaderHotReload.Init(&m_PSOCache, m_Backend.GetContext().device, threadPool);
  }

  void Render::ResizeViewport()
  {
    auto& ctx = m_Backend.GetContext();

    // Wait for all GPU work to complete before destroying resources
    vkDeviceWaitIdle(ctx.device);

    uint32_t w = m_PendingViewportWidth;
    uint32_t h = m_PendingViewportHeight;

    // Update HiZ mip count for new dimensions
    uint32_t hizMipCount = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
    m_Graph.SetResourceMipLevels(m_HiZResource, hizMipCount);

    DestroyHiZResources();
    DestroySceneImGuiDescriptor();

    m_Graph.Resize({w, h});

    CreateSceneImGuiDescriptor();
    CreateHiZResources();

    // Resize tile light buffer and update descriptor sets that reference it
    {
      uint32_t tileCountX = (w + TILE_SIZE - 1) / TILE_SIZE;
      uint32_t tileCountY = (h + TILE_SIZE - 1) / TILE_SIZE;
      m_TileLightBuffer.Resize(ctx, tileCountX, tileCountY);
      VkDeviceSize tileBufferSize = tileCountX * tileCountY * sizeof(TileData);
      for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
      {
        m_DeferredLightingLightDescriptorSets[i].WriteStorageBuffer(1,
          m_TileLightBuffer.GetBuffer(uint32_t(i)), tileBufferSize);
      }
    }

    // Recreate TAA external framebuffers
    for (auto& fb : m_TAAFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    m_TAADepth.Destroy(ctx);
    CreateTAAFramebuffers();
    ClearHistoryBuffers();

    m_ViewportWidth = w;
    m_ViewportHeight = h;
  }
}

#endif
