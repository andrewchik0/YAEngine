#pragma once

namespace YAEngine
{
  struct RenderSpecs
  {
    bool validationLayers = false;
    bool debugUtils = false;
    uint32_t maxFramesInFlight = 2;
    std::string applicationName;
  };
}
