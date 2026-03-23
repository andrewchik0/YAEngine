#include "ModelBuilder.h"

#include "AssetManager.h"

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
      auto& childTc = m_Scene->GetTransform(child);

      rootMinBB = glm::min(rootMinBB, childTc.minBB);
      rootMaxBB = glm::max(rootMaxBB, childTc.maxBB);

      if (prevChild == entt::null)
        m_Scene->GetTransform(rootEntity).firstChild = child;
      else
        m_Scene->GetTransform(prevChild).nextSibling = child;

      prevChild = child;
    }

    auto& rootTc = m_Scene->GetTransform(rootEntity);
    rootTc.minBB = rootMinBB;
    rootTc.maxBB = rootMaxBB;

    return rootEntity;
  }

  Entity ModelBuilder::BuildNode(const NodeDescription& node, Entity parent, const ModelDescription& desc)
  {
    Entity entity = m_Scene->CreateEntity(node.name);
    auto& tc = m_Scene->GetTransform(entity);

    tc.parent = parent;
    tc.position = node.position;
    tc.rotation = node.rotation;
    tc.scale = node.scale;

    tc.local = glm::translate(glm::mat4(1.0f), tc.position)
             * glm::mat4_cast(tc.rotation)
             * glm::scale(glm::mat4(1.0f), tc.scale);
    tc.dirty = true;

    if (node.meshIndex.has_value())
    {
      auto& meshDesc = desc.meshes[*node.meshIndex];
      auto meshHandle = m_AssetManager->Meshes().Load(meshDesc.vertices, meshDesc.indices);
      m_Scene->AddComponent<MeshComponent>(entity, meshHandle);

      tc.minBB = meshDesc.minBB;
      tc.maxBB = meshDesc.maxBB;
    }

    if (node.materialIndex.has_value())
    {
      auto& matDesc = desc.materials[*node.materialIndex];
      auto materialHandle = m_AssetManager->Materials().Create();
      auto& mat = m_AssetManager->Materials().Get(materialHandle);

      mat.albedo = matDesc.albedo;
      mat.emissivity = matDesc.emissivity;
      mat.roughness = matDesc.roughness;
      mat.specular = matDesc.specular;
      mat.sg = matDesc.sg;
      mat.combinedTextures = matDesc.combinedTextures;

      if (!matDesc.baseColorTexture.empty())
        mat.baseColorTexture = m_AssetManager->Textures().Load(matDesc.baseColorTexture, &mat.hasAlpha);
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
    glm::vec3 nodeMinBB = node.meshIndex.has_value() ? tc.minBB : glm::vec3( std::numeric_limits<float>::max());
    glm::vec3 nodeMaxBB = node.meshIndex.has_value() ? tc.maxBB : glm::vec3(-std::numeric_limits<float>::max());

    for (auto& childNode : node.children)
    {
      Entity child = BuildNode(childNode, entity, desc);
      auto& childTc = m_Scene->GetTransform(child);

      nodeMinBB = glm::min(nodeMinBB, childTc.minBB);
      nodeMaxBB = glm::max(nodeMaxBB, childTc.maxBB);

      if (prevChild == entt::null)
        tc.firstChild = child;
      else
        m_Scene->GetTransform(prevChild).nextSibling = child;

      prevChild = child;
    }

    tc.minBB = nodeMinBB;
    tc.maxBB = nodeMaxBB;

    return entity;
  }
}
