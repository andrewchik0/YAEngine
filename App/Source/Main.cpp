#include "Application.h"
#include "FreeCamLayer.h"

class AppLayer : public YAEngine::Layer
{
public:
  AppLayer() = default;

  std::vector<YAEngine::Vertex> vertices = {
    {
      { -5, -1.0, -5 },
      { 0, 0 },
      { 0, 1, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
    },
    {
      { -5, -1.0, 5 },
      { 0, 5 },
      { 0, 1, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
    },
    {
      { 5, -1.0, -5 },
      { 5, 0 },
      { 0, 1, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
    },
    {
      { 5, -1.0, 5 },
      { 5, 5 },
      { 0, 1, 0 },
      { 0, 0, 0 },
      { 0, 0, 0 },
    }
  };

  std::vector<uint32_t> indices = {
    0, 1, 2, 1, 3, 2
  };

  YAEngine::Entity monkey;

  void OnBeforeInit() override
  {
    App().PushLayer<YAEngine::FreeCamLayer>();
  }

  void Init() override
  {
    auto monkeyHandle = App().GetAssetManager().Models().Load(APP_WORKING_DIR "/Assets/Models/monkey.obj");
    monkey = App().GetAssetManager().Models().Get(monkeyHandle).rootEntity;

    auto textureHandle = App().GetAssetManager().Textures().Load(APP_WORKING_DIR "/Assets/Textures/Checkerboard.png");
    auto meshHandle = App().GetAssetManager().Meshes().Load(vertices, indices);
    auto materialHandle = App().GetAssetManager().Materials().Create();

    App().GetAssetManager().Materials().Get(materialHandle).baseColorTexture = textureHandle;

    auto entity = App().GetScene().CreateEntity("Plane");
    App().GetScene().AddComponent<YAEngine::MeshComponent>(entity, meshHandle);
    App().GetScene().AddComponent<YAEngine::MaterialComponent>(entity, materialHandle);
  }

  void Update(double deltaTime) override
  {
    App().GetScene().GetTransform(monkey).rotation = glm::angleAxis(1.0f * (float)deltaTime, glm::vec3(0,1,0)) * App().GetScene().GetTransform(monkey).rotation;
    App().GetScene().MarkDirty(monkey);
  }

};

int main()
{
  YAEngine::ApplicationSpecs specs;

  specs.isDebug = true;

  YAEngine::Application::Init(specs);
  YAEngine::Application::Get().PushLayer<AppLayer>();
  YAEngine::Application::Get().Run();

  return 0;
}
