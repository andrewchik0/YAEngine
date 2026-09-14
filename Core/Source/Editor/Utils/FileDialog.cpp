#include "Editor/Utils/FileDialog.h"

#include <nfd.h>

#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    bool s_Suppressed = false;
    uint32_t s_SuppressedCount = 0;

    bool RefuseWhileSuppressed(const char* kind, const nfdu8filteritem_t* filters, uint32_t filterCount)
    {
      if (!s_Suppressed)
        return false;

      s_SuppressedCount++;
      YA_LOG_WARN("Editor", "%s file dialog (%s) suppressed: an agent UI request is running",
        kind, filterCount > 0 ? filters[0].name : "any file");
      return true;
    }
  }

  void FileDialog::Init()
  {
    NFD_Init();
  }

  void FileDialog::Shutdown()
  {
    NFD_Quit();
  }

  std::string FileDialog::OpenFile(const nfdu8filteritem_t* filters, uint32_t filterCount)
  {
    if (RefuseWhileSuppressed("Open", filters, filterCount))
      return {};

    nfdu8char_t* outPath = nullptr;
    nfdopendialogu8args_t args = {};
    args.filterList = filters;
    args.filterCount = filterCount;

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

    if (result == NFD_OKAY)
    {
      std::string path(outPath);
      NFD_FreePathU8(outPath);
      return path;
    }

    return {};
  }

  std::string FileDialog::SaveFile(const nfdu8filteritem_t* filters, uint32_t filterCount, const char* defaultName)
  {
    if (RefuseWhileSuppressed("Save", filters, filterCount))
      return {};

    nfdu8char_t* outPath = nullptr;
    nfdsavedialogu8args_t args = {};
    args.filterList = filters;
    args.filterCount = filterCount;
    args.defaultName = defaultName;

    nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);

    if (result == NFD_OKAY)
    {
      std::string path(outPath);
      NFD_FreePathU8(outPath);
      return path;
    }

    return {};
  }

  void FileDialog::SetSuppressed(bool suppressed)
  {
    s_Suppressed = suppressed;
    if (suppressed)
      s_SuppressedCount = 0;
  }

  uint32_t FileDialog::GetSuppressedCount()
  {
    return s_SuppressedCount;
  }
}
