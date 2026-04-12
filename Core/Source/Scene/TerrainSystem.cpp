#include "TerrainSystem.h"

#include "Components.h"
#include "Assets/AssetManager.h"
#include "Utils/TerrainMeshGenerator.h"
#include "Utils/ThreadPool.h"
#include "Utils/Log.h"

namespace YAEngine
{
  void TerrainSystem::OnSceneClear()
  {
    m_PendingDestroys.clear();
    m_HeightmapCache.clear();
    m_HeightGridCache.clear();
  }

  bool TerrainSystem::HasCachedHeight(uint32_t entityId) const
  {
    return m_HeightGridCache.contains(entityId);
  }

  const TerrainHeightGrid* TerrainSystem::GetCachedHeightGrid(uint32_t entityId) const
  {
    auto it = m_HeightGridCache.find(entityId);
    return it != m_HeightGridCache.end() ? &it->second : nullptr;
  }

  float TerrainSystem::SampleCachedHeight(uint32_t entityId, float worldX, float worldZ) const
  {
    auto it = m_HeightGridCache.find(entityId);
    if (it == m_HeightGridCache.end())
      return 0.0f;

    auto& grid = it->second;
    float halfSize = grid.size * 0.5f;

    float normX = (worldX + halfSize) / grid.size;
    float normZ = (worldZ + halfSize) / grid.size;
    normX = glm::clamp(normX, 0.0f, 1.0f);
    normZ = glm::clamp(normZ, 0.0f, 1.0f);

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

    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    return h0 + (h1 - h0) * fz;
  }

  static TerrainGenResult GenerateTerrainCpu(
    const TerrainComponent& terrain,
    const std::string& absHeightmapPath,
    const HeightmapData* cachedHeightmap)
  {
    TerrainGenResult result;

    const HeightmapData* hmPtr = cachedHeightmap;

    if (!absHeightmapPath.empty() && !cachedHeightmap)
    {
      if (HeightmapLoader::Load(absHeightmapPath, result.loadedHeightmap))
      {
        hmPtr = &result.loadedHeightmap;
        result.heightmapPath = terrain.heightmapPath;
        result.hasNewHeightmap = true;
      }
      else
      {
        YA_LOG_ERROR("Assets", "Failed to load heightmap, falling back to procedural: %s",
          absHeightmapPath.c_str());
      }
    }

    if (hmPtr)
      result.mesh = TerrainMeshGenerator::Generate(terrain, *hmPtr);
    else
      result.mesh = TerrainMeshGenerator::Generate(terrain);

    result.heightGrid.vertsPerSide = terrain.subdivisions + 1;
    result.heightGrid.size = terrain.size;
    result.heightGrid.heights.resize(result.mesh.vertices.size());
    for (size_t i = 0; i < result.mesh.vertices.size(); i++)
      result.heightGrid.heights[i] = result.mesh.vertices[i].position.y;

    return result;
  }

  void TerrainSystem::FinalizeTerrain(entt::registry& registry, entt::entity entity,
                                      TerrainGenResult&& result, MeshHandle oldMesh, bool hadOldMesh)
  {
    uint32_t entityId = static_cast<uint32_t>(entity);

    if (hadOldMesh && m_Assets.Meshes().Has(oldMesh))
      m_PendingDestroys.push_back({ oldMesh, DESTROY_DELAY_FRAMES });

    if (result.hasNewHeightmap)
    {
      CachedHeightmap cached;
      cached.path = result.heightmapPath;
      cached.data = std::move(result.loadedHeightmap);
      m_HeightmapCache[entityId] = std::move(cached);
    }
    else if (registry.get<TerrainComponent>(entity).heightmapPath.empty())
    {
      m_HeightmapCache.erase(entityId);
    }

    m_HeightGridCache[entityId] = std::move(result.heightGrid);

    auto handle = m_Assets.Meshes().Load(result.mesh.vertices, result.mesh.indices);
    registry.emplace_or_replace<MeshComponent>(entity, handle);

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(-std::numeric_limits<float>::max());
    for (auto& v : result.mesh.vertices)
    {
      boundsMin = glm::min(boundsMin, v.position);
      boundsMax = glm::max(boundsMax, v.position);
    }

    registry.emplace_or_replace<LocalBounds>(entity, LocalBounds {
      .min = boundsMin,
      .max = boundsMax
    });
    registry.emplace_or_replace<BoundsDirty>(entity);
    registry.remove<TerrainDirty>(entity);
  }

  void TerrainSystem::Update(entt::registry& registry, double dt)
  {
    for (size_t i = 0; i < m_PendingDestroys.size();)
    {
      auto& pending = m_PendingDestroys[i];
      if (--pending.framesLeft == 0)
      {
        if (m_Assets.Meshes().Has(pending.handle))
          m_Assets.Meshes().Destroy(pending.handle);
        m_PendingDestroys[i] = m_PendingDestroys.back();
        m_PendingDestroys.pop_back();
      }
      else
      {
        i++;
      }
    }

    auto view = registry.view<TerrainComponent, TerrainDirty>();

    if (m_ThreadPool)
    {
      struct TerrainTask
      {
        entt::entity entity;
        MeshHandle oldMesh;
        bool hadOldMesh = false;
        std::future<TerrainGenResult> future;
      };

      std::vector<TerrainTask> tasks;

      for (auto entity : view)
      {
        auto& terrain = registry.get<TerrainComponent>(entity);
        uint32_t entityId = static_cast<uint32_t>(entity);

        TerrainTask task;
        task.entity = entity;

        if (registry.all_of<MeshComponent>(entity))
        {
          task.oldMesh = registry.get<MeshComponent>(entity).asset;
          task.hadOldMesh = true;
        }

        const HeightmapData* cachedHm = nullptr;
        std::string absPath;

        if (!terrain.heightmapPath.empty())
        {
          auto it = m_HeightmapCache.find(entityId);
          if (it != m_HeightmapCache.end() && it->second.path == terrain.heightmapPath)
            cachedHm = &it->second.data;
          else
            absPath = m_Assets.ResolvePath(terrain.heightmapPath);
        }

        TerrainComponent terrainCopy = terrain;
        task.future = m_ThreadPool->Submit(
          [terrainCopy, absPath, cachedHm]() {
            return GenerateTerrainCpu(terrainCopy, absPath, cachedHm);
          });

        tasks.push_back(std::move(task));
      }

      for (auto& task : tasks)
      {
        auto result = task.future.get();
        FinalizeTerrain(registry, task.entity, std::move(result),
          task.oldMesh, task.hadOldMesh);
      }
    }
    else
    {
      for (auto entity : view)
      {
        auto& terrain = registry.get<TerrainComponent>(entity);
        uint32_t entityId = static_cast<uint32_t>(entity);

        MeshHandle oldMesh;
        bool hadOldMesh = false;
        if (registry.all_of<MeshComponent>(entity))
        {
          oldMesh = registry.get<MeshComponent>(entity).asset;
          hadOldMesh = true;
        }

        const HeightmapData* cachedHm = nullptr;
        std::string absPath;

        if (!terrain.heightmapPath.empty())
        {
          auto it = m_HeightmapCache.find(entityId);
          if (it == m_HeightmapCache.end() || it->second.path != terrain.heightmapPath)
          {
            absPath = m_Assets.ResolvePath(terrain.heightmapPath);
          }
          else
          {
            cachedHm = &it->second.data;
          }
        }

        auto result = GenerateTerrainCpu(terrain, absPath, cachedHm);
        FinalizeTerrain(registry, entity, std::move(result), oldMesh, hadOldMesh);
      }
    }
  }
}
