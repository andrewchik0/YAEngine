#pragma once

#include "ServiceRegistry.h"

namespace YAEngine
{
  class Scene;
  class Render;
  class AssetManager;
  class Timer;
  class InputSystem;
  class Window;
  class LayerManager;

  class Layer
  {
  public:
    virtual ~Layer() = default;

    void SetRegistry(ServiceRegistry& registry) { m_Registry = &registry; }

    Scene& GetScene();
    Render& GetRender();
    AssetManager& GetAssets();
    Timer& GetTimer();
    InputSystem& GetInput();
    Window& GetWindow();
    LayerManager& GetLayerManager();

    virtual void OnAttach() {}
    virtual void OnSceneReady() {}
    virtual void OnDetach() {}
    virtual void FixedUpdate(double fixedDt) {}
    virtual void Update(double deltaTime) {}
    virtual void LateUpdate(double deltaTime) {}
    virtual void RenderUI() {}

  protected:
    ServiceRegistry* m_Registry = nullptr;
  };
}
