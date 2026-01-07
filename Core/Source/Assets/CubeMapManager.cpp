#include "CubeMapManager.h"

#include <StbImage/stb_image.h>

#include "glm/gtc/packing.hpp"

namespace YAEngine
{
  CubeMapHandle CubeMapManager::Load(const std::string& path)
  {
    auto texture = std::make_unique<CubeMap>();
    int32_t width, height, channels;

    auto filePath = path;

    if (float* image32 = stbi_loadf(filePath.c_str(), &width, &height, &channels, 4))
    {
      // std::vector<uint16_t> data16(width * height * 4);

      // for(int i = 0; i < width * height * 4; ++i)
      // {
        // data16[i] = glm::packHalf1x16(image32[i]);
      // }
      texture->m_CubeTexture.Create(image32, width, height);
      stbi_image_free(image32);

      return AssetManagerBase::Load(std::move(texture));
    }
    else
    {
      throw std::runtime_error("Texture file does not exist!");
    }
  }

  void CubeMapManager::Destroy(CubeMapHandle handle)
  {
    Get(handle).m_CubeTexture.DestroyCubicTextures();
    Remove(handle);
  }

  void CubeMapManager::DestroyAll()
  {
    for (auto& texture : GetAll())
    {
      texture.second.get()->m_CubeTexture.Destroy();
    }
    GetAll().clear();
  }
}
