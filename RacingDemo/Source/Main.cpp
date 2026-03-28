#include "Engine.h"
#include "AppLayer.h"

int main()
{
  YAEngine::EngineSpecs specs;

  specs.isDebug = true;

  YAEngine::Engine engine(specs);
  engine.PushLayer<AppLayer>();
  engine.Run();
  engine.Destroy();

  return 0;
}
