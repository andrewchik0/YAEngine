#include "AssetManager.h"

#include "Application.h"

namespace YAEngine
{
  void AssetManager::Init()
  {
    m_ModelManager = ModelManager(&Application::Get().GetScene(), this);
  }

  void AssetManager::SetRenderContext(const RenderContext& ctx, const VulkanTexture& noneTexture, CubicTextureResources& cubicResources)
  {
    m_TextureManager.SetRenderContext(ctx);
    m_MeshManager.SetRenderContext(ctx);
    m_MaterialManager.SetRenderContext(ctx, noneTexture);
    m_CubeMapManager.SetRenderContext(ctx, cubicResources);
  }

  void AssetManager::DestroyAll()
  {
    m_MeshManager.DestroyAll();
    m_TextureManager.DestroyAll();
    m_MaterialManager.DestroyAll();
    m_CubeMapManager.DestroyAll();
    m_ModelManager.DestroyAll();
  }
}
