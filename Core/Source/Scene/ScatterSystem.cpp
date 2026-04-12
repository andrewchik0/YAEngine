#include "ScatterSystem.h"

#include "Components.h"
#include "Scene.h"
#include "Assets/AssetManager.h"
#include "Assets/ModelImporter.h"
#include "Render/Render.h"
#include "Utils/TerrainMeshGenerator.h"
#include "Utils/PrimitiveMeshFactory.h"
#include "Utils/SplinePath2D.h"
#include "Utils/CatmullRomCurve.h"
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

  static const NodeDescription* FindMeshNode(const NodeDescription& node)
  {
    if (node.meshIndex.has_value())
      return &node;
    for (auto& child : node.children)
    {
      auto* found = FindMeshNode(child);
      if (found) return found;
    }
    return nullptr;
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
        m_PendingDestroys[i] = m_PendingDestroys.back();
        m_PendingDestroys.pop_back();
      }
      else
      {
        i++;
      }
    }

    auto view = registry.view<ScatterComponent, ScatterDirty>();
    for (auto entity : view)
    {
      GenerateScatter(registry, entity);
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

    // Find terrain on this entity or parent
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

    // Create mesh + material
    ScatterState state;
    glm::mat4 nodeTransform(1.0f);

    if (scatter.meshType == ScatterMeshType::Model && !scatter.modelPath.empty())
    {
      std::string absPath = m_Assets.ResolvePath(scatter.modelPath);
      auto desc = ModelImporter::Import(absPath, false);

      // Find first node with a mesh
      const NodeDescription* meshNode = nullptr;
      for (auto& child : desc.root.children)
      {
        meshNode = FindMeshNode(child);
        if (meshNode) break;
      }

      if (!meshNode || !meshNode->meshIndex.has_value())
      {
        YA_LOG_ERROR("Scene", "Scatter model has no mesh: %s", scatter.modelPath.c_str());
        return;
      }

      // Load mesh
      auto& meshDesc = desc.meshes[*meshNode->meshIndex];
      state.mesh = m_Assets.Meshes().Load(meshDesc.vertices, meshDesc.indices);

      // Bake node transform into instance matrices
      nodeTransform = glm::translate(glm::mat4(1.0f), meshNode->position)
                    * glm::mat4_cast(meshNode->rotation)
                    * glm::scale(glm::mat4(1.0f), meshNode->scale);

      // Load material from model
      state.material = m_Assets.Materials().Create();
      auto& mat = m_Assets.Materials().Get(state.material);
      mat.name = "scatter_" + std::to_string(entityId);

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
          mat.baseColorTexture = m_Assets.Textures().Load(matDesc.baseColorTexture, &mat.hasAlpha);
          mat.alphaTest = mat.hasAlpha;
        }
        if (!matDesc.metallicTexture.empty())
          mat.metallicTexture = m_Assets.Textures().Load(matDesc.metallicTexture, nullptr, true);
        if (!matDesc.roughnessTexture.empty())
          mat.roughnessTexture = m_Assets.Textures().Load(matDesc.roughnessTexture, nullptr, true);
        if (!matDesc.normalTexture.empty())
          mat.normalTexture = m_Assets.Textures().Load(matDesc.normalTexture, nullptr, true);
      }
    }
    else
    {
      // Plane billboard
      ProcMesh quad;
      float hw = scatter.planeWidth * 0.5f;
      float hh = scatter.planeHeight * 0.5f;
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
      state.mesh = m_Assets.Meshes().Load(quad.vertices, quad.indices);

      state.material = m_Assets.Materials().Create();
      auto& mat = m_Assets.Materials().Get(state.material);
      mat.name = "scatter_" + std::to_string(entityId);
      mat.doubleSided = true;
      if (!scatter.materialPath.empty())
      {
        std::string absPath = m_Assets.ResolvePath(scatter.materialPath);
        mat.baseColorTexture = m_Assets.Textures().Load(absPath, &mat.hasAlpha);
        mat.alphaTest = mat.hasAlpha;
      }
    }

    // Generate placement positions
    float halfSize = terrain->size * 0.5f;
    float placementRadius = scatter.radius > 0.0f ? scatter.radius : halfSize;

    // Pre-build spline/curve once for all height samples
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

    uint32_t rngState = HashScatterSeed(scatter.seed);
    // Warm up RNG
    for (int i = 0; i < 4; i++)
      ScatterRandom(rngState);

    state.instanceMatrices.reserve(scatter.count);

    for (uint32_t i = 0; i < scatter.count; i++)
    {
      float rx = ScatterRandom(rngState) * 2.0f - 1.0f;
      float rz = ScatterRandom(rngState) * 2.0f - 1.0f;

      float wx = rx * placementRadius;
      float wz = rz * placementRadius;

      // Clamp to terrain bounds
      wx = glm::clamp(wx, -halfSize, halfSize);
      wz = glm::clamp(wz, -halfSize, halfSize);

      float wy = TerrainMeshGenerator::SampleSurfaceHeight(wx, wz, *terrain, splinePtr, curvePtr);

      // Compute slope via finite differences
      float eps = terrain->size / static_cast<float>(terrain->subdivisions);
      float hL = TerrainMeshGenerator::SampleSurfaceHeight(wx - eps, wz, *terrain, splinePtr, curvePtr);
      float hR = TerrainMeshGenerator::SampleSurfaceHeight(wx + eps, wz, *terrain, splinePtr, curvePtr);
      float hD = TerrainMeshGenerator::SampleSurfaceHeight(wx, wz - eps, *terrain, splinePtr, curvePtr);
      float hU = TerrainMeshGenerator::SampleSurfaceHeight(wx, wz + eps, *terrain, splinePtr, curvePtr);

      glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * eps, hD - hU));
      float slopeY = normal.y;

      if (slopeY < scatter.maxSlope)
        continue;

      // Random Y rotation
      float angle = 0.0f;
      if (scatter.randomYRotation)
        angle = ScatterRandom(rngState) * glm::two_pi<float>();

      // Random scale
      float scale = scatter.minScale + ScatterRandom(rngState) * (scatter.maxScale - scatter.minScale);

      glm::mat4 m(1.0f);
      m = glm::translate(m, glm::vec3(wx, wy, wz));
      m = glm::rotate(m, angle, glm::vec3(0.0f, 1.0f, 0.0f));
      m = glm::scale(m, glm::vec3(scale));
      m = m * nodeTransform;

      state.instanceMatrices.push_back(m);
    }

    if (state.instanceMatrices.empty())
    {
      m_Assets.Meshes().Destroy(state.mesh);
      m_Assets.Materials().Destroy(state.material);
      return;
    }

    // Compute world bounds from all instance positions
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

    // Allocate instance buffer
    uint32_t dataSize = uint32_t(state.instanceMatrices.size() * sizeof(glm::mat4));
    state.instanceOffset = m_Render.AllocateInstanceData(dataSize);

    // Create child entity with mesh + material
    state.childEntity = m_Scene.CreateEntity("scatter_instances");
    m_Scene.SetParent(state.childEntity, entity);

    registry.emplace_or_replace<ScatterInstanceTag>(state.childEntity);
    registry.emplace_or_replace<MeshComponent>(state.childEntity, state.mesh);
    registry.emplace_or_replace<MaterialComponent>(state.childEntity, state.material);

    // Set bounds covering all instances to prevent incorrect frustum culling
    registry.emplace_or_replace<LocalBounds>(state.childEntity, LocalBounds {
      .min = boundsMin,
      .max = boundsMax
    });
    registry.emplace_or_replace<WorldBounds>(state.childEntity, WorldBounds {
      .min = boundsMin,
      .max = boundsMax
    });

    // Move state into map first, then set instance data from stored reference
    // to avoid dangling pointer after std::move
    m_States[entityId] = std::move(state);
    auto& stored = m_States[entityId];
    m_Assets.Meshes().SetInstanceData(stored.mesh, &stored.instanceMatrices, stored.instanceOffset);
  }
}
