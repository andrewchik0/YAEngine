#pragma once
#include <filesystem>

#include "AssetManagerBase.h"

#include "Scene/Scene.h"

enum aiTextureType : int;
struct aiNode;
struct aiScene;
struct aiMaterial;
struct aiMesh;

class Scene;
class AssetManager;

namespace YAEngine
{
  struct Model
  {
    Entity rootEntity;

    std::vector<glm::mat4> modelMatrices;
    uint32_t offset = 0;

  private:
    std::filesystem::path basePath;

    friend class Render;
    friend class ModelManager;
  };

  using ModelHandle = AssetHandle;

  class ModelManager : public AssetManagerBase<Model>
  {
  public:

    ModelManager() = default;
    ModelManager(Scene* scene, AssetManager* assetManager)
      : m_Scene(scene), m_AssetManager(assetManager)
    {
    }

    ModelHandle Load(const std::string& path);
    ModelHandle LoadInstanced(const std::string& path, const std::vector<glm::mat4>& instances);

    void Destroy(Model& model);
    void DestroyAll();

  private:

    Scene* m_Scene;
    AssetManager* m_AssetManager;

    Entity ProcessNode(Model& model, Entity& parent, aiNode* node, const aiScene* scene);
    Entity ProcessMesh(Model& model, aiMesh* mesh, const aiScene* scene);
    void ProcessMaterial(Model& model, Entity& mesh, const aiMaterial* material);
    std::string GetTexturePath(const aiMaterial* mat, aiTextureType type);

    void ComputeMeshBB(const aiMesh* mesh, glm::vec3& outMin, glm::vec3& outMax);

    void TraverseInstanceData(Entity entity, std::vector<glm::mat4>* instanceData, uint32_t offset);
  };
}
