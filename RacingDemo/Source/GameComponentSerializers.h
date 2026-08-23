#pragma once

namespace YAEngine
{
  class ComponentRegistry;
}

// Game components live in the demo, not in Core, so the demo registers their
// serializers itself. Must run before the scene is loaded.
void RegisterGameComponentSerializers(YAEngine::ComponentRegistry& registry);
