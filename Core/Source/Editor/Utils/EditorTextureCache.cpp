#include "Editor/Utils/EditorTextureCache.h"

#include <imgui_impl_vulkan.h>

#include "Assets/AssetManager.h"
#include "Render/VulkanTexture.h"

namespace YAEngine
{
  void EditorTextureCache::Init(AssetManager* assetManager)
  {
    m_AssetManager = assetManager;
  }

  void EditorTextureCache::Destroy()
  {
    for (auto& [handle, ds] : m_Cache)
      ImGui_ImplVulkan_RemoveTexture(ds);

    m_Cache.clear();
  }

  VkDescriptorSet EditorTextureCache::GetOrRegister(TextureHandle handle)
  {
    if (!handle)
      return VK_NULL_HANDLE;

    auto it = m_Cache.find(handle);
    if (it != m_Cache.end())
    {
      if (m_AssetManager->Textures().Has(handle))
        return it->second;

      ImGui_ImplVulkan_RemoveTexture(it->second);
      m_Cache.erase(it);
      return VK_NULL_HANDLE;
    }

    auto& vulkanTex = m_AssetManager->Textures().GetVulkanTexture(handle);
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
      vulkanTex.GetSampler(),
      vulkanTex.GetView(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_Cache[handle] = ds;
    return ds;
  }

  void EditorTextureCache::Invalidate(TextureHandle handle)
  {
    auto it = m_Cache.find(handle);
    if (it != m_Cache.end())
    {
      ImGui_ImplVulkan_RemoveTexture(it->second);
      m_Cache.erase(it);
    }
  }
}
