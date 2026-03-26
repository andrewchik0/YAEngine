#pragma once

#include "Scene/Scene.h"

namespace YAEngine
{
  class AssetManager;
  class Render;

  struct EditorContext
  {
    Entity selectedEntity = entt::null;
    Scene* scene = nullptr;
    AssetManager* assetManager = nullptr;
    Render* render = nullptr;
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
  };
}
