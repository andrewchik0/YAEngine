#include "TextureManager.h"

#include "Render/RenderContext.h"

#define STB_IMAGE_IMPLEMENTATION
#include <StbImage/stb_image.h>

namespace YAEngine
{
  TextureHandle TextureManager::Load(const std::string& path, bool* hasAlpha, bool linear)
  {
    auto texture = std::make_unique<Texture>();
    int32_t width, height, channels;

    auto filePath = path;

    if (void* data = stbi_load(filePath.c_str(), &width, &height, &channels, 4))
    {
      if (hasAlpha != nullptr)
      {
        *hasAlpha = CheckAlpha(data, width, height);
      }
      VkFormat format = linear ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
      texture->m_VulkanTexture.Load(*m_Ctx, data, width, height, 4, format);
      stbi_image_free(data);

      return AssetManagerBase::Load(std::move(texture));
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
  }

  void TextureManager::DestroyAll()
  {
    for (auto& texture : GetAll())
    {
      texture.second.get()->m_VulkanTexture.Destroy(*m_Ctx);
    }
    GetAll().clear();
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
