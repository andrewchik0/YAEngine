#pragma once

#include "Layer.h"
#include "EventBus.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class DebugUILayer : public Layer
  {
  public:

    void OnSceneReady() override;
    void OnDetach() override;
    void RenderUI() override;

  private:

    void DrawEntity(Entity entity);
    void DrawTransform(TransformComponent& tc);
    void DrawInspector();

    SubscriptionId m_KeySub {};
    SubscriptionId m_MouseButtonSub {};
    SubscriptionId m_MouseScrollSub {};
  };
}
