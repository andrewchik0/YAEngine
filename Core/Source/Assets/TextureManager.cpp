#include "TextureManager.h"


#define STB_IMAGE_IMPLEMENTATION
#include <StbImage/stb_image.h>

namespace YAEngine
{
  TextureHandle TextureManager::Load(const std::string& path)
  {
    auto texture = std::make_unique<Texture>();
    int32_t width, height, channels;

    auto filePath = path;

    if (void* data = stbi_load(filePath.c_str(), &width, &height, &channels, 4))
    {
      texture->m_VulkanTexture.Load(data, width, height, 4, VK_FORMAT_R8G8B8A8_SRGB);
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
    Get(handle).m_VulkanTexture.Destroy();
    Remove(handle);
  }

  void TextureManager::DestroyAll()
  {
    for (auto& texture : GetAll())
    {
      texture.second.get()->m_VulkanTexture.Destroy();
    }
    GetAll().clear();
  }
}
