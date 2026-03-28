#pragma once

#include "Render/RenderObject.h"
#include "Scene/Scene.h"
#include "Assets/MeshManager.h"

namespace YAEngine
{
  inline SceneSnapshot BuildSceneSnapshot(Scene& scene, MeshManager& meshManager)
  {
    SceneSnapshot snapshot;

    snapshot.skybox = scene.GetSkybox();

    // Extract camera
    auto activeCamera = scene.GetActiveCamera();
    if (activeCamera != entt::null && scene.HasComponent<CameraComponent>(activeCamera))
    {
      auto& transform = scene.GetComponent<LocalTransform>(activeCamera);
      auto& camera = scene.GetComponent<CameraComponent>(activeCamera);
      snapshot.camera.position = transform.position;
      snapshot.camera.rotation = transform.rotation;
      snapshot.camera.fov = camera.fov;
      snapshot.camera.aspectRatio = camera.aspectRatio;
      snapshot.camera.nearPlane = camera.nearPlane;
      snapshot.camera.farPlane = camera.farPlane;
    }

    // Extract render objects
    auto view = scene.GetView<MeshComponent, WorldTransform, MaterialComponent>();
    auto& reg = scene.GetRegistry();

    view.each([&](entt::entity entity, MeshComponent& mesh, WorldTransform& wt, MaterialComponent& material)
    {
      if (reg.all_of<HiddenTag>(entity)) return;

      snapshot.objects.push_back({
        .mesh = mesh.asset,
        .material = material.asset,
        .worldTransform = wt.world,
        .instanceData = meshManager.GetInstanceData(mesh.asset),
        .instanceOffset = meshManager.GetInstanceOffset(mesh.asset),
        .doubleSided = reg.all_of<DoubleSidedTag>(entity),
        .noShading = reg.all_of<NoShadingTag>(entity),
      });
    });

    return snapshot;
  }
}
