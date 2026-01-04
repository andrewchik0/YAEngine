#include "Layer.h"

#include "Application.h"

namespace YAEngine
{
  Application& Layer::App()
  {
    return Application::Get();
  }

}
