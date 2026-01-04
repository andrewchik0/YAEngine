#include "AssetManager.h"

#include "Application.h"

namespace YAEngine
{
  void AssetManager::Init()
  {
    m_ModelManager = ModelManager(&Application::Get().GetScene(), this);
  }

  void AssetManager::DestroyAll()
  {
    m_MeshManager.DestroyAll();
    m_TextureManager.DestroyAll();
    m_MaterialManager.DestroyAll();
  }
}

