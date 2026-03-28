#pragma once

#include "Scene/Scene.h"
#include "Assets/Handle.h"

namespace YAEngine
{
  class AssetManager;
  class Render;
  class Timer;
  class EditorTextureCache;

  struct EditorContext
  {
    Entity selectedEntity = entt::null;
    MaterialHandle selectedMaterial;
    Scene* scene = nullptr;
    AssetManager* assetManager = nullptr;
    Render* render = nullptr;
    Timer* timer = nullptr;
    EditorTextureCache* textureCache = nullptr;
    bool selectionChangedFlag = false;
    bool viewportHovered = false;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;

    void SelectEntity(Entity e)
    {
      selectedEntity = e;
      selectionChangedFlag = true;
    }

    void ClearSelection()
    {
      selectedEntity = entt::null;
      selectionChangedFlag = true;
    }

    bool ConsumeSelectionChanged()
    {
      bool changed = selectionChangedFlag;
      selectionChangedFlag = false;
      return changed;
    }

    void SelectMaterial(MaterialHandle h)
    {
      selectedMaterial = h;
    }

    void ClearMaterialSelection()
    {
      selectedMaterial = MaterialHandle::Invalid();
    }
  };
}
