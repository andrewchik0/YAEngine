#include "ModelManager.h"

#include <filesystem>

#include "AssetManager.h"
#include "Scene/Scene.h"

#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/pbrmaterial.h>

namespace YAEngine
{
  ModelHandle ModelManager::Load(const std::string& path)
  {
    Assimp::Importer importer;
    const aiScene* scene =
      importer.ReadFile(path,
                        aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_GenUVCoords | aiProcess_FlipUVs |
                        aiProcess_GenNormals | aiProcess_Triangulate);

    if (scene == nullptr)
    {
      return -1;
    }

    auto model = std::make_unique<Model>();

    model->basePath = std::filesystem::path(path).parent_path();

    model->rootEntity = m_Scene->CreateEntity(std::filesystem::path(path).filename().string());

    m_Scene->GetTransform(model->rootEntity).firstChild = ProcessNode(*model, model->rootEntity, scene->mRootNode, scene);

    return AssetManagerBase::Load(std::move(model));
  }

  Entity ModelManager::ProcessNode(Model& model, Entity& parent, aiNode* node, const aiScene* scene)
  {
    auto nodeEntity = m_Scene->CreateEntity(node->mName.C_Str());
    auto& tc = m_Scene->GetTransform(nodeEntity);

    tc.parent = parent;

    aiVector3D pos, scale;
    aiQuaternion rot;
    node->mTransformation.Decompose(scale, rot, pos);

    tc.position = { pos.x, pos.y, pos.z };
    tc.rotation = { rot.w, rot.x, rot.y, rot.z };
    tc.scale    = { scale.x, scale.y, scale.z };

    tc.local = glm::translate(glm::mat4(1.0f), tc.position)
             * glm::mat4_cast(tc.rotation)
             * glm::scale(glm::mat4(1.0f), tc.scale);

    tc.dirty = true;

    Entity prevChild = entt::null;

    for (size_t i = 0; i < node->mNumMeshes; ++i)
    {
      aiMesh* ai_mesh = scene->mMeshes[node->mMeshes[i]];

      entt::entity meshEntity = ProcessMesh(model, ai_mesh, scene);

      auto& mt = m_Scene->GetTransform(meshEntity);
      mt.parent = nodeEntity;
      mt.local  = glm::mat4(1.0f);
      mt.world  = glm::mat4(1.0f);
      mt.dirty  = true;

      if (prevChild == entt::null)
        tc.firstChild = meshEntity;
      else
        m_Scene->GetTransform(prevChild).nextSibling = meshEntity;

      prevChild = meshEntity;
    }
    for (size_t i = 0; i < node->mNumChildren; ++i)
    {
      auto child = ProcessNode(model, nodeEntity, node->mChildren[i], scene);
      if (prevChild == entt::null)
        tc.firstChild = child;
      else
        m_Scene->GetTransform(prevChild).nextSibling = child;

      prevChild = child;
    }
    return nodeEntity;
  }

