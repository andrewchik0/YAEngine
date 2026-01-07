#pragma once
#include "AssetManagerBase.h"
#include "Render/VulkanCubicTexture.h"

namespace YAEngine
{
  struct CubeMap
  {
  private:
    VulkanCubicTexture m_CubeTexture;

    friend class Render;
    friend class CubeMapManager;
    friend class VulkanMaterial;
  };

  using CubeMapHandle = AssetHandle;

  class CubeMapManager : public AssetManagerBase<CubeMap>
  {
  public:
    [[nodiscard]]
    CubeMapHandle Load(const std::string& filePath);
    void Destroy(CubeMapHandle handle);

    void DestroyAll();

  private:
  };
}
