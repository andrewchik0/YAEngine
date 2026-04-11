#include "TerrainSystem.h"

#include "Components.h"
#include "Assets/AssetManager.h"
#include "Utils/TerrainMeshGenerator.h"
#include "Utils/Log.h"

namespace YAEngine
{
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
