#pragma once

#include "Pch.h"

namespace YAEngine
{
  // printf-style formatting into a string sized to the result; empty on an encoding error.
  inline void FormatText(std::string& out, const char* format, va_list args)
  {
    va_list sizing;
    va_copy(sizing, args);
    int32_t length = std::vsnprintf(nullptr, 0, format, sizing);
    va_end(sizing);

    out.assign(length > 0 ? size_t(length) : 0, '\0');
    if (length > 0)
      std::vsnprintf(out.data(), out.size() + 1, format, args);
  }
}
