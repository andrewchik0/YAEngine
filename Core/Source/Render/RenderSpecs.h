#pragma once

namespace YAEngine
{
  struct RenderSpecs
  {
    bool validationLayers = false;
    uint32_t maxFramesInFlight = 2;
    std::string applicationName;
  };
}