  Entity ModelManager::ProcessMesh(Model& model, aiMesh* mesh, const aiScene* scene)
  {
    Entity entity = m_Scene->CreateEntity(mesh->mName.C_Str());

    aiMaterial* aiMat = scene->mMaterials[mesh->mMaterialIndex];
    ProcessMaterial(model, entity, aiMat);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(mesh->mNumVertices);
    indices.reserve(mesh->mNumFaces * 3);

    for (size_t i = 0; i < mesh->mNumVertices; ++i)
    {
      Vertex vertex;
      glm::vec4 pos = glm::vec4(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
      glm::vec4 normal = mesh->mNormals != nullptr
        ? glm::vec4(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z, 0.0f)
        : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

      vertex.position = pos;
      vertex.normal = glm::normalize(normal);

      if (mesh->HasTangentsAndBitangents())
      {
        glm::vec4 tangent = glm::vec4(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z, 0.0f);
        glm::vec4 bitangent = glm::vec4(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z, 0.0f);
        vertex.tangent = glm::normalize(tangent);
        vertex.bitangent = glm::normalize(bitangent);
      }
      else
      {
        vertex.tangent = glm::vec4(1, 0, 0, 0);
        vertex.bitangent = glm::vec4(0, 1, 0, 0);
      }

      if (mesh->HasTextureCoords(0))
      {
        vertex.tex.x = mesh->mTextureCoords[0][i].x;
        vertex.tex.y = mesh->mTextureCoords[0][i].y;
      }
      else
      {
        vertex.tex.x = 0.0f;
        vertex.tex.y = 0.0f;
      }

      vertices.push_back(vertex);
    }

    for (size_t i = 0; i < mesh->mNumFaces; ++i)
    {
      aiFace face = mesh->mFaces[i];
      indices.push_back(face.mIndices[0]);
      indices.push_back(face.mIndices[1]);
      indices.push_back(face.mIndices[2]);
    }

    auto meshHandle = m_AssetManager->Meshes().Load(vertices, indices);
    m_Scene->AddComponent<MeshComponent>(entity, meshHandle);
    return entity;
  }

  void ModelManager::ProcessMaterial(Model& model, Entity& mesh, const aiMaterial* material)
  {
    auto materialHandle = m_AssetManager->Materials().Create();
    auto& mat = m_AssetManager->Materials().Get(materialHandle);

    aiColor3D diffuse(-1.0f);
    aiColor3D ambient(-1.0f);
    aiColor3D specular(-1.0f);
    aiColor3D emissive(-1.0f);
    aiColor3D transparent(-1.0f);
    aiColor3D reflective(-1.0f);
    float shininess = -1.0f;
    float shininessStrength = -1.0f;
    float opacity = -1.0f;
    float reflectivity = -1.0f;
    float refractiveIndex = -1.0f;
    float roughness = -1.0f;
    float metallic = -1.0f;

    material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
    material->Get(AI_MATKEY_COLOR_AMBIENT, ambient);
    material->Get(AI_MATKEY_COLOR_SPECULAR, specular);
    material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
    material->Get(AI_MATKEY_COLOR_TRANSPARENT, transparent);
    material->Get(AI_MATKEY_COLOR_REFLECTIVE, reflective);
    material->Get(AI_MATKEY_SHININESS, shininess);
    material->Get(AI_MATKEY_SHININESS_STRENGTH, shininessStrength);
    material->Get(AI_MATKEY_OPACITY, opacity);
    material->Get(AI_MATKEY_REFLECTIVITY, reflectivity);
    material->Get(AI_MATKEY_REFRACTI, refractiveIndex);
    material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
    material->Get(AI_MATKEY_METALLIC_FACTOR, metallic);

    int hasSG = 0;
    material->Get(AI_MATKEY_GLTF_PBRSPECULARGLOSSINESS, hasSG);

    std::string baseColorTexture = GetTexturePath(material, aiTextureType_DIFFUSE);
    std::string metallicTexture = GetTexturePath(material, aiTextureType_METALNESS);
    std::string roughnessTexture = GetTexturePath(material, aiTextureType_DIFFUSE_ROUGHNESS);
    std::string ambientOcclusionTexture = GetTexturePath(material, aiTextureType_AMBIENT_OCCLUSION);
    std::string normalTexture = GetTexturePath(material, aiTextureType_NORMALS);
    std::string normalPBR = GetTexturePath(material, aiTextureType_NORMAL_CAMERA);
    std::string emissiveTexture = GetTexturePath(material, aiTextureType_EMISSIVE);
    std::string opacityTexture = GetTexturePath(material, aiTextureType_OPACITY);
    std::string displacementTexture = GetTexturePath(material, aiTextureType_DISPLACEMENT);
    std::string heightTexture = GetTexturePath(material, aiTextureType_HEIGHT);
    std::string specularTexture = GetTexturePath(material, aiTextureType_SPECULAR);
    std::string shininessTexture = GetTexturePath(material, aiTextureType_SHININESS);
    std::string reflectionTexture = GetTexturePath(material, aiTextureType_REFLECTION);
    std::string lightmapTexture = GetTexturePath(material, aiTextureType_LIGHTMAP);
    std::string sheensTexture = GetTexturePath(material, aiTextureType_SHEEN);
    std::string clearcoatTexture = GetTexturePath(material, aiTextureType_CLEARCOAT);
    std::string transmissionTexture = GetTexturePath(material, aiTextureType_TRANSMISSION);

    mat.albedo = glm::vec3(diffuse.r, diffuse.g, diffuse.b);
    mat.emissivity = glm::vec3(emissive.r, emissive.g, emissive.b);
    mat.roughness = roughness;
    mat.specular = specular.r;
    mat.sg = hasSG;

    if (!baseColorTexture.empty())
      mat.baseColorTexture = m_AssetManager->Textures().Load((model.basePath / baseColorTexture).string());
    if (!metallicTexture.empty())
      mat.metallicTexture = m_AssetManager->Textures().Load((model.basePath / metallicTexture).string());
    if (!roughnessTexture.empty())
      mat.roughnessTexture = m_AssetManager->Textures().Load((model.basePath / roughnessTexture).string());
    else if (hasSG && !specularTexture.empty())
      mat.roughnessTexture = m_AssetManager->Textures().Load((model.basePath / roughnessTexture).string());
    if (!specularTexture.empty())
      mat.specularTexture = m_AssetManager->Textures().Load((model.basePath / specularTexture).string());
    if (!emissiveTexture.empty())
      mat.emissiveTexture = m_AssetManager->Textures().Load((model.basePath / emissiveTexture).string());

    if (!normalTexture.empty())
      mat.normalTexture = m_AssetManager->Textures().Load((model.basePath / normalTexture).string());
    else if (!normalPBR.empty())
      mat.normalTexture = m_AssetManager->Textures().Load((model.basePath / normalPBR).string());
    else if (!heightTexture.empty())
      mat.heightTexture = m_AssetManager->Textures().Load((model.basePath / heightTexture).string());

    m_Scene->AddComponent<MaterialComponent>(mesh, materialHandle);
  }

  std::string ModelManager::GetTexturePath(const aiMaterial* mat, aiTextureType type)
  {
    if (mat->GetTextureCount(type) > 0)
    {
      aiString path;
      if (mat->GetTexture(type, 0, &path) == AI_SUCCESS)
      {
        return { path.C_Str() };
      }
    }
    return ""; // No texture found
  }

}
