#pragma once

#include "Pch.h"
#include "Utils/Log.h"

struct stbtt_fontinfo;

namespace YAEngine
{
  struct GlyphBitmap
  {
    std::unique_ptr<uint8_t[]> pixels;
    int width = 0;
    int height = 0;
  };

  class GlyphRasterizer
  {
  public:

    GlyphRasterizer();
    ~GlyphRasterizer();
    GlyphRasterizer(GlyphRasterizer&&) noexcept;
    GlyphRasterizer& operator=(GlyphRasterizer&&) noexcept;

    bool Init(const std::string& fontPath);
    void Destroy();

    GlyphBitmap RasterizeGlyph(uint32_t codepoint, float pixelHeight);

  private:

    std::vector<uint8_t> m_FontData;
    std::unique_ptr<stbtt_fontinfo> m_FontInfo;
    bool b_Initialized = false;
  };
}
