#pragma once

#include "Pch.h"
#include "Utils/SphericalHarmonics.h"

namespace YAEngine
{
  // On-disk format of a baked irradiance volume (".yaiv"), sibling of ".yacm".
  //
  // Nodes sit on a world axis aligned lattice anchored at the world origin:
  // node (x, y, z) is at latticeOrigin + (x, y, z) * spacing. position, rotation
  // and halfExtents describe the (possibly rotated) influence box and are used
  // for the containment test and the edge fade, not for the node layout.
  //
  // NODE ORDER: x varies fastest, then y, then z -
  //   index = x + y * nodesX + z * nodesX * nodesY
  // The same order is used by the grid generator, the baker and the 3D texture
  // upload, so a node blob can be copied without reshuffling.
  //
  // Coefficients are stored as float32 on disk; the conversion to half happens
  // only when they are uploaded into the 3D atlas.
  struct IrradianceVolumeFileHeader
  {
    char magic[4];          // "YAIV"
    uint32_t version;       // 2
    uint32_t nodesX;
    uint32_t nodesY;
    uint32_t nodesZ;
    float spacing;          // node spacing in meters, uniform on every axis
    float latticeOrigin[3]; // world position of node (0, 0, 0)
    float position[3];      // volume center in world space
    float rotation[4];      // orientation quaternion (x, y, z, w)
    float halfExtents[3];   // world half-extents of the volume box
    uint32_t format;        // 0 = SH L1 RGB, float32, cosine convolved
    uint32_t nodeCount;     // redundant, doubles as an integrity check
  };

  enum class IrradianceVolumeFormat : uint32_t
  {
    SHL1RGBFloat32 = 0
  };

  struct IrradianceVolumeFileDesc
  {
    uint32_t nodesX;
    uint32_t nodesY;
    uint32_t nodesZ;
    float spacing = 1.0f;
    glm::vec3 latticeOrigin { 0.0f };
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 halfExtents { 1.0f };
    const SHL1RGB* coefficients = nullptr;  // nodeCount entries
    const uint8_t* validity = nullptr;      // nodeCount entries, 0 = rejected node
    uint32_t nodeCount = 0;
  };

  struct IrradianceVolumeFileData
  {
    uint32_t nodesX = 0;
    uint32_t nodesY = 0;
    uint32_t nodesZ = 0;
    float spacing = 1.0f;
    glm::vec3 latticeOrigin { 0.0f };
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 halfExtents { 1.0f };
    std::vector<SHL1RGB> coefficients;
    std::vector<uint8_t> validity;
  };

  class IrradianceVolumeFile
  {
  public:

    static bool Save(const std::string& path, const IrradianceVolumeFileDesc& desc);
    static bool Load(const std::string& path, IrradianceVolumeFileData& outData);

    static size_t GetCoefficientBlobSize(uint32_t nodeCount);
    static size_t GetValidityBlobSize(uint32_t nodeCount);
    static size_t GetTotalSize(uint32_t nodeCount);

    static uint32_t GetNodeIndex(uint32_t x, uint32_t y, uint32_t z,
      uint32_t nodesX, uint32_t nodesY);
  };
}
