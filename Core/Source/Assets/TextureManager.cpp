#include "TextureManager.h"

#include <filesystem>

#include "Render/RenderContext.h"

#define STB_IMAGE_IMPLEMENTATION
#include <StbImage/stb_image.h>

namespace YAEngine
{
  TextureHandle TextureManager::Load(const std::string& path, bool* hasAlpha, bool linear)
  {
    auto canonical = std::filesystem::weakly_canonical(path).string();
    CacheKey key { canonical, linear };

    auto it = m_Cache.find(key);
    if (it != m_Cache.end() && Has(it->second))
    {
      return it->second;
    }

    auto texture = std::make_unique<Texture>();
    int32_t width, height, channels;

    if (void* data = stbi_load(path.c_str(), &width, &height, &channels, 4))
    {
      if (hasAlpha != nullptr)
      {
        *hasAlpha = CheckAlpha(data, width, height);
      }
      VkFormat format = linear ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
      texture->m_VulkanTexture.Load(*m_Ctx, data, width, height, 4, format);
      stbi_image_free(data);

      auto handle = Store(std::move(texture));
      m_Cache[key] = handle;
      return handle;
    }
    else
    {
      throw std::runtime_error("Texture file does not exist!");
    }
  }

  void TextureManager::Destroy(TextureHandle handle)
  {
    Get(handle).m_VulkanTexture.Destroy(*m_Ctx);
    Remove(handle);

    std::erase_if(m_Cache, [&](const auto& pair) {
      return pair.second == handle;
    });
  }

  void TextureManager::DestroyAll()
  {
    ForEach([this](Texture& texture) {
      texture.m_VulkanTexture.Destroy(*m_Ctx);
    });
    Clear();
    m_Cache.clear();
  }

  bool TextureManager::CheckAlpha(void* data, uint32_t width, uint32_t height)
  {
    uint8_t* pixels = (uint8_t *)data;

    for (uint32_t i = 3; i < width * height; i += 4)
    {
      if (pixels[i] < 250)
        return true;
    }
    return false;
  }
}
