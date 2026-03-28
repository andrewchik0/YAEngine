#pragma once

#include "Pch.h"

namespace YAEngine
{
  class Scene;
  class AssetManager;

  struct CubicTextureResources;

  struct FrameContext
  {
    Scene& scene;
    AssetManager& assets;
    CubicTextureResources& cubicResources;
    double time;
    uint32_t windowWidth;
    uint32_t windowHeight;
    std::function<void()> renderUI;
  };
}
