#include "TextureManager.h"

#include "DdsFile.h"
#include "Render/RenderContext.h"
#include "Utils/Log.h"
#include "Utils/MipGenerator.h"

#define STB_IMAGE_IMPLEMENTATION
#include <Stb/stb_image.h>

namespace YAEngine
{
  static CpuTextureData BuildCpuTextureData(const uint8_t* rgba, uint32_t width, uint32_t height, bool linear)
  {
    CpuTextureData result;
    result.width = width;
    result.height = height;
    result.pixelSize = 4;
    result.linear = linear;
    result.repeat = true;

    result.hasAlpha = TextureManager::CheckAlpha(rgba, width, height);

    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    if (result.hasAlpha)
    {
      result.hasCpuMips = true;
      MipGenerator::GenerateWithAlphaCoverage(rgba, width, height, mipLevels, result.mips);
    }
    else
    {
      result.hasCpuMips = false;
      result.mips.resize(1);
      result.mips[0].width = width;
      result.mips[0].height = height;
      size_t dataSize = static_cast<size_t>(width) * height * result.pixelSize;
      result.mips[0].data.assign(rgba, rgba + dataSize);
    }

    return result;
  }

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

    // Load() sniffs the signature itself, so this is one open instead of the two
    // an IsDdsFile() probe followed by a Load() would cost - and every non-DDS
    // texture in the scene used to pay for the probe as well.
    if (DdsFile::HasDdsExtension(path))
    {
      auto cpuData = DdsFile::Load(path, linear);
      if (cpuData.width == 0)
        return {};

      return LoadFromCpuData(std::move(cpuData), hasAlpha, canonical);
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

  TextureHandle TextureManager::LoadEmbedded(const EmbeddedTexture& texture, const std::string& cacheKey,
    bool* hasAlpha, bool linear)
  {
    CacheKey key { cacheKey, linear };

    auto it = m_Cache.find(key);
    if (it != m_Cache.end() && Has(it->second))
    {
      if (hasAlpha != nullptr)
        *hasAlpha = Get(it->second).m_HasAlpha;
      return it->second;
    }

    auto cpuData = DecodeEmbeddedToCpu(texture, linear);
    if (cpuData.width == 0)
    {
      YA_LOG_ERROR("Assets", "Failed to load embedded texture: %s", cacheKey.c_str());
      return {};
    }

    return LoadFromCpuData(std::move(cpuData), hasAlpha, cacheKey);
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

    VkFormat format = cpuData.format != VK_FORMAT_UNDEFINED
      ? cpuData.format
      : (cpuData.linear ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB);
    if (!texture->m_VulkanTexture.LoadFromCpuData(*m_Ctx, cpuData, format))
    {
      // Storing it anyway would hand out a handle whose image is VK_NULL_HANDLE,
      // and VulkanMaterial::Bind would write that into a combined image sampler.
      return {};
    }

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
    if (DdsFile::IsDdsFile(filePath))
      return DdsFile::Load(filePath, linear);

    int32_t width, height, channels;

    void* data = stbi_load(filePath.c_str(), &width, &height, &channels, 4);
    if (!data)
    {
      YA_LOG_ERROR("Assets", "DecodeToCpu: failed to load texture: %s", filePath.c_str());
      return {};
    }

    CpuTextureData result = BuildCpuTextureData(static_cast<const uint8_t*>(data),
      static_cast<uint32_t>(width), static_cast<uint32_t>(height), linear);

    stbi_image_free(data);
    return result;
  }

  CpuTextureData TextureManager::DecodeEmbeddedToCpu(const EmbeddedTexture& texture, bool linear)
  {
    if (texture.data.empty())
      return {};

    // Uncompressed payload is already RGBA8 - no decoding needed
    if (texture.width != 0 && texture.height != 0)
      return BuildCpuTextureData(texture.data.data(), texture.width, texture.height, linear);

    int32_t width, height, channels;

    void* data = stbi_load_from_memory(texture.data.data(), static_cast<int32_t>(texture.data.size()),
      &width, &height, &channels, 4);
    if (!data)
    {
      YA_LOG_ERROR("Assets", "DecodeEmbeddedToCpu: failed to decode embedded texture (%zu bytes)", texture.data.size());
      return {};
    }

    CpuTextureData result = BuildCpuTextureData(static_cast<const uint8_t*>(data),
      static_cast<uint32_t>(width), static_cast<uint32_t>(height), linear);

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

  bool TextureManager::CheckAlpha(const void* data, uint32_t width, uint32_t height)
  {
    const uint8_t* pixels = (const uint8_t *)data;

    for (size_t i = 3; i < static_cast<size_t>(width) * height * 4; i += 4)
    {
      if (pixels[i] < 250)
        return true;
    }
    return false;
  }
}
