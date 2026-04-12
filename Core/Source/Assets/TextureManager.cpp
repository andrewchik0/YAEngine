#include "TextureManager.h"

#include "Render/RenderContext.h"
#include "Utils/Log.h"
#include "Utils/MipGenerator.h"

#define STB_IMAGE_IMPLEMENTATION
#include <Stb/stb_image.h>

namespace YAEngine
{
  TextureHandle TextureManager::Load(const std::string& path, bool* hasAlpha, bool linear)
  {
    auto canonical = std::filesystem::weakly_canonical(path).string();
    CacheKey key { canonical, linear };

    auto it = m_Cache.find(key);
    if (it != m_Cache.end() && Has(it->second))
    {
      if (hasAlpha != nullptr)
        *hasAlpha = Get(it->second).m_HasAlpha;
      return it->second;
    }

    auto texture = std::make_unique<Texture>();
    int32_t width, height, channels;

    if (void* data = stbi_load(path.c_str(), &width, &height, &channels, 4))
    {
      bool alpha = CheckAlpha(data, width, height);
      texture->m_HasAlpha = alpha;
      if (hasAlpha != nullptr)
        *hasAlpha = alpha;

      VkFormat format = linear ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
      texture->m_VulkanTexture.Load(*m_Ctx, data, width, height, 4, format, true, alpha);
      stbi_image_free(data);

      auto handle = Store(std::move(texture));
      m_Cache[key] = handle;
      return handle;
    }
    else
    {
      YA_LOG_ERROR("Assets", "Failed to load texture: %s", path.c_str());
      return {};
    }
  }

  TextureHandle TextureManager::LoadFromCpuData(CpuTextureData&& cpuData, bool* hasAlpha, const std::string& cachePath)
  {
    if (cpuData.mips.empty() || cpuData.width == 0)
      return {};

    if (!cachePath.empty())
    {
      CacheKey key { cachePath, cpuData.linear };
      auto it = m_Cache.find(key);
      if (it != m_Cache.end() && Has(it->second))
      {
        if (hasAlpha != nullptr)
          *hasAlpha = Get(it->second).m_HasAlpha;
        return it->second;
      }
    }

    auto texture = std::make_unique<Texture>();
    texture->m_HasAlpha = cpuData.hasAlpha;
    if (hasAlpha != nullptr)
      *hasAlpha = cpuData.hasAlpha;

    VkFormat format = cpuData.linear ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
    texture->m_VulkanTexture.LoadFromCpuData(*m_Ctx, cpuData, format);

    auto handle = Store(std::move(texture));

    if (!cachePath.empty())
    {
      CacheKey key { cachePath, cpuData.linear };
      m_Cache[key] = handle;
    }

    return handle;
  }

  CpuTextureData TextureManager::DecodeToCpu(const std::string& filePath, bool linear)
  {
    CpuTextureData result;
    int32_t width, height, channels;

    void* data = stbi_load(filePath.c_str(), &width, &height, &channels, 4);
    if (!data)
    {
      YA_LOG_ERROR("Assets", "DecodeToCpu: failed to load texture: %s", filePath.c_str());
      return result;
    }

    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);
    result.pixelSize = 4;
    result.linear = linear;
    result.repeat = true;

    result.hasAlpha = CheckAlpha(data, result.width, result.height);

    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(result.width, result.height)))) + 1;

    if (result.hasAlpha && result.pixelSize == 4)
    {
      result.hasCpuMips = true;
      MipGenerator::GenerateWithAlphaCoverage(static_cast<uint8_t*>(data), result.width, result.height, mipLevels, result.mips);
    }
    else
    {
      result.hasCpuMips = false;
      result.mips.resize(1);
      result.mips[0].width = result.width;
      result.mips[0].height = result.height;
      size_t dataSize = static_cast<size_t>(result.width) * result.height * result.pixelSize;
      result.mips[0].data.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + dataSize);
    }

    stbi_image_free(data);
    return result;
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

    for (size_t i = 3; i < static_cast<size_t>(width) * height * 4; i += 4)
    {
      if (pixels[i] < 250)
        return true;
    }
    return false;
  }
}
