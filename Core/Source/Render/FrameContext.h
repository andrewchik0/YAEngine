#pragma once

#include "Pch.h"
#include "RenderObject.h"
#include "LightData.h"

namespace YAEngine
{
  class AssetManager;

  struct CubicTextureResources;

  struct FrameContext
  {
    SceneSnapshot& snapshot;
    LightBuffer& lights;
    AssetManager& assets;
    CubicTextureResources& cubicResources;
    double time;
    uint32_t windowWidth;
    uint32_t windowHeight;
    void(*renderUI)(void* userData) = nullptr;
    void* renderUIData = nullptr;
  };
}
