#pragma once

#include "Render/RenderObject.h"
#include "LightData.h"
#include "ShadowData.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Assets/MeshManager.h"
#include "Assets/MaterialManager.h"
#include "Utils/SplinePath3D.h"
#include "Utils/Log.h"
#include "TerrainMaterialUniforms.h"

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
      auto& local = scene.GetComponent<LocalTransform>(activeCamera);
      auto& camera = scene.GetComponent<CameraComponent>(activeCamera);
      snapshot.camera.position = local.position;
      snapshot.camera.rotation = local.rotation;
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
        .isAlphaTest = mat.alphaTest,
        .isTransparent = mat.transparent,
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

    // Sample road polyline for terrain shoulder mask (XZ only)
    snapshot.terrainData.roadPolyline.clear();
    auto roadView = reg.view<RoadComponent>();
    for (auto roadEntity : roadView)
    {
      auto& road = reg.get<RoadComponent>(roadEntity);
      if (road.points.size() < 2) continue;

      SplinePath3D spline;
      spline.points = road.points;

      uint32_t segCount = MAX_ROAD_SEGMENTS;
      snapshot.terrainData.roadPolyline.reserve(segCount);
      for (uint32_t i = 0; i < segCount; i++)
      {
        float t = float(i) / float(segCount - 1);
        glm::vec3 p = spline.Evaluate(t);
        snapshot.terrainData.roadPolyline.emplace_back(p.x, p.z);
      }
      break;
    }

    // Extract reflection probes
    snapshot.probeBuffer.probeCount = 0;
    uint32_t skippedProbes = 0;
    auto probeView = scene.GetView<ReflectionProbeComponent, WorldTransform>();
    for (auto entity : probeView)
    {
      auto& probe = probeView.get<ReflectionProbeComponent>(entity);
      if (!probe.baked || probe.atlasSlot == 0) continue;

      if (snapshot.probeBuffer.probeCount >= MAX_REFLECTION_PROBES)
      {
        skippedProbes++;
        continue;
      }

      auto& wt = probeView.get<WorldTransform>(entity);
      glm::vec3 position = glm::vec3(wt.world[3]);

      // Orientation comes from the entity transform, with the basis normalized so
      // transform scale cannot leak in. Volume size stays ReflectionProbeComponent::extents.
      glm::mat3 basis(glm::vec3(wt.world[0]), glm::vec3(wt.world[1]), glm::vec3(wt.world[2]));
      for (int axis = 0; axis < 3; axis++)
      {
        float len = glm::length(basis[axis]);
        if (len > 1e-6f)
          basis[axis] /= len;
        else
          basis[axis] = glm::vec3(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f);
      }
      glm::quat rotation = glm::normalize(glm::quat_cast(basis));

      auto& info = snapshot.probeBuffer.probes[snapshot.probeBuffer.probeCount];
      info.positionShape = glm::vec4(position, probe.shape == ProbeShape::Box ? 1.0f : 0.0f);
      info.extentsFade = glm::vec4(probe.extents, probe.fadeDistance);
      info.orientation = glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w);
      info.arrayIndex = probe.atlasSlot;
      info.priority = probe.priority;
      info.parallaxCorrection = probe.parallaxCorrection ? 1 : 0;
      info._pad0 = 0;
      snapshot.probeBuffer.probeCount++;
    }

    // Latched: this runs every frame, so it logs only when the skipped count changes.
    // The limit stays where it is: 16 probes is generous for a specular-only role.
    static uint32_t s_LastSkippedProbes = 0;
    if (skippedProbes != s_LastSkippedProbes)
    {
      s_LastSkippedProbes = skippedProbes;
      if (skippedProbes > 0)
      {
        YA_LOG_WARN("Render", "Scene has %u reflection probes, only %d fit (MAX_REFLECTION_PROBES); %u skipped",
          uint32_t(snapshot.probeBuffer.probeCount) + skippedProbes, MAX_REFLECTION_PROBES, skippedProbes);
      }
    }

    // Extract irradiance volumes. Position and orientation come from the entity
    // transform with the basis normalized, exactly like reflection probes above -
    // halfExtents alone define the box, transform scale is ignored.
#ifdef YA_EDITOR
    snapshot.irradianceVolumes.clear();
    auto volumeView = scene.GetView<IrradianceVolumeComponent, WorldTransform>();
    for (auto entity : volumeView)
    {
      auto& volume = volumeView.get<IrradianceVolumeComponent>(entity);
      auto& wt = volumeView.get<WorldTransform>(entity);

      glm::vec3 center = glm::vec3(wt.world[3]);
      glm::quat rotation = ExtractIrradianceBoxRotation(wt.world);

      snapshot.irradianceVolumes.push_back(IrradianceVolumeInstance {
        .center = center,
        .rotation = rotation,
        .grid = ComputeIrradianceGridLayout(center, rotation, volume.halfExtents, volume.spacing),
      });
    }
#endif

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
            snapshot.directionalShadow.shadowDistance = light.shadowDistance;
            snapshot.directionalShadow.castShadow = light.castShadow;
            hasDirectional = true;
          }
          break;
        }
      }
    }
  }
}
