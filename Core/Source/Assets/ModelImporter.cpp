#include "ModelImporter.h"

#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/pbrmaterial.h>

#include "Log.h"

namespace YAEngine
{
  ModelDescription ModelImporter::Import(const std::string& path, bool combinedTextures)
  {
    Assimp::Importer importer;
    const aiScene* scene =
      importer.ReadFile(path,
                        aiProcess_Triangulate | aiProcess_GenUVCoords | aiProcess_FlipUVs |
                        aiProcess_GenNormals);

    ModelDescription desc;

    if (scene == nullptr)
    {
      YA_LOG_ERROR("Assets", "Failed to load model '%s': %s", path.c_str(), importer.GetErrorString());
      return desc;
    }

    desc.basePath = std::filesystem::path(path).parent_path();
    desc.root.name = std::filesystem::path(path).filename().string();

    ProcessNode(desc, desc.root, scene->mRootNode, scene, combinedTextures);

    return desc;
  }

  void ModelImporter::ProcessNode(ModelDescription& desc, NodeDescription& parentNode, aiNode* node, const aiScene* scene, bool combinedTextures)
  {
    NodeDescription nodeDesc;
    nodeDesc.name = node->mName.C_Str();

    aiVector3D pos, scale;
    aiQuaternion rot;
    node->mTransformation.Decompose(scale, rot, pos);

    nodeDesc.position = { pos.x, pos.y, pos.z };
    nodeDesc.rotation = { rot.w, rot.x, rot.y, rot.z };
    nodeDesc.scale    = { scale.x, scale.y, scale.z };

    for (size_t i = 0; i < node->mNumMeshes; ++i)
    {
      aiMesh* ai_mesh = scene->mMeshes[node->mMeshes[i]];
      ProcessMesh(desc, nodeDesc, ai_mesh, scene, combinedTextures);
    }

    for (size_t i = 0; i < node->mNumChildren; ++i)
    {
      ProcessNode(desc, nodeDesc, node->mChildren[i], scene, combinedTextures);
    }

    parentNode.children.push_back(std::move(nodeDesc));
  }

