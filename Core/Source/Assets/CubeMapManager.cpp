#include "CubeMapManager.h"

#include <filesystem>
#include <StbImage/stb_image.h>

#include "Render/RenderContext.h"
#include "Utils/Log.h"

namespace YAEngine
{
  CubeMapHandle CubeMapManager::Load(const std::string& path)
  {
    auto canonical = std::filesystem::weakly_canonical(path).string();

    auto it = m_Cache.find(canonical);
    if (it != m_Cache.end() && Has(it->second))
    {
      return it->second;
    }

    auto texture = std::make_unique<CubeMap>();
    int32_t width, height, channels;

    if (float* image32 = stbi_loadf(path.c_str(), &width, &height, &channels, 4))
    {
      texture->m_CubeTexture.Create(*m_Ctx, *m_CubicRes, image32, width, height);
      stbi_image_free(image32);

      auto handle = Store(std::move(texture));
      m_Cache[canonical] = handle;
      return handle;
    }
    else
    {
      YA_LOG_ERROR("Assets", "Failed to load cubemap: %s", path.c_str());
      throw std::runtime_error("Texture file does not exist!");
    }
  }

  void CubeMapManager::Destroy(CubeMapHandle handle)
  {
    Get(handle).m_CubeTexture.Destroy(*m_Ctx);
    Remove(handle);

    std::erase_if(m_Cache, [&](const auto& pair) {
      return pair.second == handle;
    });
  }

  void CubeMapManager::DestroyAll()
  {
    ForEach([this](CubeMap& texture) {
      texture.m_CubeTexture.Destroy(*m_Ctx);
    });
    Clear();
    m_Cache.clear();
  }
}
