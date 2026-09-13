#include "Engine.h"
#include "AppLayer.h"
#include "Render/FrameCaptureLayer.h"
#include "Utils/FrameCaptureSpec.h"

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

  // Parsed before the window opens so a malformed capture flag costs nothing but a message.
  if (!YAEngine::ParseFrameCaptureSpec(argc, argv, specs.captureSpec))
    return 1;

  bool capture = specs.captureSpec.armed || specs.captureSpec.listTargets;

  YAEngine::Engine engine(specs);
  engine.PushLayer<AppLayer>();
  if (capture)
    engine.PushLayer<YAEngine::FrameCaptureLayer>();
  engine.Run();
  engine.Destroy();

  return engine.GetExitCode();
}
