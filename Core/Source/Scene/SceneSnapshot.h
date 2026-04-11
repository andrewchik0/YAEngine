#pragma once

#include "Render/RenderObject.h"
#include "LightData.h"
#include "ShadowData.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Assets/MeshManager.h"
#include "Assets/MaterialManager.h"

namespace YAEngine
{
  inline void BuildSceneSnapshot(SceneSnapshot& snapshot, LightBuffer& lights, Scene& scene, MeshManager& meshManager, MaterialManager& materialManager)
  {
    snapshot.objects.clear();
    snapshot.spotShadowRequests.clear();
    snapshot.pointShadowRequests.clear();
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

      // Auto-add LocalBounds from mesh data if missing
      if (!reg.all_of<LocalBounds>(entity) && meshManager.Has(mesh.asset))
      {
        reg.emplace<LocalBounds>(entity, LocalBounds {
          .min = meshManager.GetMinBB(mesh.asset),
          .max = meshManager.GetMaxBB(mesh.asset)
        });
        reg.emplace_or_replace<BoundsDirty>(entity);
      }

      auto& mat = materialManager.Get(material.asset);

      bool hasTerrain = reg.all_of<TerrainMaterialComponent>(entity);

      RenderObject obj {
        .mesh = mesh.asset,
        .material = material.asset,
        .worldTransform = wt.world,
        .instanceData = meshManager.GetInstanceData(mesh.asset),
        .instanceOffset = meshManager.GetInstanceOffset(mesh.asset),
        .doubleSided = mat.doubleSided,
        .noShading = (mat.shadingModel == ShadingModel::Unlit),
        .isTerrain = hasTerrain,
      };

      if (hasTerrain)
      {
        snapshot.terrainData.layer0 = material.asset;
        snapshot.terrainData.layer1 = &reg.get<TerrainMaterialComponent>(entity);
      }

      if (reg.all_of<WorldBounds>(entity))
      {
        auto& wb = reg.get<WorldBounds>(entity);
        obj.boundsMin = wb.min;
        obj.boundsMax = wb.max;
      }

      snapshot.objects.push_back(obj);
    });

    snapshot.visibleCount = uint32_t(snapshot.objects.size());

    // Extract light probes
    snapshot.probeBuffer.probeCount = 0;
    auto probeView = scene.GetView<LightProbeComponent, WorldTransform>();
    for (auto entity : probeView)
    {
      if (snapshot.probeBuffer.probeCount >= MAX_LIGHT_PROBES) break;

      auto& probe = probeView.get<LightProbeComponent>(entity);
      if (!probe.baked || probe.atlasSlot == 0) continue;

      auto& wt = probeView.get<WorldTransform>(entity);
      glm::vec3 position = glm::vec3(wt.world[3]);

      auto& info = snapshot.probeBuffer.probes[snapshot.probeBuffer.probeCount];
      info.positionShape = glm::vec4(position, probe.shape == ProbeShape::Box ? 1.0f : 0.0f);
      info.extentsFade = glm::vec4(probe.extents, probe.fadeDistance);
      info.arrayIndex = probe.atlasSlot;
      info.priority = probe.priority;
      info._pad0 = 0;
      info._pad1 = 0;
      snapshot.probeBuffer.probeCount++;
    }

    // Extract lights
    lights.pointLightCount = 0;
    lights.spotLightCount = 0;
    lights.directional = {};
    bool hasDirectional = false;
    auto lightView = scene.GetView<LightComponent, WorldTransform>();
    for (auto entity : lightView)
    {
      auto& light = lightView.get<LightComponent>(entity);
      auto& wt = lightView.get<WorldTransform>(entity);

      glm::vec3 position = glm::vec3(wt.world[3]);
      glm::vec3 forward = glm::normalize(-glm::vec3(wt.world[2]));

      switch (light.type)
      {
        case LightType::Point:
        {
          if (lights.pointLightCount >= MAX_POINT_LIGHTS) break;
          uint32_t lightIdx = uint32_t(lights.pointLightCount);
          auto& pl = lights.pointLights[lights.pointLightCount++];
          pl.positionRadius = glm::vec4(position, light.radius);
          pl.colorIntensity = glm::vec4(light.color, light.intensity);
          pl.shadowPad = glm::vec4(glm::intBitsToFloat(-1), 0.0f, 0.0f, 0.0f);

          if (light.castShadow && snapshot.pointShadowRequests.size() < MAX_SHADOW_POINTS)
          {
            pl.shadowPad.x = glm::intBitsToFloat(int(snapshot.pointShadowRequests.size()));
            snapshot.pointShadowRequests.push_back({
              .position = position,
              .radius = light.radius,
              .lightIndex = lightIdx,
            });
          }
          break;
        }
        case LightType::Spot:
        {
          if (lights.spotLightCount >= MAX_SPOT_LIGHTS) break;
          uint32_t lightIdx = uint32_t(lights.spotLightCount);
          auto& sl = lights.spotLights[lights.spotLightCount++];
          sl.positionRadius = glm::vec4(position, light.radius);
          sl.directionInnerCone = glm::vec4(forward, std::cos(light.innerCone));
          sl.colorOuterCone = glm::vec4(light.color, std::cos(light.outerCone));
          sl.intensityShadow = glm::vec4(light.intensity, glm::intBitsToFloat(-1), 0.0f, 0.0f);

          if (light.castShadow && snapshot.spotShadowRequests.size() < MAX_SHADOW_SPOTS)
          {
            sl.intensityShadow.y = glm::intBitsToFloat(int(snapshot.spotShadowRequests.size()));
            snapshot.spotShadowRequests.push_back({
              .position = position,
              .direction = forward,
              .outerCone = light.outerCone,
              .radius = light.radius,
              .lightIndex = lightIdx,
            });
          }
          break;
        }
        case LightType::Directional:
        {
          if (!hasDirectional)
          {
            lights.directional.directionIntensity = glm::vec4(forward, light.intensity);
            lights.directional.colorPad = glm::vec4(light.color, 0.0f);
            snapshot.directionalShadow.direction = forward;
            snapshot.directionalShadow.position = position;
            snapshot.directionalShadow.castShadow = light.castShadow;
            hasDirectional = true;
          }
          break;
        }
      }
    }
  }
}
