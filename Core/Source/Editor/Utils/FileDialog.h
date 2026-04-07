#pragma once

#include <nfd.h>

namespace YAEngine
{
  namespace FileDialog
  {
    void Init();
    void Shutdown();
    std::string OpenFile(const nfdu8filteritem_t* filters, uint32_t filterCount);
    std::string SaveFile(const nfdu8filteritem_t* filters, uint32_t filterCount, const char* defaultName = nullptr);
  }
}