  void ModelImporter::ProcessMesh(ModelDescription& desc, NodeDescription& parentNode, aiMesh* mesh, const aiScene* scene, bool combinedTextures)
  {
    MeshDescription meshDesc;

    meshDesc.vertices.reserve(mesh->mNumVertices);
    meshDesc.indices.reserve(mesh->mNumFaces * 3);

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
        glm::vec3 T = glm::normalize(glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z));
        glm::vec3 B = glm::normalize(glm::vec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z));
        glm::vec3 N = glm::normalize(glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z));

        float w = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;

        vertex.tangent = glm::vec4(T, w);
      }
      else
      {
        vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
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

      meshDesc.vertices.push_back(vertex);
    }

    for (size_t i = 0; i < mesh->mNumFaces; ++i)
    {
      aiFace face = mesh->mFaces[i];
      meshDesc.indices.push_back(face.mIndices[0]);
      meshDesc.indices.push_back(face.mIndices[1]);
      meshDesc.indices.push_back(face.mIndices[2]);
    }

    ComputeMeshBB(mesh, meshDesc.minBB, meshDesc.maxBB);

    uint32_t meshIndex = static_cast<uint32_t>(desc.meshes.size());
    desc.meshes.push_back(std::move(meshDesc));

    aiMaterial* aiMat = scene->mMaterials[mesh->mMaterialIndex];
    uint32_t materialIndex = ProcessMaterial(desc, aiMat, combinedTextures);

    NodeDescription meshNode;
    meshNode.name = mesh->mName.C_Str();
    meshNode.meshIndex = meshIndex;
    meshNode.materialIndex = materialIndex;

    parentNode.children.push_back(std::move(meshNode));
  }

  uint32_t ModelImporter::ProcessMaterial(ModelDescription& desc, const aiMaterial* material, bool combinedTextures)
  {
    MaterialDescription matDesc;

    aiString matName;
    material->Get(AI_MATKEY_NAME, matName);
    matDesc.name = matName.C_Str();

    aiColor3D diffuse(-1.0f);
    aiColor3D specular(-1.0f);
    aiColor3D emissive(-1.0f);
    float roughness = -1.0f;
    float metallic = -1.0f;

    material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
    material->Get(AI_MATKEY_COLOR_SPECULAR, specular);
    material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
    material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
    material->Get(AI_MATKEY_METALLIC_FACTOR, metallic);

    int hasSG = 0;
    material->Get(AI_MATKEY_GLTF_PBRSPECULARGLOSSINESS, hasSG);

    matDesc.albedo = glm::vec3(diffuse.r, diffuse.g, diffuse.b);
    matDesc.emissivity = glm::vec3(emissive.r, emissive.g, emissive.b);
    matDesc.roughness = roughness;
    matDesc.specular = specular.r;
    matDesc.sg = hasSG;
    matDesc.combinedTextures = combinedTextures;

    std::string baseColorTexture = GetTexturePath(material, aiTextureType_DIFFUSE);
    std::string metallicTexture = GetTexturePath(material, aiTextureType_METALNESS);
    std::string roughnessTexture = GetTexturePath(material, aiTextureType_DIFFUSE_ROUGHNESS);
    std::string normalTexture = GetTexturePath(material, aiTextureType_NORMALS);
    std::string normalPBR = GetTexturePath(material, aiTextureType_NORMAL_CAMERA);
    std::string emissiveTexture = GetTexturePath(material, aiTextureType_EMISSIVE);
    std::string heightTexture = GetTexturePath(material, aiTextureType_HEIGHT);
    std::string specularTexture = GetTexturePath(material, aiTextureType_SPECULAR);

    if (!baseColorTexture.empty())
      matDesc.baseColorTexture = (desc.basePath / baseColorTexture).string();
    if (!metallicTexture.empty())
      matDesc.metallicTexture = (desc.basePath / metallicTexture).string();
    if (!roughnessTexture.empty())
      matDesc.roughnessTexture = (desc.basePath / roughnessTexture).string();
    else if (hasSG && !specularTexture.empty())
      matDesc.roughnessTexture = (desc.basePath / specularTexture).string();
    if (!specularTexture.empty())
      matDesc.specularTexture = (desc.basePath / specularTexture).string();
    if (!emissiveTexture.empty())
      matDesc.emissiveTexture = (desc.basePath / emissiveTexture).string();

    if (!normalTexture.empty())
      matDesc.normalTexture = (desc.basePath / normalTexture).string();
    else if (!normalPBR.empty())
      matDesc.normalTexture = (desc.basePath / normalPBR).string();
    else if (!heightTexture.empty())
      matDesc.heightTexture = (desc.basePath / heightTexture).string();

    uint32_t index = static_cast<uint32_t>(desc.materials.size());
    desc.materials.push_back(std::move(matDesc));
    return index;
  }

  std::string ModelImporter::GetTexturePath(const aiMaterial* mat, aiTextureType type)
  {
    if (mat->GetTextureCount(type) > 0)
    {
      aiString path;
      if (mat->GetTexture(type, 0, &path) == AI_SUCCESS)
      {
        return { path.C_Str() };
      }
    }
    return "";
  }

  void ModelImporter::ComputeMeshBB(const aiMesh* mesh, glm::vec3& outMin, glm::vec3& outMax)
  {
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
      aiVector3D v = mesh->mVertices[i];

      if (v.x < outMin.x) outMin.x = v.x;
      if (v.y < outMin.y) outMin.y = v.y;
      if (v.z < outMin.z) outMin.z = v.z;

      if (v.x > outMax.x) outMax.x = v.x;
      if (v.y > outMax.y) outMax.y = v.y;
      if (v.z > outMax.z) outMax.z = v.z;
    }
  }
}
