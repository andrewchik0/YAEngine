#include "ModelBuilder.h"

#include "AssetManager.h"
#include "Utils/Log.h"

namespace YAEngine
{
  static glm::mat4 ComposeNodeLocal(const NodeDescription& node)
  {
    return glm::translate(glm::mat4(1.0f), node.position)
         * glm::toMat4(node.rotation)
         * glm::scale(glm::mat4(1.0f), node.scale);
  }

  // Expand an AABB by another AABB transformed into the same frame by matrix m.
  static void AccumulateTransformedAABB(const glm::vec3& inMin, const glm::vec3& inMax, const glm::mat4& m,
                                        glm::vec3& outMin, glm::vec3& outMax)
  {
    // empty child bounds: skip so they don't pollute the union
    if (inMin.x > inMax.x || inMin.y > inMax.y || inMin.z > inMax.z) return;

    const glm::vec3 corners[8] = {
      { inMin.x, inMin.y, inMin.z }, { inMax.x, inMin.y, inMin.z },
      { inMin.x, inMax.y, inMin.z }, { inMax.x, inMax.y, inMin.z },
      { inMin.x, inMin.y, inMax.z }, { inMax.x, inMin.y, inMax.z },
      { inMin.x, inMax.y, inMax.z }, { inMax.x, inMax.y, inMax.z },
    };
    for (auto& c : corners)
    {
      const glm::vec3 tc = glm::vec3(m * glm::vec4(c, 1.0f));
      outMin = glm::min(outMin, tc);
      outMax = glm::max(outMax, tc);
    }
  }

  Entity ModelBuilder::Build(const ModelDescription& desc)
  {
    Entity rootEntity = m_Scene->CreateEntity(desc.root.name);

    Entity prevChild = entt::null;
    glm::vec3 rootMinBB( std::numeric_limits<float>::max());
    glm::vec3 rootMaxBB(-std::numeric_limits<float>::max());

    // Meshes sharing a material description share one Material, so editing it in the
    // inspector affects every mesh that uses it
    std::unordered_map<uint32_t, MaterialHandle> materialCache;

    for (auto& childNode : desc.root.children)
    {
      Entity child = BuildNode(childNode, rootEntity, desc, materialCache);
      auto& childBounds = m_Scene->GetComponent<LocalBounds>(child);

      AccumulateTransformedAABB(childBounds.min, childBounds.max, ComposeNodeLocal(childNode), rootMinBB, rootMaxBB);

      if (prevChild == entt::null)
        m_Scene->GetHierarchy(rootEntity).firstChild = child;
      else
        m_Scene->GetHierarchy(prevChild).nextSibling = child;

      prevChild = child;
    }

    m_Scene->AddComponent<LocalBounds>(rootEntity, LocalBounds { .min = rootMinBB, .max = rootMaxBB });
    m_Scene->AddComponent<BoundsDirty>(rootEntity);

    return rootEntity;
  }

  Entity ModelBuilder::BuildNode(const NodeDescription& node, Entity parent, const ModelDescription& desc,
    std::unordered_map<uint32_t, MaterialHandle>& materialCache)
  {
    Entity entity = m_Scene->CreateEntity(node.name);

    m_Scene->GetHierarchy(entity).parent = parent;
    m_Scene->RemoveComponent<RootTag>(entity);
    m_Scene->GetTransform(entity).position = node.position;
    m_Scene->GetTransform(entity).rotation = node.rotation;
    m_Scene->GetTransform(entity).scale = node.scale;

    m_Scene->MarkDirty(entity);

    glm::vec3 nodeMinBB( std::numeric_limits<float>::max());
    glm::vec3 nodeMaxBB(-std::numeric_limits<float>::max());

    if (node.meshIndex.has_value())
    {
      auto& meshDesc = desc.meshes[*node.meshIndex];
      auto meshHandle = m_AssetManager->Meshes().Load(meshDesc.vertices, meshDesc.indices);
      m_Scene->AddComponent<MeshComponent>(entity, meshHandle);

      nodeMinBB = meshDesc.minBB;
      nodeMaxBB = meshDesc.maxBB;
    }

    if (node.materialIndex.has_value())
    {
      auto cached = materialCache.find(*node.materialIndex);
      if (cached != materialCache.end())
      {
        m_Scene->AddComponent<MaterialComponent>(entity, cached->second);
      }
      else
      {
        auto& matDesc = desc.materials[*node.materialIndex];
        auto materialHandle = m_AssetManager->Materials().Create();
        auto& mat = m_AssetManager->Materials().Get(materialHandle);

        if (!matDesc.name.empty())
          mat.name = matDesc.name;

        mat.albedo = matDesc.albedo;
        mat.emissivity = matDesc.emissivity;
        mat.roughness = matDesc.roughness;
        mat.metallic = matDesc.metallic;
        mat.roughnessFactor = matDesc.roughnessFactor;
        mat.metallicFactor = matDesc.metallicFactor;
        mat.specular = matDesc.specular;
        mat.sg = matDesc.sg;
        mat.combinedTextures = matDesc.combinedTextures;
        mat.doubleSided = matDesc.doubleSided;
        mat.transparent = matDesc.transparent;

        auto loadTexture = [&](const std::string& path, bool linear, bool* hasAlpha)
        {
          const EmbeddedTexture* embedded = desc.FindEmbedded(path);
          return embedded != nullptr
            ? m_AssetManager->Textures().LoadEmbedded(*embedded, path, hasAlpha, linear)
            : m_AssetManager->Textures().Load(path, hasAlpha, linear);
        };

        if (!matDesc.baseColorTexture.empty())
        {
          mat.baseColorTexture = loadTexture(matDesc.baseColorTexture, false, &mat.hasAlpha);
          mat.alphaTest = mat.hasAlpha && !mat.transparent;
        }
        if (!matDesc.metallicTexture.empty())
          mat.metallicTexture = loadTexture(matDesc.metallicTexture, true, nullptr);
        if (!matDesc.roughnessTexture.empty())
          mat.roughnessTexture = loadTexture(matDesc.roughnessTexture, true, nullptr);
        if (!matDesc.specularTexture.empty())
          mat.specularTexture = loadTexture(matDesc.specularTexture, true, nullptr);
        if (!matDesc.emissiveTexture.empty())
          mat.emissiveTexture = loadTexture(matDesc.emissiveTexture, false, nullptr);
        if (!matDesc.normalTexture.empty())
          mat.normalTexture = loadTexture(matDesc.normalTexture, true, nullptr);
        if (!matDesc.heightTexture.empty())
          mat.heightTexture = loadTexture(matDesc.heightTexture, true, nullptr);

        materialCache.emplace(*node.materialIndex, materialHandle);
        m_Scene->AddComponent<MaterialComponent>(entity, materialHandle);
      }
    }

    Entity prevChild = entt::null;

    for (auto& childNode : node.children)
    {
      Entity child = BuildNode(childNode, entity, desc, materialCache);
      auto& childBounds = m_Scene->GetComponent<LocalBounds>(child);

      AccumulateTransformedAABB(childBounds.min, childBounds.max, ComposeNodeLocal(childNode), nodeMinBB, nodeMaxBB);

      if (prevChild == entt::null)
      {
        m_Scene->GetHierarchy(entity).firstChild = child;
      }
      else
      {
        m_Scene->GetHierarchy(prevChild).nextSibling = child;
      }

      prevChild = child;
    }

    m_Scene->AddComponent<LocalBounds>(entity, LocalBounds { .min = nodeMinBB, .max = nodeMaxBB });
    m_Scene->AddComponent<BoundsDirty>(entity);

    return entity;
  }
}
