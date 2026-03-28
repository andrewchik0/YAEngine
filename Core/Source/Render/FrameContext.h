#pragma once

#include "Pch.h"
#include "RenderObject.h"

namespace YAEngine
{
  class AssetManager;

  struct CubicTextureResources;

  struct FrameContext
  {
    SceneSnapshot& snapshot;
    AssetManager& assets;
    CubicTextureResources& cubicResources;
    double time;
    uint32_t windowWidth;
    uint32_t windowHeight;
    void(*renderUI)(void* userData) = nullptr;
    void* renderUIData = nullptr;
  };
}
