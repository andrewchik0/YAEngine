#include "Layer.h"

#include "Window.h"
#include "LayerManager.h"
#include "Input/InputSystem.h"
#include "Assets/AssetManager.h"
#include "Render/Render.h"
#include "Scene/Scene.h"
#include "Utils/Timer.h"

namespace YAEngine
{
  Scene& Layer::GetScene() { return m_Registry->Get<Scene>(); }
  Render& Layer::GetRender() { return m_Registry->Get<Render>(); }
  AssetManager& Layer::GetAssets() { return m_Registry->Get<AssetManager>(); }
  Timer& Layer::GetTimer() { return m_Registry->Get<Timer>(); }
  InputSystem& Layer::GetInput() { return m_Registry->Get<InputSystem>(); }
  Window& Layer::GetWindow() { return m_Registry->Get<Window>(); }
  LayerManager& Layer::GetLayerManager() { return m_Registry->Get<LayerManager>(); }
}
