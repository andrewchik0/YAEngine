#pragma once

#include "Pch.h"
#include "Assets/Handle.h"

namespace YAEngine
{
  struct RenderObject
  {
    MeshHandle mesh;
    MaterialHandle material;
    glm::mat4 worldTransform;
    std::vector<glm::mat4>* instanceData = nullptr;
    uint32_t instanceOffset = 0;
    bool doubleSided = false;
    bool noShading = false;
  };

  struct CameraData
  {
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1, 0, 0, 0 };
    float fov = glm::radians(58.31f);
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.01f;
    float farPlane = 1000.0f;
  };

  struct SceneSnapshot
  {
    std::vector<RenderObject> objects;
    CameraData camera;
    CubeMapHandle skybox;
  };
}
