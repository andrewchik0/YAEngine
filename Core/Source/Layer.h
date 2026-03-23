#pragma once

namespace YAEngine
{
  class Application;

  class Layer
  {
  public:
    virtual ~Layer() = default;

    Application& App();

    // Called when the layer is added to the stack. EventBus is available.
    // Asset manager and scene are NOT yet initialized.
    virtual void OnAttach() {}

    // Called after asset manager and render are initialized.
    // Scene is ready — create entities and load assets here.
    virtual void OnSceneReady() {}

    // Called when the layer is removed or application shuts down.
    virtual void OnDetach() {}

    virtual void Update(double deltaTime) {}
    virtual void RenderUI() {}

  };
}
