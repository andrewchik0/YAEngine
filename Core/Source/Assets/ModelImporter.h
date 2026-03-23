#pragma once

#include "ModelDescription.h"

enum aiTextureType : int;
struct aiNode;
struct aiScene;
struct aiMaterial;
struct aiMesh;

namespace YAEngine
{
  class ModelImporter
  {
  public:

    static ModelDescription Import(const std::string& path, bool combinedTextures);

  private:

    static void ProcessNode(ModelDescription& desc, NodeDescription& parentNode, aiNode* node, const aiScene* scene, bool combinedTextures);
    static void ProcessMesh(ModelDescription& desc, NodeDescription& parentNode, aiMesh* mesh, const aiScene* scene, bool combinedTextures);
    static uint32_t ProcessMaterial(ModelDescription& desc, const aiMaterial* material, bool combinedTextures);
    static std::string GetTexturePath(const aiMaterial* mat, aiTextureType type);
    static void ComputeMeshBB(const aiMesh* mesh, glm::vec3& outMin, glm::vec3& outMax);
  };
}
