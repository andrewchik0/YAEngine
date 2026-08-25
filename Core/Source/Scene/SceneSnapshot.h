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
  // FNV-1a-flavoured fold of shadow-relevant scene state, computed while the
  // snapshot loops already run. Floats are hashed bit-wise: any actual change
  // flips the digest, bit-identical state keeps it. The fold consumes 64 bits
  // per step instead of one byte, because the per-caster loop below runs on
  // every frame including cache HITs. Local to the snapshot on purpose - this
  // is not a general hashing utility yet, and the values are only ever
  // compared against the previous frame, never stored.
  struct SnapshotDigest
  {
    uint64_t value = 14695981039346656037ull;

    void AddWord(uint64_t word)
    {
      // Avalanche the input first: a packed pair of handles has only a handful
      // of live bits, and without this they would only ever stir the low end
      // of the digest.
      word *= 0xff51afd7ed558ccdull;
      word ^= word >> 33;
      value = (value ^ word) * 1099511628211ull;
      value ^= value >> 29;
    }

    void AddPair(uint32_t low, uint32_t high)
    {
      AddWord(uint64_t(low) | (uint64_t(high) << 32));
    }

    template<typename T>
    void Add(const T& data)
    {
      const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
      size_t remaining = sizeof(T);
      while (remaining >= sizeof(uint64_t))
      {
        uint64_t word;
        std::memcpy(&word, bytes, sizeof(uint64_t));
        AddWord(word);
        bytes += sizeof(uint64_t);
        remaining -= sizeof(uint64_t);
      }
      if (remaining > 0)
      {
        uint64_t word = 0;
        std::memcpy(&word, bytes, remaining);
        // The tail length rides in the top byte, which a short tail always
        // leaves zero, so a 4-byte field cannot fold like an 8-byte one.
        AddWord(word | (uint64_t(remaining) << 56));
      }
    }
  };

#ifdef YA_EDITOR
  // One-shot request from PerformancePanel: the next BuildSceneSnapshot logs
  // every shadow-relevant caster whose lastChangeTick is recent - the entities
  // keeping casterTransformDigest changing. One bool check per frame while
  // disarmed.
  inline bool g_ShadowCacheBlockerDumpPending = false;
#endif

  inline void BuildSceneSnapshot(SceneSnapshot& snapshot, LightBuffer& lights, Scene& scene, MeshManager& meshManager, MaterialManager& materialManager)
  {
    snapshot.objects.clear();
    snapshot.spotShadowRequests.clear();
    snapshot.pointShadowRequests.clear();
    snapshot.shadowMoverBounds.clear();
    snapshot.shadowMoverUnbounded = false;
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

    // Tick of the TransformSystem update right before this snapshot. Objects
    // stamped with it moved THIS tick; older stamps were already handled by
    // the rebuild their digest change forced.
    uint64_t currentTransformTick = 0;
    if (auto* tickCtx = reg.ctx().find<TransformTickContext>())
      currentTransformTick = tickCtx->tick;

    SnapshotDigest identityDigest;
    SnapshotDigest transformDigest;

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
#ifdef YA_EDITOR
        .entityId = static_cast<uint32_t>(entity),
#endif
      };

      if (hasTerrain)
      {
        snapshot.terrainData.layer0 = material.asset;
        snapshot.terrainData.layer1 = &reg.get<TerrainMaterialComponent>(entity);
      }

      const WorldBounds* wb = reg.try_get<WorldBounds>(entity);
      if (wb)
      {
        obj.boundsMin = wb->min;
        obj.boundsMax = wb->max;
      }

      // Shadow-relevant casters only, matching what RenderShadowMaps skips.
      // Identity catches set/order/mesh/material/flag/instancing changes; the
      // transform digest catches any recomputed world matrix via its stamp.
      if (!obj.noShading && !obj.isTransparent)
      {
        // Two 32-bit inputs per fold step: the field positions inside a word
        // are fixed and every caster contributes the same word sequence, so
        // packing costs nothing in change detection and saves most of the
        // work this loop does on a HIT frame.
        //
        // INVARIANT for anyone writing instance matrices: what goes in below
        // is the instance COUNT, never the matrices. A writer that mutates a
        // live instanceData vector in place while keeping its length (wind, an
        // LOD reshuffle, a respawn pool) leaves the shadow atlas - and the
        // instance SSBO, whose upload sits behind the cache early-return -
        // holding the old matrices forever. Regeneration must recreate the
        // mesh asset, which moves the mesh handle folded below.
        uint32_t instanceCount = obj.instanceData ? uint32_t(obj.instanceData->size()) : 0u;
        uint32_t shadowFlags = (obj.doubleSided ? 1u : 0u) | (obj.isAlphaTest ? 2u : 0u);

        identityDigest.AddPair(uint32_t(entity), obj.mesh.index);
        identityDigest.AddPair(obj.mesh.generation, obj.material.index);
        identityDigest.AddPair(obj.material.generation, obj.instanceOffset);
        identityDigest.AddPair(instanceCount, shadowFlags);

        // The shadow pass reads exactly one thing out of a material: the alpha
        // of baseColorTexture in alphatest_discard.frag, and only for an
        // alpha-test caster (Render.Draw.cpp binds the material descriptor
        // under `if (dc.isAlphaTest)`). The handle pair folded above is only
        // the SlotMap slot key, which does not move when the inspector assigns
        // a different texture to the same material, so the texture handle has
        // to go in separately. Nothing else from the material reaches a shadow
        // shader - folding roughness, albedo or an emissive map here would
        // rebuild the atlas for edits it cannot see.
        if (obj.isAlphaTest)
          identityDigest.AddPair(mat.baseColorTexture.index, mat.baseColorTexture.generation);

        transformDigest.AddWord(wt.lastChangeTick);

        // Stage 6 dirty rects: attribute the digest change to the objects
        // that moved this tick, as the union of previous and current bounds.
        if (currentTransformTick != 0 && wt.lastChangeTick == currentTransformTick)
        {
          if (wb)
          {
            snapshot.shadowMoverBounds.push_back({
              .min = glm::min(wb->min, wb->prevMin),
              .max = glm::max(wb->max, wb->prevMax),
            });
          }
          else
          {
            snapshot.shadowMoverUnbounded = true;
          }
        }
      }

      snapshot.objects.push_back(obj);
    });

    snapshot.casterIdentityDigest = identityDigest.value;
    snapshot.casterTransformDigest = transformDigest.value;

