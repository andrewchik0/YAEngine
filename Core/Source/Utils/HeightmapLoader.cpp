#include "HeightmapLoader.h"

#include "Utils/Log.h"
#include <Stb/stb_image.h>

namespace YAEngine
{
  bool HeightmapLoader::Load(const std::string& path, HeightmapData& out)
  {
    int w, h, channels;

    if (stbi_is_16_bit(path.c_str()))
    {
      uint16_t* data = stbi_load_16(path.c_str(), &w, &h, &channels, 1);
      if (!data)
      {
        YA_LOG_ERROR("Assets", "Failed to load 16-bit heightmap: %s", path.c_str());
        return false;
      }

      out.width = static_cast<uint32_t>(w);
      out.height = static_cast<uint32_t>(h);
      out.heights.resize(static_cast<size_t>(w) * h);

      constexpr float scale = 1.0f / 65535.0f;
      for (size_t i = 0; i < out.heights.size(); i++)
        out.heights[i] = static_cast<float>(data[i]) * scale;

      stbi_image_free(data);
    }
    else
    {
      uint8_t* data = stbi_load(path.c_str(), &w, &h, &channels, 1);
      if (!data)
      {
        YA_LOG_ERROR("Assets", "Failed to load heightmap: %s", path.c_str());
        return false;
      }

      out.width = static_cast<uint32_t>(w);
      out.height = static_cast<uint32_t>(h);
      out.heights.resize(static_cast<size_t>(w) * h);

      constexpr float scale = 1.0f / 255.0f;
      for (size_t i = 0; i < out.heights.size(); i++)
        out.heights[i] = static_cast<float>(data[i]) * scale;

      stbi_image_free(data);
    }

    YA_LOG_INFO("Assets", "Loaded heightmap %s (%ux%u)", path.c_str(), out.width, out.height);
    return true;
  }
}
