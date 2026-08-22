#include "ModelImporter.h"

#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/pbrmaterial.h>

#include "Utils/Log.h"

namespace YAEngine
{
  ModelDescription ModelImporter::Import(const std::string& path, bool combinedTextures)
  {
    Assimp::Importer importer;
    const aiScene* scene =
      importer.ReadFile(path,
                        aiProcess_Triangulate | aiProcess_GenUVCoords | aiProcess_FlipUVs |
                        aiProcess_GenNormals | aiProcess_CalcTangentSpace);

    ModelDescription desc;

    if (scene == nullptr)
    {
      YA_LOG_ERROR("Assets", "Failed to load model '%s': %s", path.c_str(), importer.GetErrorString());
      return desc;
    }

    desc.basePath = std::filesystem::path(path).parent_path();
    desc.root.name = std::filesystem::path(path).filename().string();
    desc.sourcePath = path;

    // Meshes share materials, so process each one once up front. This keeps
    // desc.materials indexed exactly like scene->mMaterials.
    desc.materials.reserve(scene->mNumMaterials);
    for (uint32_t i = 0; i < scene->mNumMaterials; i++)
      ProcessMaterial(desc, scene->mMaterials[i], scene, combinedTextures);

    ProcessNode(desc, desc.root, scene->mRootNode, scene);

    return desc;
  }

