#pragma once

namespace YAEngine
{
  struct HeightmapData
  {
    std::vector<float> heights;
    uint32_t width = 0;
    uint32_t height = 0;
  };

  class HeightmapLoader
  {
  public:
    static bool Load(const std::string& path, HeightmapData& out);
  };
}
