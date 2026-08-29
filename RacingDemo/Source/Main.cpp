#include "Engine.h"
#include "AppLayer.h"

int main(int argc, char** argv)
{
  YAEngine::EngineSpecs specs;

#ifndef NDEBUG
  specs.debugUtils = true;
#endif
  // specs.validationLayers = true;

  for (int i = 1; i < argc; i++)
  {
    if (std::string_view(argv[i]) == "--no-dlss")
      specs.enableDLSS = false;
  }

  YAEngine::Engine engine(specs);
  engine.PushLayer<AppLayer>();
  engine.Run();
  engine.Destroy();

  return 0;
}
