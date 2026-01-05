#pragma once

#include "Layer.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class DebugUILayer : public Layer
  {
  public:

    void RenderUI() override;

  private:

    void DrawEntity(Entity entity);
  };
}
