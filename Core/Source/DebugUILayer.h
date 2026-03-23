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
    void DrawTransform(TransformComponent& tc);
    void DrawInspector();

    static constexpr int FRAMETIME_HISTORY_SIZE = 200;
    float m_FrametimeHistory[FRAMETIME_HISTORY_SIZE] = {};
    int m_FrametimeOffset = 0;
  };
}
