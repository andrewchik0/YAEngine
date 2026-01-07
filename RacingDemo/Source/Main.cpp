#include "AppLayer.h"

int main()
{
  YAEngine::ApplicationSpecs specs;

  specs.isDebug = true;

  YAEngine::Application::Init(specs);
  YAEngine::Application::Get().PushLayer<AppLayer>();
  YAEngine::Application::Get().Run();
  YAEngine::Application::Get().Destroy();

  return 0;
}
