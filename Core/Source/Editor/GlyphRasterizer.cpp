#define STB_TRUETYPE_IMPLEMENTATION
#include "Stb/stb_truetype.h"

#include "Editor/GlyphRasterizer.h"

namespace YAEngine
{
  GlyphRasterizer::GlyphRasterizer() = default;
  GlyphRasterizer::~GlyphRasterizer() = default;
  GlyphRasterizer::GlyphRasterizer(GlyphRasterizer&&) noexcept = default;
  GlyphRasterizer& GlyphRasterizer::operator=(GlyphRasterizer&&) noexcept = default;

  bool GlyphRasterizer::Init(const std::string& fontPath)
  {
    FILE* f = fopen(fontPath.c_str(), "rb");
    if (!f)
    {
      YA_LOG_ERROR("Editor", "GlyphRasterizer: failed to open font file: %s", fontPath.c_str());
      return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    m_FontData.resize(size);
    fread(m_FontData.data(), 1, size, f);
    fclose(f);

    m_FontInfo = std::make_unique<stbtt_fontinfo>();
    if (!stbtt_InitFont(m_FontInfo.get(), m_FontData.data(), 0))
    {
      YA_LOG_ERROR("Editor", "GlyphRasterizer: failed to parse font: %s", fontPath.c_str());
      m_FontInfo.reset();
      m_FontData.clear();
      return false;
    }

    b_Initialized = true;
    YA_LOG_INFO("Editor", "GlyphRasterizer: loaded font %s", fontPath.c_str());
    return true;
  }

  void GlyphRasterizer::Destroy()
  {
    m_FontInfo.reset();
    m_FontData.clear();
    b_Initialized = false;
  }

  GlyphBitmap GlyphRasterizer::RasterizeGlyph(uint32_t codepoint, float pixelHeight)
  {
    GlyphBitmap result;

    if (!b_Initialized)
    {
      YA_LOG_ERROR("Editor", "GlyphRasterizer: not initialized");
      return result;
    }

    float scale = stbtt_ScaleForPixelHeight(m_FontInfo.get(), pixelHeight);

    int glyphIndex = stbtt_FindGlyphIndex(m_FontInfo.get(), int(codepoint));
    if (glyphIndex == 0)
    {
      YA_LOG_WARN("Editor", "GlyphRasterizer: glyph not found for codepoint 0x%X", codepoint);
      return result;
    }

    int w, h, xoff, yoff;
    uint8_t* bitmap = stbtt_GetGlyphBitmap(m_FontInfo.get(), scale, scale, glyphIndex, &w, &h, &xoff, &yoff);

    if (!bitmap || w <= 0 || h <= 0)
    {
      YA_LOG_WARN("Editor", "GlyphRasterizer: empty bitmap for codepoint 0x%X", codepoint);
      if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);
      return result;
    }

    result.width = w;
    result.height = h;
    result.pixels = std::make_unique<uint8_t[]>(w * h);
    memcpy(result.pixels.get(), bitmap, w * h);

    stbtt_FreeBitmap(bitmap, nullptr);
    return result;
  }
}
