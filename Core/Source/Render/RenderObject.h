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
    glm::vec3 boundsMin { std::numeric_limits<float>::max() };
    glm::vec3 boundsMax { std::numeric_limits<float>::lowest() };
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

  struct DirectionalShadowData
  {
    glm::vec3 direction { 0.0f, -1.0f, 0.0f };
    glm::vec3 position { 0.0f };
    bool castShadow = false;
  };

  struct SceneSnapshot
  {
    std::vector<RenderObject> objects;
    uint32_t visibleCount = 0;
    CameraData camera;
    CubeMapHandle skybox;
    DirectionalShadowData directionalShadow;
  };
}