#ifdef YA_EDITOR
    if (g_ShadowCacheBlockerDumpPending)
    {
      g_ShadowCacheBlockerDumpPending = false;

      uint64_t maxTick = 0;
      auto tickView = reg.view<WorldTransform>();
      for (auto e : tickView)
        maxTick = std::max(maxTick, tickView.get<WorldTransform>(e).lastChangeTick);

      uint32_t blockers = 0;
      view.each([&](entt::entity entity, MeshComponent& mesh, WorldTransform& wt, MaterialComponent& material)
      {
        if (reg.all_of<HiddenTag>(entity)) return;
        auto& mat = materialManager.Get(material.asset);
        if (mat.shadingModel == ShadingModel::Unlit || mat.transparent) return;
        if (wt.lastChangeTick + 2 < maxTick) return;
        blockers++;
        YA_LOG_INFO("Render", "Shadow cache blocker: '%s' (entity %u) tick %llu, max %llu",
          scene.GetName(entity).c_str(), uint32_t(entity),
          (unsigned long long)wt.lastChangeTick, (unsigned long long)maxTick);
      });
      if (blockers == 0)
        YA_LOG_INFO("Render", "Shadow cache blockers: none (no caster stamped within 2 ticks of max %llu)",
          (unsigned long long)maxTick);
      else
        YA_LOG_INFO("Render", "Shadow cache blockers: %u caster(s) stamped within 2 ticks of max %llu",
          blockers, (unsigned long long)maxTick);
    }
#endif

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

    // Any change to a shadow-casting light forces a full redraw, which is what
    // keeps the ordinal tile-to-light binding safe under all-or-nothing caching.
    // The sun DIRECTION is deliberately excluded: the cascade fit hysteresis
    // owns it, and hashing it here would defeat the angular threshold.
    SnapshotDigest lightDigest;
    lightDigest.Add(snapshot.directionalShadow.castShadow);
    lightDigest.Add(snapshot.directionalShadow.shadowDistance);
    lightDigest.Add(uint32_t(snapshot.spotShadowRequests.size()));
    for (const auto& req : snapshot.spotShadowRequests)
    {
      lightDigest.Add(req.position);
      lightDigest.Add(req.direction);
      lightDigest.Add(req.outerCone);
      lightDigest.Add(req.radius);
      lightDigest.Add(req.lightIndex);
    }
    lightDigest.Add(uint32_t(snapshot.pointShadowRequests.size()));
    for (const auto& req : snapshot.pointShadowRequests)
    {
      lightDigest.Add(req.position);
      lightDigest.Add(req.radius);
      lightDigest.Add(req.lightIndex);
    }
    snapshot.lightDigest = lightDigest.value;
  }
}
