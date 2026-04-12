#include "TerrainSystem.h"

#include "Components.h"
#include "Assets/AssetManager.h"
#include "Utils/TerrainMeshGenerator.h"
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
    for (auto entity : view)
    {
      auto& terrain = registry.get<TerrainComponent>(entity);

      if (registry.all_of<MeshComponent>(entity))
      {
        auto oldHandle = registry.get<MeshComponent>(entity).asset;
        if (m_Assets.Meshes().Has(oldHandle))
          m_PendingDestroys.push_back({ oldHandle, DESTROY_DELAY_FRAMES });
      }

      ProcMesh procMesh;
      uint32_t entityId = static_cast<uint32_t>(entity);

      if (!terrain.heightmapPath.empty())
      {
        auto it = m_HeightmapCache.find(entityId);
        if (it == m_HeightmapCache.end() || it->second.path != terrain.heightmapPath)
        {
          std::string absPath = m_Assets.ResolvePath(terrain.heightmapPath);
          CachedHeightmap cached;
          cached.path = terrain.heightmapPath;
          if (HeightmapLoader::Load(absPath, cached.data))
          {
            m_HeightmapCache[entityId] = std::move(cached);
          }
          else
          {
            YA_LOG_ERROR("Assets", "Failed to load heightmap, falling back to procedural: %s", absPath.c_str());
            m_HeightmapCache.erase(entityId);
          }
        }

        it = m_HeightmapCache.find(entityId);
        if (it != m_HeightmapCache.end())
          procMesh = TerrainMeshGenerator::Generate(terrain, it->second.data);
        else
          procMesh = TerrainMeshGenerator::Generate(terrain);
      }
      else
      {
        m_HeightmapCache.erase(entityId);
        procMesh = TerrainMeshGenerator::Generate(terrain);
      }

      // Cache height grid for scatter sampling
      TerrainHeightGrid heightGrid;
      heightGrid.vertsPerSide = terrain.subdivisions + 1;
      heightGrid.size = terrain.size;
      heightGrid.heights.resize(procMesh.vertices.size());
      for (size_t i = 0; i < procMesh.vertices.size(); i++)
        heightGrid.heights[i] = procMesh.vertices[i].position.y;
      m_HeightGridCache[entityId] = std::move(heightGrid);

      auto handle = m_Assets.Meshes().Load(procMesh.vertices, procMesh.indices);
      registry.emplace_or_replace<MeshComponent>(entity, handle);

      glm::vec3 boundsMin(std::numeric_limits<float>::max());
      glm::vec3 boundsMax(-std::numeric_limits<float>::max());
      for (auto& v : procMesh.vertices)
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
  }
}
