#pragma once

#include "Pch.h"

namespace YAEngine
{
  // Byte offset of the first ASCII case-insensitive occurrence of pattern in text; 0 for an empty
  // pattern, npos when there is none.
  inline size_t FindCaseInsensitive(std::string_view text, std::string_view pattern)
  {
    auto it = std::search(text.begin(), text.end(), pattern.begin(), pattern.end(),
      [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); });
    return it == text.end() && !pattern.empty() ? std::string_view::npos : size_t(it - text.begin());
  }

  // An empty pattern is contained in every text.
  inline bool ContainsCaseInsensitive(std::string_view text, std::string_view pattern)
  {
    return FindCaseInsensitive(text, pattern) != std::string_view::npos;
  }
}
