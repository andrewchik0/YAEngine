#pragma once
#include "MaterialManager.h"
#include "MeshManager.h"
#include "ModelManager.h"
#include "TextureManager.h"

namespace YAEngine
{
  class AssetManager
  {
  public:

    AssetManager() = default;

    void Init();

    MeshManager& Meshes()
    {
      return m_MeshManager;
    }

    TextureManager& Textures()
    {
      return m_TextureManager;
    }

    MaterialManager& Materials()
    {
      return m_MaterialManager;
    }

    ModelManager& Models()
    {
      return m_ModelManager;
    }

    void DestroyAll();

  private:

    MeshManager m_MeshManager;
    TextureManager m_TextureManager;
    MaterialManager m_MaterialManager;
    ModelManager m_ModelManager {};
  };
}
