#pragma once

namespace YAEngine
{
  class Application;

  class Layer
  {
  public:
    virtual ~Layer() = default;

    Application& App();

    virtual void OnBeforeInit() {}
    virtual void Init() {}
    virtual void Destroy() {}
    virtual void Update(double deltaTime) {}
    virtual void RenderUI() {}

  };
}
