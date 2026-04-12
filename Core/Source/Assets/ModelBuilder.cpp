#include "ModelBuilder.h"

#include "AssetManager.h"
#include "Utils/Log.h"

namespace YAEngine
{
  Entity ModelBuilder::Build(const ModelDescription& desc)
  {
    Entity rootEntity = m_Scene->CreateEntity(desc.root.name);

    Entity prevChild = entt::null;
    glm::vec3 rootMinBB( std::numeric_limits<float>::max());
    glm::vec3 rootMaxBB(-std::numeric_limits<float>::max());

    for (auto& childNode : desc.root.children)
    {
      Entity child = BuildNode(childNode, rootEntity, desc);
      auto& childBounds = m_Scene->GetComponent<LocalBounds>(child);

      rootMinBB = glm::min(rootMinBB, childBounds.min);
      rootMaxBB = glm::max(rootMaxBB, childBounds.max);

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

  Entity ModelBuilder::BuildNode(const NodeDescription& node, Entity parent, const ModelDescription& desc)
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

      if (!matDesc.baseColorTexture.empty())
      {
        mat.baseColorTexture = m_AssetManager->Textures().Load(matDesc.baseColorTexture, &mat.hasAlpha);
        mat.alphaTest = mat.hasAlpha;
      }
      if (!matDesc.metallicTexture.empty())
        mat.metallicTexture = m_AssetManager->Textures().Load(matDesc.metallicTexture, nullptr, true);
      if (!matDesc.roughnessTexture.empty())
        mat.roughnessTexture = m_AssetManager->Textures().Load(matDesc.roughnessTexture, nullptr, true);
      if (!matDesc.specularTexture.empty())
        mat.specularTexture = m_AssetManager->Textures().Load(matDesc.specularTexture, nullptr, true);
      if (!matDesc.emissiveTexture.empty())
        mat.emissiveTexture = m_AssetManager->Textures().Load(matDesc.emissiveTexture);
      if (!matDesc.normalTexture.empty())
        mat.normalTexture = m_AssetManager->Textures().Load(matDesc.normalTexture, nullptr, true);
      if (!matDesc.heightTexture.empty())
        mat.heightTexture = m_AssetManager->Textures().Load(matDesc.heightTexture, nullptr, true);

      m_Scene->AddComponent<MaterialComponent>(entity, materialHandle);
    }

    Entity prevChild = entt::null;

    for (auto& childNode : node.children)
    {
      Entity child = BuildNode(childNode, entity, desc);
      auto& childBounds = m_Scene->GetComponent<LocalBounds>(child);

      nodeMinBB = glm::min(nodeMinBB, childBounds.min);
      nodeMaxBB = glm::max(nodeMaxBB, childBounds.max);

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
