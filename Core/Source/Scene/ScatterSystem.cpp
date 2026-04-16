#include "ScatterSystem.h"

#include "Components.h"
#include "Scene.h"
#include "TerrainSystem.h"
#include "Assets/AssetManager.h"
#include "Assets/ModelImporter.h"
#include "Render/Render.h"
#include "Utils/TerrainMeshGenerator.h"
#include "Utils/PrimitiveMeshFactory.h"
#include "Utils/SplinePath2D.h"
#include "Utils/SplinePath3D.h"
#include "Utils/CatmullRomCurve.h"
#include "Utils/ThreadPool.h"
#include "Utils/Log.h"

namespace YAEngine
{
  static uint32_t HashScatterSeed(int32_t seed)
  {
    uint32_t h = static_cast<uint32_t>(seed);
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= h >> 16;
    return h;
  }

  static float ScatterRandom(uint32_t& state)
  {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00FFFFFF) / static_cast<float>(0x00FFFFFF);
  }

  static float DistanceToRoadXZ(float wx, float wz, const SplinePath3D& spline, int segments = 64)
  {
    float minDistSq = std::numeric_limits<float>::max();
    glm::vec2 point(wx, wz);

    glm::vec3 prev3d = spline.Evaluate(0.0f);
    glm::vec2 prev(prev3d.x, prev3d.z);

    for (int i = 1; i <= segments; i++)
    {
      float t = static_cast<float>(i) / static_cast<float>(segments);
      glm::vec3 curr3d = spline.Evaluate(t);
      glm::vec2 curr(curr3d.x, curr3d.z);

      glm::vec2 edge = curr - prev;
      float edgeLenSq = glm::dot(edge, edge);
      float proj = 0.0f;
      if (edgeLenSq > 0.0f)
        proj = glm::clamp(glm::dot(point - prev, edge) / edgeLenSq, 0.0f, 1.0f);

      glm::vec2 closest = prev + proj * edge;
      float distSq = glm::dot(point - closest, point - closest);
      if (distSq < minDistSq)
        minDistSq = distSq;

      prev = curr;
    }

    return std::sqrt(minDistSq);
  }

  static const NodeDescription* FindMeshNode(const NodeDescription& node,
                                              glm::mat4 parentTransform,
                                              glm::mat4& outAccumulatedTransform)
  {
    glm::mat4 local = glm::translate(glm::mat4(1.0f), node.position)
                    * glm::mat4_cast(node.rotation)
                    * glm::scale(glm::mat4(1.0f), node.scale);
    glm::mat4 world = parentTransform * local;

    if (node.meshIndex.has_value())
    {
      outAccumulatedTransform = world;
      return &node;
    }
    for (auto& child : node.children)
    {
      auto* found = FindMeshNode(child, world, outAccumulatedTransform);
      if (found) return found;
    }
    return nullptr;
  }

  static float SampleHeightGrid(const TerrainHeightGrid& grid, float worldX, float worldZ)
  {
    if (grid.heights.empty()) return 0.0f;
    float hs = grid.size * 0.5f;
    float normX = glm::clamp((worldX + hs) / grid.size, 0.0f, 1.0f);
    float normZ = glm::clamp((worldZ + hs) / grid.size, 0.0f, 1.0f);
    float px = normX * static_cast<float>(grid.vertsPerSide - 1);
    float pz = normZ * static_cast<float>(grid.vertsPerSide - 1);
    uint32_t x0 = static_cast<uint32_t>(px);
    uint32_t z0 = static_cast<uint32_t>(pz);
    uint32_t x1 = std::min(x0 + 1, grid.vertsPerSide - 1);
    uint32_t z1 = std::min(z0 + 1, grid.vertsPerSide - 1);
    float fx = px - static_cast<float>(x0);
    float fz = pz - static_cast<float>(z0);
    float h00 = grid.heights[z0 * grid.vertsPerSide + x0];
    float h10 = grid.heights[z0 * grid.vertsPerSide + x1];
    float h01 = grid.heights[z1 * grid.vertsPerSide + x0];
    float h11 = grid.heights[z1 * grid.vertsPerSide + x1];
    float h0n = h00 + (h10 - h00) * fx;
    float h1n = h01 + (h11 - h01) * fx;
    return h0n + (h1n - h0n) * fz;
  }

  // --- CPU-only placement functions (thread-safe) ---

  struct PlacementInput
  {
    ScatterComponent scatter;
    TerrainComponent terrain;
    const TerrainHeightGrid* heightGrid;
    uint32_t terrainEntityId;
    glm::mat4 nodeTransform;
    bool hasRoadMask;
    SplinePath3D roadSpline;
    float roadHalfWidth;
  };

  static std::vector<glm::mat4> ComputeInstanceMatrices(const PlacementInput& input)
  {
    auto& scatter = input.scatter;
    auto& terrain = input.terrain;
    float halfSize = terrain.size * 0.5f;
    float placementRadius = scatter.radius > 0.0f ? scatter.radius : halfSize;

    SplinePath2D spline;
    CatmullRomCurve curve;
    const SplinePath2D* splinePtr = nullptr;
    const CatmullRomCurve* curvePtr = nullptr;
    if (!terrain.maskPath.empty())
    {
      spline = SplinePath2D(terrain.maskPath);
      splinePtr = &spline;
      if (!terrain.maskCurve.empty())
      {
        curve = CatmullRomCurve(terrain.maskCurve);
        curvePtr = &curve;
      }
    }

    bool useCachedHeight = input.heightGrid != nullptr;
    float eps = terrain.size / static_cast<float>(terrain.subdivisions);

    uint32_t rngState = HashScatterSeed(scatter.seed);
    for (int i = 0; i < 4; i++)
      ScatterRandom(rngState);

    std::vector<glm::mat4> matrices;
    matrices.reserve(scatter.count);

    for (uint32_t i = 0; i < scatter.count; i++)
    {
      float rx = ScatterRandom(rngState) * 2.0f - 1.0f;
      float rz = ScatterRandom(rngState) * 2.0f - 1.0f;

      float wx = glm::clamp(rx * placementRadius, -halfSize, halfSize);
      float wz = glm::clamp(rz * placementRadius, -halfSize, halfSize);

      if (input.hasRoadMask)
      {
        float dist = DistanceToRoadXZ(wx, wz, input.roadSpline, 128);
        float innerRadius = input.roadHalfWidth + scatter.roadMaskPadding;

        if (dist < innerRadius)
          continue;
        if (dist > scatter.roadMaskOuterRadius)
          continue;

        if (scatter.roadMaskFalloff > 0.0f)
        {
          float falloffStart = scatter.roadMaskOuterRadius - scatter.roadMaskFalloff;
          if (dist > falloffStart)
          {
            float falloffT = (dist - falloffStart) / scatter.roadMaskFalloff;
            if (ScatterRandom(rngState) < falloffT)
              continue;
          }
        }
      }

      float wy;
      if (useCachedHeight)
        wy = SampleHeightGrid(*input.heightGrid, wx, wz);
      else
        wy = TerrainMeshGenerator::SampleSurfaceHeight(wx, wz, terrain, splinePtr, curvePtr);

      float hL, hR, hD, hU;
      if (useCachedHeight)
      {
        hL = SampleHeightGrid(*input.heightGrid, wx - eps, wz);
        hR = SampleHeightGrid(*input.heightGrid, wx + eps, wz);
        hD = SampleHeightGrid(*input.heightGrid, wx, wz - eps);
        hU = SampleHeightGrid(*input.heightGrid, wx, wz + eps);
      }
      else
      {
        hL = TerrainMeshGenerator::SampleSurfaceHeight(wx - eps, wz, terrain, splinePtr, curvePtr);
        hR = TerrainMeshGenerator::SampleSurfaceHeight(wx + eps, wz, terrain, splinePtr, curvePtr);
        hD = TerrainMeshGenerator::SampleSurfaceHeight(wx, wz - eps, terrain, splinePtr, curvePtr);
        hU = TerrainMeshGenerator::SampleSurfaceHeight(wx, wz + eps, terrain, splinePtr, curvePtr);
      }

      glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * eps, hD - hU));
      if (normal.y < scatter.maxSlope)
        continue;

      float angle = 0.0f;
      if (scatter.randomYRotation)
        angle = ScatterRandom(rngState) * glm::two_pi<float>();

      float scale = scatter.minScale + ScatterRandom(rngState) * (scatter.maxScale - scatter.minScale);

      glm::mat4 m(1.0f);
      m = glm::translate(m, glm::vec3(wx, wy, wz));
      m = glm::rotate(m, angle, glm::vec3(0.0f, 1.0f, 0.0f));
      m = glm::scale(m, glm::vec3(scale));
      m = m * input.nodeTransform;

      matrices.push_back(m);
    }

    return matrices;
  }

  static std::vector<glm::mat4> ComputeSatelliteMatrices(
    const ScatterComponent& scatter,
    const TerrainComponent& terrain,
    const TerrainHeightGrid* heightGrid,
    const glm::mat4& nodeTransform,
    const std::vector<glm::mat4>& sourcePositions)
  {
    float halfSize = terrain.size * 0.5f;
    float eps = terrain.size / static_cast<float>(terrain.subdivisions);

    SplinePath2D spline;
    CatmullRomCurve curve;
    const SplinePath2D* splinePtr = nullptr;
    const CatmullRomCurve* curvePtr = nullptr;
    if (!terrain.maskPath.empty())
    {
      spline = SplinePath2D(terrain.maskPath);
      splinePtr = &spline;
      if (!terrain.maskCurve.empty())
      {
        curve = CatmullRomCurve(terrain.maskCurve);
        curvePtr = &curve;
      }
    }

    bool useCachedHeight = heightGrid != nullptr;

    auto sampleHeight = [&](float sx, float sz) -> float {
      if (useCachedHeight)
        return SampleHeightGrid(*heightGrid, sx, sz);
      return TerrainMeshGenerator::SampleSurfaceHeight(sx, sz, terrain, splinePtr, curvePtr);
    };

    uint32_t rngState = HashScatterSeed(scatter.seed);
    for (int i = 0; i < 4; i++)
      ScatterRandom(rngState);

    std::vector<glm::mat4> matrices;

    for (auto& srcMatrix : sourcePositions)
    {
      glm::vec3 srcPos(srcMatrix[3]);

      uint32_t clusterCount = scatter.clusterCountMin +
        static_cast<uint32_t>(ScatterRandom(rngState) *
          (scatter.clusterCountMax - scatter.clusterCountMin + 1));
      clusterCount = std::min(clusterCount, scatter.clusterCountMax);

      for (uint32_t j = 0; j < clusterCount; j++)
      {
        float angle = ScatterRandom(rngState) * glm::two_pi<float>();
        float dist = ScatterRandom(rngState) * scatter.clusterRadius;
        float wx = glm::clamp(srcPos.x + std::cos(angle) * dist, -halfSize, halfSize);
        float wz = glm::clamp(srcPos.z + std::sin(angle) * dist, -halfSize, halfSize);

        float wy = sampleHeight(wx, wz);

        float hL = sampleHeight(wx - eps, wz);
        float hR = sampleHeight(wx + eps, wz);
        float hD = sampleHeight(wx, wz - eps);
        float hU = sampleHeight(wx, wz + eps);

        glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * eps, hD - hU));
        if (normal.y < scatter.maxSlope)
          continue;

        float yRot = 0.0f;
        if (scatter.randomYRotation)
          yRot = ScatterRandom(rngState) * glm::two_pi<float>();

        float scale = scatter.minScale + ScatterRandom(rngState) * (scatter.maxScale - scatter.minScale);

        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(wx, wy, wz));
        m = glm::rotate(m, yRot, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::scale(m, glm::vec3(scale));
        m = m * nodeTransform;

        matrices.push_back(m);
      }
    }

    return matrices;
  }

  // --- Shared helpers for mesh/material prep ---

  struct ScatterMeshSetup
  {
    MeshHandle mesh;
    MaterialHandle material;
    glm::mat4 nodeTransform{1.0f};
    bool valid = true;
  };

  static ScatterMeshSetup SetupModelMesh(
    const ScatterComponent& scatter, uint32_t entityId,
    AssetManager& assets, const ModelDescription& desc,
    const char* namePrefix = "scatter_")
  {
    ScatterMeshSetup setup;

    const NodeDescription* meshNode = nullptr;
    glm::mat4 accumulatedTransform(1.0f);
    for (auto& child : desc.root.children)
    {
      meshNode = FindMeshNode(child, glm::mat4(1.0f), accumulatedTransform);
      if (meshNode) break;
    }

    if (!meshNode || !meshNode->meshIndex.has_value())
    {
      YA_LOG_ERROR("Scene", "Scatter model has no mesh: %s", scatter.modelPath.c_str());
      setup.valid = false;
      return setup;
    }

    auto& meshDesc = desc.meshes[*meshNode->meshIndex];
    setup.mesh = assets.Meshes().Load(meshDesc.vertices, meshDesc.indices);

    glm::vec3 rawMin = assets.Meshes().GetMinBB(setup.mesh);
    glm::vec3 rawMax = assets.Meshes().GetMaxBB(setup.mesh);
    glm::vec3 corners[8] = {
      { rawMin.x, rawMin.y, rawMin.z }, { rawMax.x, rawMin.y, rawMin.z },
      { rawMin.x, rawMax.y, rawMin.z }, { rawMax.x, rawMax.y, rawMin.z },
      { rawMin.x, rawMin.y, rawMax.z }, { rawMax.x, rawMin.y, rawMax.z },
      { rawMin.x, rawMax.y, rawMax.z }, { rawMax.x, rawMax.y, rawMax.z },
    };
    glm::vec3 worldMin(std::numeric_limits<float>::max());
    glm::vec3 worldMax(std::numeric_limits<float>::lowest());
    for (auto& c : corners)
    {
      glm::vec3 w = glm::vec3(accumulatedTransform * glm::vec4(c, 1.0f));
      worldMin = glm::min(worldMin, w);
      worldMax = glm::max(worldMax, w);
    }
    glm::vec3 worldCenter = (worldMin + worldMax) * 0.5f;

    setup.nodeTransform = glm::translate(glm::mat4(1.0f),
                            glm::vec3(-worldCenter.x, -worldMin.y, -worldCenter.z))
                        * accumulatedTransform;

    setup.material = assets.Materials().Create();
    auto& mat = assets.Materials().Get(setup.material);
    mat.name = std::string(namePrefix) + std::to_string(entityId);

    if (meshNode->materialIndex.has_value())
    {
      auto& matDesc = desc.materials[*meshNode->materialIndex];
      mat.albedo = matDesc.albedo;
      mat.roughness = matDesc.roughness;
      mat.metallic = matDesc.metallic;
      mat.roughnessFactor = matDesc.roughnessFactor;
      mat.metallicFactor = matDesc.metallicFactor;
      mat.doubleSided = matDesc.doubleSided;
      mat.combinedTextures = matDesc.combinedTextures;

      if (!matDesc.baseColorTexture.empty())
      {
        mat.baseColorTexture = assets.Textures().Load(matDesc.baseColorTexture, &mat.hasAlpha);
        mat.alphaTest = mat.hasAlpha;
      }
      if (!matDesc.metallicTexture.empty())
        mat.metallicTexture = assets.Textures().Load(matDesc.metallicTexture, nullptr, true);
      if (!matDesc.roughnessTexture.empty())
        mat.roughnessTexture = assets.Textures().Load(matDesc.roughnessTexture, nullptr, true);
      if (!matDesc.normalTexture.empty())
        mat.normalTexture = assets.Textures().Load(matDesc.normalTexture, nullptr, true);
    }

    return setup;
  }

  static ScatterMeshSetup SetupPlaneMesh(
    const ScatterComponent& scatter, uint32_t entityId,
    AssetManager& assets,
    const char* namePrefix = "scatter_")
  {
    ScatterMeshSetup setup;

    float hw = scatter.planeWidth * 0.5f;
    float hh = scatter.planeHeight * 0.5f;
    ProcMesh quad;
    quad.vertices = {
      { .position = { -hw, 0.0f, 0.0f }, .tex = { 0.0f, 1.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .tangent = { 1.0f, 0.0f, 0.0f, 1.0f } },
      { .position = {  hw, 0.0f, 0.0f }, .tex = { 1.0f, 1.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .tangent = { 1.0f, 0.0f, 0.0f, 1.0f } },
      { .position = {  hw, hh * 2.0f, 0.0f }, .tex = { 1.0f, 0.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .tangent = { 1.0f, 0.0f, 0.0f, 1.0f } },
      { .position = { -hw, hh * 2.0f, 0.0f }, .tex = { 0.0f, 0.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .tangent = { 1.0f, 0.0f, 0.0f, 1.0f } },
      { .position = { 0.0f, 0.0f, -hw }, .tex = { 0.0f, 1.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } },
      { .position = { 0.0f, 0.0f,  hw }, .tex = { 1.0f, 1.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } },
      { .position = { 0.0f, hh * 2.0f,  hw }, .tex = { 1.0f, 0.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } },
      { .position = { 0.0f, hh * 2.0f, -hw }, .tex = { 0.0f, 0.0f }, .normal = { 0.0f, 1.0f, 0.0f }, .tangent = { 0.0f, 0.0f, 1.0f, 1.0f } },
    };
    quad.indices = { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
    setup.mesh = assets.Meshes().Load(quad.vertices, quad.indices);

    setup.material = assets.Materials().Create();
    auto& mat = assets.Materials().Get(setup.material);
    mat.name = std::string(namePrefix) + std::to_string(entityId);
    mat.doubleSided = true;
    if (!scatter.materialPath.empty())
    {
      std::string absPath = assets.ResolvePath(scatter.materialPath);
      mat.baseColorTexture = assets.Textures().Load(absPath, &mat.hasAlpha);
      mat.alphaTest = mat.hasAlpha;
    }

    return setup;
  }

  // Builds per-instance AABB colliders from scatter instance matrices.
  // Planes (grass/billboards) are skipped because we don't want to collide with them.
  // meshMin/meshMax are the actual mesh-space AABB corners from the mesh asset; each
  // instance matrix already folds in the centering nodeTransform, so transforming the
  // 8 corners gives a tight world-space AABB that matches the rendered model.
  static void BuildInstancedColliders(entt::registry& registry, entt::entity childEntity,
                                      const ScatterComponent& scatter,
                                      const std::vector<glm::mat4>& matrices,
                                      const glm::vec3& meshMin, const glm::vec3& meshMax)
  {
    if (scatter.meshType == ScatterMeshType::Plane)
      return;

    InstancedColliderComponent colliders;
    colliders.instances.reserve(matrices.size());

    const glm::vec3 corners[8] = {
      { meshMin.x, meshMin.y, meshMin.z }, { meshMax.x, meshMin.y, meshMin.z },
      { meshMin.x, meshMax.y, meshMin.z }, { meshMax.x, meshMax.y, meshMin.z },
      { meshMin.x, meshMin.y, meshMax.z }, { meshMax.x, meshMin.y, meshMax.z },
      { meshMin.x, meshMax.y, meshMax.z }, { meshMax.x, meshMax.y, meshMax.z },
    };

    for (auto& m : matrices)
    {
      glm::vec3 worldMin(std::numeric_limits<float>::max());
      glm::vec3 worldMax(std::numeric_limits<float>::lowest());
      for (auto& c : corners)
      {
        glm::vec3 w = glm::vec3(m * glm::vec4(c, 1.0f));
        worldMin = glm::min(worldMin, w);
        worldMax = glm::max(worldMax, w);
      }
      glm::vec3 center = (worldMin + worldMax) * 0.5f;
      glm::vec3 halfExtents = (worldMax - worldMin) * 0.5f;
      colliders.instances.push_back(InstancedColliderComponent::Entry {
        .center = center,
        .halfExtents = halfExtents
      });
    }

    YA_LOG_INFO("Physics", "Scatter entity %u: %zu instanced colliders",
                static_cast<uint32_t>(childEntity), colliders.instances.size());

    registry.emplace_or_replace<InstancedColliderComponent>(childEntity, std::move(colliders));
  }

  // --- ScatterSystem implementation ---

  void ScatterSystem::OnSceneClear()
  {
    m_States.clear();
    m_PendingDestroys.clear();
    m_ModelCache.clear();
  }

  void ScatterSystem::Update(entt::registry& registry, double dt)
  {
    for (size_t i = 0; i < m_PendingDestroys.size();)
    {
      auto& pending = m_PendingDestroys[i];
      if (--pending.framesLeft == 0)
      {
        if (m_Assets.Meshes().Has(pending.mesh))
          m_Assets.Meshes().Destroy(pending.mesh);
        if (m_Assets.Materials().Has(pending.material))
          m_Assets.Materials().Destroy(pending.material);
        if (pending.instanceSize > 0 && pending.instanceOffset != UINT32_MAX)
          m_Render.FreeInstanceData(pending.instanceOffset, pending.instanceSize);
        m_PendingDestroys[i] = m_PendingDestroys.back();
        m_PendingDestroys.pop_back();
      }
      else
      {
        i++;
      }
    }

    // When terrain changes, mark scatter on same entity and children as dirty
    auto terrainView = registry.view<TerrainDirty>();
    for (auto entity : terrainView)
    {
      if (registry.all_of<ScatterComponent>(entity) && !registry.all_of<ScatterDirty>(entity))
        registry.emplace<ScatterDirty>(entity);

      if (registry.all_of<HierarchyComponent>(entity))
      {
        entt::entity child = registry.get<HierarchyComponent>(entity).firstChild;
        while (child != entt::null)
        {
          if (registry.all_of<ScatterComponent>(child) && !registry.all_of<ScatterDirty>(child))
            registry.emplace<ScatterDirty>(child);
          child = registry.get<HierarchyComponent>(child).nextSibling;
        }
      }
    }

    // When a primary scatter is dirty, mark its satellites dirty too
    {
      auto dirtyView = registry.view<ScatterComponent, ScatterDirty>();
      auto allScatter = registry.view<ScatterComponent>();
      for (auto dirtyEntity : dirtyView)
      {
        auto& dirtyName = registry.get<Name>(dirtyEntity);
        for (auto candidate : allScatter)
        {
          if (candidate == dirtyEntity) continue;
          auto& cs = registry.get<ScatterComponent>(candidate);
          if (cs.clusterSource == dirtyName && !registry.all_of<ScatterDirty>(candidate))
            registry.emplace<ScatterDirty>(candidate);
        }
      }
    }

    // Two-pass processing: standalone first, then satellites
    auto dirtyView = registry.view<ScatterComponent, ScatterDirty>();
    std::vector<entt::entity> standalone;
    std::vector<entt::entity> satellites;
    for (auto entity : dirtyView)
    {
      auto& s = registry.get<ScatterComponent>(entity);
      if (s.clusterSource.empty())
        standalone.push_back(entity);
      else
        satellites.push_back(entity);
    }

    if (m_ThreadPool && standalone.size() > 0)
    {
      // Parallel path for standalone scatter

      // Phase 1: Import all unique models in parallel, skipping any already cached
      std::unordered_map<std::string, std::future<ModelDescription>> modelFutures;
      for (auto entity : standalone)
      {
        auto& scatter = registry.get<ScatterComponent>(entity);
        if (scatter.meshType == ScatterMeshType::Model && !scatter.modelPath.empty())
        {
          std::string absPath = m_Assets.ResolvePath(scatter.modelPath);
          if (m_ModelCache.contains(absPath) || modelFutures.contains(absPath))
            continue;
          modelFutures.emplace(absPath, m_ThreadPool->Submit(
            [absPath]() { return ModelImporter::Import(absPath, false); }));
        }
      }

      // Wait for all model imports and merge into the persistent cache
      for (auto& [path, future] : modelFutures)
        m_ModelCache.emplace(path, future.get());

      // Phase 2: Build road mask (shared across all entities)
      SplinePath3D roadSpline;
      float roadHalfWidth = 0.0f;
      bool hasRoadMask = false;
      {
        auto roadView = registry.view<RoadComponent>();
        for (auto roadEntity : roadView)
        {
          auto& road = registry.get<RoadComponent>(roadEntity);
          if (road.points.size() >= 2)
          {
            roadSpline.points = road.points;
            roadHalfWidth = road.width * 0.5f;
            hasRoadMask = true;
            break;
          }
        }
      }

      // Phase 3: Setup meshes/materials (main thread), then submit placement in parallel
      struct StandaloneTask
      {
        entt::entity entity;
        uint32_t entityId;
        ScatterMeshSetup setup;
        std::future<std::vector<glm::mat4>> future;
      };

      std::vector<StandaloneTask> tasks;

      for (auto entity : standalone)
      {
        uint32_t entityId = static_cast<uint32_t>(entity);
        DestroyState(entityId);

        auto& scatter = registry.get<ScatterComponent>(entity);
        if (scatter.count == 0)
        {
          registry.remove<ScatterDirty>(entity);
          continue;
        }

        // Find terrain
        const TerrainComponent* terrain = nullptr;
        entt::entity terrainEntity = entt::null;
        if (registry.all_of<TerrainComponent>(entity))
        {
          terrain = &registry.get<TerrainComponent>(entity);
          terrainEntity = entity;
        }
        else if (registry.all_of<HierarchyComponent>(entity))
        {
          auto parent = registry.get<HierarchyComponent>(entity).parent;
          if (parent != entt::null && registry.all_of<TerrainComponent>(parent))
          {
            terrain = &registry.get<TerrainComponent>(parent);
            terrainEntity = parent;
          }
        }

        if (!terrain)
        {
          YA_LOG_ERROR("Scene", "ScatterComponent requires TerrainComponent on entity or parent");
          registry.remove<ScatterDirty>(entity);
          continue;
        }

        // Setup mesh/material on main thread
        ScatterMeshSetup meshSetup;
        if (scatter.meshType == ScatterMeshType::Model && !scatter.modelPath.empty())
        {
          std::string absPath = m_Assets.ResolvePath(scatter.modelPath);
          meshSetup = SetupModelMesh(scatter, entityId, m_Assets, m_ModelCache.at(absPath));
        }
        else
        {
          meshSetup = SetupPlaneMesh(scatter, entityId, m_Assets);
        }

        if (!meshSetup.valid)
        {
          registry.remove<ScatterDirty>(entity);
          continue;
        }

        uint32_t terrainEntityId = static_cast<uint32_t>(terrainEntity);

        PlacementInput input;
        input.scatter = scatter;
        input.terrain = *terrain;
        input.heightGrid = m_TerrainSystem.GetCachedHeightGrid(terrainEntityId);
        input.terrainEntityId = terrainEntityId;
        input.nodeTransform = meshSetup.nodeTransform;
        input.hasRoadMask = hasRoadMask && scatter.useRoadMask;
        if (input.hasRoadMask)
        {
          input.roadSpline = roadSpline;
          input.roadHalfWidth = roadHalfWidth;
        }

        StandaloneTask task;
        task.entity = entity;
        task.entityId = entityId;
        task.setup = meshSetup;
        task.future = m_ThreadPool->Submit(
          [input = std::move(input)]() { return ComputeInstanceMatrices(input); });
        tasks.push_back(std::move(task));
      }

      // Phase 4: Finalize all
      for (auto& task : tasks)
      {
        auto matrices = task.future.get();

        if (matrices.empty())
        {
          m_Assets.Meshes().Destroy(task.setup.mesh);
          m_Assets.Materials().Destroy(task.setup.material);
          registry.remove<ScatterDirty>(task.entity);
          continue;
        }

        auto& scatter = registry.get<ScatterComponent>(task.entity);

        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
        float meshHeight = scatter.meshType == ScatterMeshType::Plane
          ? scatter.planeHeight * scatter.maxScale
          : scatter.maxScale;
        float meshRadius = scatter.meshType == ScatterMeshType::Plane
          ? scatter.planeWidth * 0.5f * scatter.maxScale
          : scatter.maxScale;

        for (auto& m : matrices)
        {
          glm::vec3 pos(m[3]);
          boundsMin = glm::min(boundsMin, pos - glm::vec3(meshRadius, 0.0f, meshRadius));
          boundsMax = glm::max(boundsMax, pos + glm::vec3(meshRadius, meshHeight, meshRadius));
        }

        ScatterState state;
        state.mesh = task.setup.mesh;
        state.material = task.setup.material;
        state.instanceMatrices = std::move(matrices);

        uint32_t dataSize = uint32_t(state.instanceMatrices.size() * sizeof(glm::mat4));
        state.instanceOffset = m_Render.AllocateInstanceData(dataSize);
        if (state.instanceOffset == UINT32_MAX)
        {
          YA_LOG_ERROR("Render", "Scatter instance buffer out of space: requested %u bytes, dropping scatter for entity %u",
            dataSize, task.entityId);
          m_Assets.Meshes().Destroy(task.setup.mesh);
          m_Assets.Materials().Destroy(task.setup.material);
          registry.remove<ScatterDirty>(task.entity);
          continue;
        }

        state.childEntity = m_Scene.CreateEntity("scatter_instances");
        m_Scene.SetParent(state.childEntity, task.entity);

        registry.emplace_or_replace<ScatterInstanceTag>(state.childEntity);
        registry.emplace_or_replace<MeshComponent>(state.childEntity, state.mesh);
        registry.emplace_or_replace<MaterialComponent>(state.childEntity, state.material);

        registry.emplace_or_replace<LocalBounds>(state.childEntity, LocalBounds {
          .min = boundsMin, .max = boundsMax
        });
        registry.emplace_or_replace<WorldBounds>(state.childEntity, WorldBounds {
          .min = boundsMin, .max = boundsMax
        });

        glm::vec3 meshMin = m_Assets.Meshes().GetMinBB(state.mesh);
        glm::vec3 meshMax = m_Assets.Meshes().GetMaxBB(state.mesh);
        BuildInstancedColliders(registry, state.childEntity, scatter,
                                state.instanceMatrices, meshMin, meshMax);

        m_States[task.entityId] = std::move(state);
        auto& stored = m_States[task.entityId];
        m_Assets.Meshes().SetInstanceData(stored.mesh, &stored.instanceMatrices, stored.instanceOffset);

        registry.remove<ScatterDirty>(task.entity);
      }
    }
    else
    {
      for (auto entity : standalone)
      {
        GenerateScatter(registry, entity);
        registry.remove<ScatterDirty>(entity);
      }
    }

    // Satellites always sequential (depend on standalone scatter state)
    for (auto entity : satellites)
    {
      GenerateSatelliteScatter(registry, entity);
      registry.remove<ScatterDirty>(entity);
    }
  }

  void ScatterSystem::DestroyState(uint32_t entityId)
  {
    auto it = m_States.find(entityId);
    if (it == m_States.end())
      return;

    auto& state = it->second;
    if (state.childEntity != entt::null)
      m_Scene.DestroyEntity(state.childEntity);

    m_PendingDestroys.push_back({
      .mesh = state.mesh,
      .material = state.material,
      .instanceOffset = state.instanceOffset,
      .instanceSize = uint32_t(state.instanceMatrices.size() * sizeof(glm::mat4)),
      .framesLeft = DESTROY_DELAY_FRAMES
    });

    m_States.erase(it);
  }

  void ScatterSystem::GenerateScatter(entt::registry& registry, entt::entity entity)
  {
    uint32_t entityId = static_cast<uint32_t>(entity);
    DestroyState(entityId);

    auto& scatter = registry.get<ScatterComponent>(entity);
    if (scatter.count == 0)
      return;

    const TerrainComponent* terrain = nullptr;
    entt::entity terrainEntity = entt::null;
    if (registry.all_of<TerrainComponent>(entity))
    {
      terrain = &registry.get<TerrainComponent>(entity);
      terrainEntity = entity;
    }
    else if (registry.all_of<HierarchyComponent>(entity))
    {
      auto parent = registry.get<HierarchyComponent>(entity).parent;
      if (parent != entt::null && registry.all_of<TerrainComponent>(parent))
      {
        terrain = &registry.get<TerrainComponent>(parent);
        terrainEntity = parent;
      }
    }

    if (!terrain)
    {
      YA_LOG_ERROR("Scene", "ScatterComponent requires TerrainComponent on entity or parent");
      return;
    }

    ScatterMeshSetup meshSetup;
    if (scatter.meshType == ScatterMeshType::Model && !scatter.modelPath.empty())
    {
      std::string absPath = m_Assets.ResolvePath(scatter.modelPath);
      auto it = m_ModelCache.find(absPath);
      if (it == m_ModelCache.end())
        it = m_ModelCache.emplace(absPath, ModelImporter::Import(absPath, false)).first;
      meshSetup = SetupModelMesh(scatter, entityId, m_Assets, it->second);
    }
    else
    {
      meshSetup = SetupPlaneMesh(scatter, entityId, m_Assets);
    }

    if (!meshSetup.valid)
      return;

    // Build road mask
    SplinePath3D roadSpline;
    float roadHalfWidth = 0.0f;
    bool hasRoadMask = false;
    if (scatter.useRoadMask)
    {
      auto roadView = registry.view<RoadComponent>();
      for (auto roadEntity : roadView)
      {
        auto& road = registry.get<RoadComponent>(roadEntity);
        if (road.points.size() >= 2)
        {
          roadSpline.points = road.points;
          roadHalfWidth = road.width * 0.5f;
          hasRoadMask = true;
          break;
        }
      }
    }

    uint32_t terrainEntityId = static_cast<uint32_t>(terrainEntity);
    float halfSize = terrain->size * 0.5f;
    float placementRadius = scatter.radius > 0.0f ? scatter.radius : halfSize;

    SplinePath2D spline;
    CatmullRomCurve curve;
    const SplinePath2D* splinePtr = nullptr;
    const CatmullRomCurve* curvePtr = nullptr;
    if (!terrain->maskPath.empty())
    {
      spline = SplinePath2D(terrain->maskPath);
      splinePtr = &spline;
      if (!terrain->maskCurve.empty())
      {
        curve = CatmullRomCurve(terrain->maskCurve);
        curvePtr = &curve;
      }
    }

    bool useCachedHeight = m_TerrainSystem.HasCachedHeight(terrainEntityId);
    float eps = terrain->size / static_cast<float>(terrain->subdivisions);

    uint32_t rngState = HashScatterSeed(scatter.seed);
    for (int i = 0; i < 4; i++)
      ScatterRandom(rngState);

    ScatterState state;
    state.mesh = meshSetup.mesh;
    state.material = meshSetup.material;
    state.instanceMatrices.reserve(scatter.count);

    for (uint32_t i = 0; i < scatter.count; i++)
    {
      float rx = ScatterRandom(rngState) * 2.0f - 1.0f;
      float rz = ScatterRandom(rngState) * 2.0f - 1.0f;

      float wx = glm::clamp(rx * placementRadius, -halfSize, halfSize);
      float wz = glm::clamp(rz * placementRadius, -halfSize, halfSize);

      if (hasRoadMask)
      {
        float dist = DistanceToRoadXZ(wx, wz, roadSpline, 128);
        float innerRadius = roadHalfWidth + scatter.roadMaskPadding;

        if (dist < innerRadius)
          continue;
        if (dist > scatter.roadMaskOuterRadius)
          continue;

        if (scatter.roadMaskFalloff > 0.0f)
        {
          float falloffStart = scatter.roadMaskOuterRadius - scatter.roadMaskFalloff;
          if (dist > falloffStart)
          {
            float falloffT = (dist - falloffStart) / scatter.roadMaskFalloff;
            if (ScatterRandom(rngState) < falloffT)
              continue;
          }
        }
      }

      float wy;
      if (useCachedHeight)
        wy = m_TerrainSystem.SampleCachedHeight(terrainEntityId, wx, wz);
      else
        wy = TerrainMeshGenerator::SampleSurfaceHeight(wx, wz, *terrain, splinePtr, curvePtr);

      float hL, hR, hD, hU;
      if (useCachedHeight)
      {
        hL = m_TerrainSystem.SampleCachedHeight(terrainEntityId, wx - eps, wz);
        hR = m_TerrainSystem.SampleCachedHeight(terrainEntityId, wx + eps, wz);
        hD = m_TerrainSystem.SampleCachedHeight(terrainEntityId, wx, wz - eps);
        hU = m_TerrainSystem.SampleCachedHeight(terrainEntityId, wx, wz + eps);
      }
      else
      {
        hL = TerrainMeshGenerator::SampleSurfaceHeight(wx - eps, wz, *terrain, splinePtr, curvePtr);
        hR = TerrainMeshGenerator::SampleSurfaceHeight(wx + eps, wz, *terrain, splinePtr, curvePtr);
        hD = TerrainMeshGenerator::SampleSurfaceHeight(wx, wz - eps, *terrain, splinePtr, curvePtr);
        hU = TerrainMeshGenerator::SampleSurfaceHeight(wx, wz + eps, *terrain, splinePtr, curvePtr);
      }

      glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * eps, hD - hU));
      if (normal.y < scatter.maxSlope)
        continue;

      float angle = 0.0f;
      if (scatter.randomYRotation)
        angle = ScatterRandom(rngState) * glm::two_pi<float>();

      float scale = scatter.minScale + ScatterRandom(rngState) * (scatter.maxScale - scatter.minScale);

      glm::mat4 m(1.0f);
      m = glm::translate(m, glm::vec3(wx, wy, wz));
      m = glm::rotate(m, angle, glm::vec3(0.0f, 1.0f, 0.0f));
      m = glm::scale(m, glm::vec3(scale));
      m = m * meshSetup.nodeTransform;

      state.instanceMatrices.push_back(m);
    }

    if (state.instanceMatrices.empty())
    {
      m_Assets.Meshes().Destroy(state.mesh);
      m_Assets.Materials().Destroy(state.material);
      return;
    }

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
    float meshHeight = scatter.meshType == ScatterMeshType::Plane
      ? scatter.planeHeight * scatter.maxScale
      : scatter.maxScale;
    float meshRadius = scatter.meshType == ScatterMeshType::Plane
      ? scatter.planeWidth * 0.5f * scatter.maxScale
      : scatter.maxScale;

    for (auto& m : state.instanceMatrices)
    {
      glm::vec3 pos(m[3]);
      boundsMin = glm::min(boundsMin, pos - glm::vec3(meshRadius, 0.0f, meshRadius));
      boundsMax = glm::max(boundsMax, pos + glm::vec3(meshRadius, meshHeight, meshRadius));
    }

    uint32_t dataSize = uint32_t(state.instanceMatrices.size() * sizeof(glm::mat4));
    state.instanceOffset = m_Render.AllocateInstanceData(dataSize);
    if (state.instanceOffset == UINT32_MAX)
    {
      YA_LOG_ERROR("Render", "Scatter instance buffer out of space: requested %u bytes, dropping scatter for entity %u",
        dataSize, entityId);
      m_Assets.Meshes().Destroy(state.mesh);
      m_Assets.Materials().Destroy(state.material);
      return;
    }

    state.childEntity = m_Scene.CreateEntity("scatter_instances");
    m_Scene.SetParent(state.childEntity, entity);

    registry.emplace_or_replace<ScatterInstanceTag>(state.childEntity);
    registry.emplace_or_replace<MeshComponent>(state.childEntity, state.mesh);
    registry.emplace_or_replace<MaterialComponent>(state.childEntity, state.material);

    registry.emplace_or_replace<LocalBounds>(state.childEntity, LocalBounds {
      .min = boundsMin, .max = boundsMax
    });
    registry.emplace_or_replace<WorldBounds>(state.childEntity, WorldBounds {
      .min = boundsMin, .max = boundsMax
    });

    {
      glm::vec3 meshMin = m_Assets.Meshes().GetMinBB(state.mesh);
      glm::vec3 meshMax = m_Assets.Meshes().GetMaxBB(state.mesh);
      BuildInstancedColliders(registry, state.childEntity, scatter,
                              state.instanceMatrices, meshMin, meshMax);
    }

    m_States[entityId] = std::move(state);
    auto& stored = m_States[entityId];
    m_Assets.Meshes().SetInstanceData(stored.mesh, &stored.instanceMatrices, stored.instanceOffset);
  }

  entt::entity ScatterSystem::FindEntityByName(entt::registry& registry, const std::string& name) const
  {
    auto view = registry.view<Name>();
    for (auto entity : view)
    {
      if (registry.get<Name>(entity) == name)
        return entity;
    }
    return entt::null;
  }

  void ScatterSystem::GenerateSatelliteScatter(entt::registry& registry, entt::entity entity)
  {
    uint32_t entityId = static_cast<uint32_t>(entity);
    DestroyState(entityId);

    auto& scatter = registry.get<ScatterComponent>(entity);

    entt::entity sourceEntity = FindEntityByName(registry, scatter.clusterSource);
    if (sourceEntity == entt::null)
    {
      YA_LOG_ERROR("Scene", "Satellite scatter: source entity '%s' not found", scatter.clusterSource.c_str());
      return;
    }
    uint32_t sourceId = static_cast<uint32_t>(sourceEntity);
    auto sourceIt = m_States.find(sourceId);
    if (sourceIt == m_States.end() || sourceIt->second.instanceMatrices.empty())
    {
      YA_LOG_ERROR("Scene", "Satellite scatter: source '%s' has no scatter instances", scatter.clusterSource.c_str());
      return;
    }
    auto& sourcePositions = sourceIt->second.instanceMatrices;

    const TerrainComponent* terrain = nullptr;
    entt::entity terrainEntity = entt::null;
    if (registry.all_of<TerrainComponent>(entity))
    {
      terrain = &registry.get<TerrainComponent>(entity);
      terrainEntity = entity;
    }
    else if (registry.all_of<HierarchyComponent>(entity))
    {
      auto parent = registry.get<HierarchyComponent>(entity).parent;
      if (parent != entt::null && registry.all_of<TerrainComponent>(parent))
      {
        terrain = &registry.get<TerrainComponent>(parent);
        terrainEntity = parent;
      }
    }

    if (!terrain)
    {
      YA_LOG_ERROR("Scene", "Satellite ScatterComponent requires TerrainComponent on entity or parent");
      return;
    }

    ScatterMeshSetup meshSetup;
    if (scatter.meshType == ScatterMeshType::Model && !scatter.modelPath.empty())
    {
      std::string absPath = m_Assets.ResolvePath(scatter.modelPath);
      auto it = m_ModelCache.find(absPath);
      if (it == m_ModelCache.end())
        it = m_ModelCache.emplace(absPath, ModelImporter::Import(absPath, false)).first;
      meshSetup = SetupModelMesh(scatter, entityId, m_Assets, it->second, "scatter_sat_");
    }
    else
    {
      meshSetup = SetupPlaneMesh(scatter, entityId, m_Assets, "scatter_sat_");
    }

    if (!meshSetup.valid)
      return;

    uint32_t terrainEntityId = static_cast<uint32_t>(terrainEntity);
    const TerrainHeightGrid* heightGrid = m_TerrainSystem.GetCachedHeightGrid(terrainEntityId);

    auto matrices = ComputeSatelliteMatrices(scatter, *terrain, heightGrid,
      meshSetup.nodeTransform, sourcePositions);

    if (matrices.empty())
    {
      m_Assets.Meshes().Destroy(meshSetup.mesh);
      m_Assets.Materials().Destroy(meshSetup.material);
      return;
    }

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
    float meshHeight = scatter.meshType == ScatterMeshType::Plane
      ? scatter.planeHeight * scatter.maxScale
      : scatter.maxScale;
    float meshRadius = scatter.meshType == ScatterMeshType::Plane
      ? scatter.planeWidth * 0.5f * scatter.maxScale
      : scatter.maxScale;

    for (auto& m : matrices)
    {
      glm::vec3 pos(m[3]);
      boundsMin = glm::min(boundsMin, pos - glm::vec3(meshRadius, 0.0f, meshRadius));
      boundsMax = glm::max(boundsMax, pos + glm::vec3(meshRadius, meshHeight, meshRadius));
    }

    ScatterState state;
    state.mesh = meshSetup.mesh;
    state.material = meshSetup.material;
    state.instanceMatrices = std::move(matrices);

    uint32_t dataSize = uint32_t(state.instanceMatrices.size() * sizeof(glm::mat4));
    state.instanceOffset = m_Render.AllocateInstanceData(dataSize);
    if (state.instanceOffset == UINT32_MAX)
    {
      YA_LOG_ERROR("Render", "Scatter instance buffer out of space: requested %u bytes, dropping scatter for entity %u",
        dataSize, entityId);
      m_Assets.Meshes().Destroy(state.mesh);
      m_Assets.Materials().Destroy(state.material);
      return;
    }

    state.childEntity = m_Scene.CreateEntity("scatter_satellite");
    m_Scene.SetParent(state.childEntity, entity);

    registry.emplace_or_replace<ScatterInstanceTag>(state.childEntity);
    registry.emplace_or_replace<MeshComponent>(state.childEntity, state.mesh);
    registry.emplace_or_replace<MaterialComponent>(state.childEntity, state.material);

    registry.emplace_or_replace<LocalBounds>(state.childEntity, LocalBounds {
      .min = boundsMin, .max = boundsMax
    });
    registry.emplace_or_replace<WorldBounds>(state.childEntity, WorldBounds {
      .min = boundsMin, .max = boundsMax
    });

    {
      glm::vec3 meshMin = m_Assets.Meshes().GetMinBB(state.mesh);
      glm::vec3 meshMax = m_Assets.Meshes().GetMaxBB(state.mesh);
      BuildInstancedColliders(registry, state.childEntity, scatter,
                              state.instanceMatrices, meshMin, meshMax);
    }

    m_States[entityId] = std::move(state);
    auto& stored = m_States[entityId];
    m_Assets.Meshes().SetInstanceData(stored.mesh, &stored.instanceMatrices, stored.instanceOffset);
  }
}
