#include "CubeMapManager.h"

#include <StbImage/stb_image.h>

#include "Render/RenderContext.h"

namespace YAEngine
{
  CubeMapHandle CubeMapManager::Load(const std::string& path)
  {
    auto texture = std::make_unique<CubeMap>();
    int32_t width, height, channels;

    auto filePath = path;

    if (float* image32 = stbi_loadf(filePath.c_str(), &width, &height, &channels, 4))
    {
      texture->m_CubeTexture.Create(*m_Ctx, *m_CubicRes, image32, width, height);
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
    Get(handle).m_CubeTexture.Destroy(*m_Ctx);
    Remove(handle);
  }

  void CubeMapManager::DestroyAll()
  {
    for (auto& texture : GetAll())
    {
      texture.second.get()->m_CubeTexture.Destroy(*m_Ctx);
    }
    GetAll().clear();
  }
}