  void ModelImporter::ProcessNode(ModelDescription& desc, NodeDescription& parentNode, aiNode* node, const aiScene* scene)
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
      ProcessMesh(desc, nodeDesc, ai_mesh);
    }

    for (size_t i = 0; i < node->mNumChildren; ++i)
    {
      ProcessNode(desc, nodeDesc, node->mChildren[i], scene);
    }

    parentNode.children.push_back(std::move(nodeDesc));
  }

  void ModelImporter::ProcessMesh(ModelDescription& desc, NodeDescription& parentNode, aiMesh* mesh)
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
      const auto& face = mesh->mFaces[i];
      meshDesc.indices.push_back(face.mIndices[0]);
      meshDesc.indices.push_back(face.mIndices[1]);
      meshDesc.indices.push_back(face.mIndices[2]);
    }

    ComputeMeshBB(mesh, meshDesc.minBB, meshDesc.maxBB);

    uint32_t meshIndex = static_cast<uint32_t>(desc.meshes.size());
    desc.meshes.push_back(std::move(meshDesc));

    NodeDescription meshNode;
    meshNode.name = mesh->mName.C_Str();
    meshNode.meshIndex = meshIndex;
    meshNode.materialIndex = mesh->mMaterialIndex;

    parentNode.children.push_back(std::move(meshNode));
  }

  uint32_t ModelImporter::ProcessMaterial(ModelDescription& desc, const aiMaterial* material, const aiScene* scene, bool combinedTextures)
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
    matDesc.roughness = (roughness >= 0.0f) ? roughness : 1.0f;
    matDesc.metallic = (metallic >= 0.0f) ? metallic : 0.0f;
    matDesc.roughnessFactor = (roughness >= 0.0f) ? roughness : 1.0f;
    matDesc.metallicFactor = (metallic >= 0.0f) ? metallic : 1.0f;
    matDesc.specular = specular.r;
    matDesc.sg = hasSG;
    matDesc.combinedTextures = combinedTextures;

    int twoSided = 0;
    material->Get(AI_MATKEY_TWOSIDED, twoSided);
    matDesc.doubleSided = (twoSided != 0);

    aiString alphaMode;
    if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
    {
      matDesc.transparent = (std::string(alphaMode.C_Str()) == "BLEND");
    }

    std::string baseColorTexture = ResolveTexturePath(desc, scene, material, aiTextureType_DIFFUSE);
    std::string metallicTexture = ResolveTexturePath(desc, scene, material, aiTextureType_METALNESS);
    std::string roughnessTexture = ResolveTexturePath(desc, scene, material, aiTextureType_DIFFUSE_ROUGHNESS);
    std::string normalTexture = ResolveTexturePath(desc, scene, material, aiTextureType_NORMALS);
    std::string normalPBR = ResolveTexturePath(desc, scene, material, aiTextureType_NORMAL_CAMERA);
    std::string emissiveTexture = ResolveTexturePath(desc, scene, material, aiTextureType_EMISSIVE);
    std::string heightTexture = ResolveTexturePath(desc, scene, material, aiTextureType_HEIGHT);
    std::string specularTexture = ResolveTexturePath(desc, scene, material, aiTextureType_SPECULAR);

    if (!baseColorTexture.empty())
      matDesc.baseColorTexture = baseColorTexture;
    if (!metallicTexture.empty())
      matDesc.metallicTexture = metallicTexture;
    if (!roughnessTexture.empty())
      matDesc.roughnessTexture = roughnessTexture;
    else if (hasSG && !specularTexture.empty())
      matDesc.roughnessTexture = specularTexture;
    if (!specularTexture.empty())
      matDesc.specularTexture = specularTexture;
    if (!emissiveTexture.empty())
      matDesc.emissiveTexture = emissiveTexture;

    if (!normalTexture.empty())
      matDesc.normalTexture = normalTexture;
    else if (!normalPBR.empty())
      matDesc.normalTexture = normalPBR;
    else if (!heightTexture.empty())
      matDesc.heightTexture = heightTexture;

    // FBX has no metal-rough slots at all, so exporters smuggle a packed ORM map
    // through SpecularColor and document the channel layout outside the file. That
    // cannot be detected, only declared - hence the caller-supplied flag.
    if (combinedTextures && matDesc.metallicTexture.empty() && matDesc.roughnessTexture.empty()
      && !specularTexture.empty())
    {
      matDesc.metallicTexture = specularTexture;
      matDesc.roughnessTexture = specularTexture;
    }

    // glTF packs both values into one texture (G = roughness, B = metallic) and assimp
    // reports it in the metalness and roughness slots alike - identical paths mean packed.
    if (!matDesc.metallicTexture.empty() && matDesc.metallicTexture == matDesc.roughnessTexture)
      matDesc.combinedTextures = true;

    // A metallic map with no factor in the file still needs a neutral multiplier
    if (metallic < 0.0f && !matDesc.metallicTexture.empty())
      matDesc.metallic = 1.0f;

    uint32_t index = static_cast<uint32_t>(desc.materials.size());
    desc.materials.push_back(std::move(matDesc));
    return index;
  }

  std::string ModelImporter::ResolveTexturePath(ModelDescription& desc, const aiScene* scene,
    const aiMaterial* mat, aiTextureType type)
  {
    if (mat->GetTextureCount(type) == 0)
      return "";

    aiString path;
    if (mat->GetTexture(type, 0, &path) != AI_SUCCESS)
      return "";

    // Resolves both "*N" GLB references and FBX embedded media referenced by file name
    if (const aiTexture* embedded = scene->GetEmbeddedTexture(path.C_Str()))
      return StoreEmbeddedTexture(desc, embedded, path.C_Str());

    return (desc.basePath / path.C_Str()).string();
  }

  std::string ModelImporter::StoreEmbeddedTexture(ModelDescription& desc, const aiTexture* texture,
    const std::string& refName)
  {
    std::string key = desc.sourcePath + "*" + refName;
    if (desc.embeddedTextures.contains(key))
      return key;

    EmbeddedTexture entry;

    if (texture->mHeight == 0)
    {
      // Compressed payload - mWidth holds the byte count, decoded by stb later
      auto* bytes = reinterpret_cast<const uint8_t*>(texture->pcData);
      entry.data.assign(bytes, bytes + texture->mWidth);
    }
    else
    {
      entry.width = texture->mWidth;
      entry.height = texture->mHeight;

      size_t texelCount = size_t(texture->mWidth) * texture->mHeight;
      entry.data.resize(texelCount * 4);
      for (size_t i = 0; i < texelCount; i++)
      {
        const aiTexel& texel = texture->pcData[i];
        entry.data[i * 4 + 0] = texel.r;
        entry.data[i * 4 + 1] = texel.g;
        entry.data[i * 4 + 2] = texel.b;
        entry.data[i * 4 + 3] = texel.a;
      }
    }

    desc.embeddedTextures.emplace(key, std::move(entry));
    return key;
  }

  void ModelImporter::ComputeMeshBB(const aiMesh* mesh, glm::vec3& outMin, glm::vec3& outMax)
  {
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
      const auto& v = mesh->mVertices[i];

      if (v.x < outMin.x) outMin.x = v.x;
      if (v.y < outMin.y) outMin.y = v.y;
      if (v.z < outMin.z) outMin.z = v.z;

      if (v.x > outMax.x) outMax.x = v.x;
      if (v.y > outMax.y) outMax.y = v.y;
      if (v.z > outMax.z) outMax.z = v.z;
    }
  }
}
