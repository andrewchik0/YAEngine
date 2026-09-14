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

    // While suppressed, dialogs return the cancelled result without opening. A bridge UI request
    // suppresses them: a native dialog blocks the main thread, and the request with it, until
    // someone closes it by hand. Main thread only.
    void SetSuppressed(bool suppressed);
    // Dialogs refused since suppression was last turned on.
    uint32_t GetSuppressedCount();
  }
}
