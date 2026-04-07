#include "Editor/Utils/FileDialog.h"

#include <nfd.h>

namespace YAEngine
{
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
}
