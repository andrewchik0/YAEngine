#pragma once

namespace YAEngine
{
  struct RenderSpecs
  {
    bool validationLayers = false;
    bool debugUtils = false;
    // Skipping Streamline entirely keeps NGX out of the process: the driver meters
    // presents for NGX-initialized apps, which caps benchmark/demo runs.
    bool enableDLSS = true;
    uint32_t maxFramesInFlight = 2;
    std::string applicationName;
  };
}
