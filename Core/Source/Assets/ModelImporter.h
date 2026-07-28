#pragma once

#include "ModelDescription.h"

enum aiTextureType : int;
struct aiNode;
struct aiScene;
struct aiMaterial;
struct aiMesh;
struct aiTexture;

namespace YAEngine
{
  class ModelImporter
  {
  public:

    static ModelDescription Import(const std::string& path, bool combinedTextures);

  private:

    static void ProcessNode(ModelDescription& desc, NodeDescription& parentNode, aiNode* node, const aiScene* scene);
    static void ProcessMesh(ModelDescription& desc, NodeDescription& parentNode, aiMesh* mesh);
    static uint32_t ProcessMaterial(ModelDescription& desc, const aiMaterial* material, const aiScene* scene, bool combinedTextures);
    static std::string ResolveTexturePath(ModelDescription& desc, const aiScene* scene, const aiMaterial* mat, aiTextureType type);
    static std::string StoreEmbeddedTexture(ModelDescription& desc, const aiTexture* texture, const std::string& refName);
    static void ComputeMeshBB(const aiMesh* mesh, glm::vec3& outMin, glm::vec3& outMax);
  };
}
