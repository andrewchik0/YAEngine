#include "Engine.h"
#include "AppLayer.h"

int main()
{
  YAEngine::EngineSpecs specs;

#ifndef NDEBUG
  specs.debugUtils = true;
  // specs.validationLayers = true;
#endif

  YAEngine::Engine engine(specs);
  engine.PushLayer<AppLayer>();
  engine.Run();
  engine.Destroy();

  return 0;
}
