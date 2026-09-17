#pragma once

#include "Layer.h"
#include "Scene/Scene.h"

// Game build camera and shot controls: 1-9 pick a camera, Tab / Shift+Tab step through them,
// F9 plays the selected camera's shot, F5 puts the car on a hardcoded spot and looks at it
// through a still camera. The car follow camera comes first, then the scene cameras by name. A
// camera with a shot waits on its first frame, the car posed there too, so a take starts from
// exactly what is on screen and comes back to it when it ends.
class CameraDirectorLayer : public YAEngine::Layer
{
public:
  void Update(double deltaTime) override;

private:
  std::vector<YAEngine::Entity> CollectCameras();
  void Select(const std::vector<YAEngine::Entity>& cameras, size_t index);
  void ShowSelected();
  void TogglePlayback();
  void TeleportToSpot();
  bool HasShot(YAEngine::Entity camera);

  YAEngine::Entity m_Selected = entt::null;
  YAEngine::Entity m_SpotCamera = entt::null;
  bool b_Playing = false;
};
