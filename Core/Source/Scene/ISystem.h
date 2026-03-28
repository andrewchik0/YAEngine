#pragma once

#include <entt/entt.hpp>

namespace YAEngine
{
  enum class SystemPhase : uint8_t
  {
    PrePhysics,
    Physics,
    PostPhysics,
    TransformUpdate,
    PreRender
  };

  class ISystem
  {
  public:
    virtual ~ISystem() = default;

    virtual void Update(entt::registry& registry, double dt) = 0;
    virtual SystemPhase GetPhase() const = 0;
    virtual int GetPriority() const { return 0; }
  };
}
