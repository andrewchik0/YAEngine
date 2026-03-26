#pragma once

#include "Pch.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Render/VulkanVertexBuffer.h"

namespace YAEngine
{
  struct MeshDescription
  {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec3 minBB;
    glm::vec3 maxBB;
  };

  struct MaterialDescription
  {
    glm::vec3 albedo {};
    glm::vec3 emissivity {};
    float roughness = -1.0f;
    float metallic = -1.0f;
    float specular = 0.0f;
    bool sg = false;
    bool hasAlpha = false;
    bool combinedTextures = false;

    std::string baseColorTexture;
    std::string metallicTexture;
    std::string roughnessTexture;
    std::string specularTexture;
    std::string emissiveTexture;
    std::string normalTexture;
    std::string heightTexture;
  };

  struct NodeDescription
  {
    std::string name;
    glm::vec3 position { 0 };
    glm::quat rotation { 1, 0, 0, 0 };
    glm::vec3 scale { 1 };

    std::optional<uint32_t> meshIndex;
    std::optional<uint32_t> materialIndex;

    std::vector<NodeDescription> children;
  };

  struct ModelDescription
  {
    NodeDescription root;
    std::vector<MeshDescription> meshes;
    std::vector<MaterialDescription> materials;
    std::filesystem::path basePath;
  };
}
