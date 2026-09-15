#pragma once

#include "Pch.h"
#include "Utils/IrradianceBrickLayout.h"
#include "Utils/SphericalHarmonics.h"

namespace YAEngine
{
  // On-disk format of a baked irradiance volume (".yaiv"), sibling of ".yacm": the sparse brick
  // layout of Utils/IrradianceBrickLayout.h with one set of coefficients per unique node.
  //
  // FILE LAYOUT, little endian, no padding anywhere:
  //   IrradianceVolumeFileHeader                     100 bytes
  //   bricks            brickCount x 16 bytes        originKey int32 x3, spacingIndex uint32
  //   brick node index  brickCount x 125 x uint32    node order x fastest, then y, then z
  //   coefficients      nodeCount x 12 x half float  r(l0, l1x, l1y, l1z), g(...), b(...)
  //   validity          nodeCount x uint8            0 = node rejected by the bake
  //   indirection       cellCount x uint32           brick index, or UINT32_MAX where no brick is
  // cellCount = indirectionDims.x * y * z, cells x fastest, then y, then z.
  //
  // Coefficients are cosine convolved SH L1 (irradiance(n) = l0 + dot(l1, n)). Rejected nodes -
  // buried, too close to geometry for their spacing, or enclosed by it - are dilated from their
  // neighbours, or left zero when no valid node reaches them, and stitched ones interpolated from
  // their coarser neighbour. Validity is 0 for a rejected node and for a stitched node
  // interpolating rejected nodes only; the bake saves no volume without a valid node.
  //
  // Bricks and node keys sit on the world lattice; position, rotation and halfExtents describe
  // the (possibly rotated) influence box for the containment test, edgeFade the width of the fade
  // at its faces, all as the volume was when it was baked.
  struct IrradianceVolumeFileHeader
  {
    char magic[4];                   // "YAIV"
    uint32_t version;                // 4
    uint32_t format;                 // IrradianceVolumeFormat
    float position[3];               // box center in world space
    float rotation[4];               // box orientation quaternion (x, y, z, w)
    float halfExtents[3];            // box half extents in meters
    float edgeFade;                  // edge fade width in meters
    uint32_t minSpacingIndex;        // into IRRADIANCE_SPACINGS
    uint32_t maxSpacingIndex;
    uint32_t brickCount;
    uint32_t nodeCount;
    int32_t indirectionOriginKey[3]; // key of the first cell's minimum corner
    uint32_t indirectionDims[3];     // cells per axis
    uint32_t indirectionCellKeys;    // cell size in keys, the brick size of minSpacingIndex
  };

  enum class IrradianceVolumeFormat : uint32_t
  {
    SHL1RGBHalf = 1
  };

  // One node's SH L1 RGB as the 12 half floats the file and the GPU store: r(l0, l1x, l1y, l1z),
  // then g, then b.
  struct SHL1RGBHalf
  {
    std::array<uint16_t, 12> halves {};
  };

  SHL1RGBHalf PackSHL1RGBHalf(const SHL1RGB& sh);
  SHL1RGB UnpackSHL1RGBHalf(const SHL1RGBHalf& packed);

  struct IrradianceVolumeFileData
  {
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 halfExtents { 1.0f };
    // Meters, finite and positive.
    float edgeFade = 1.0f;
    uint32_t minSpacingIndex = 0;
    uint32_t maxSpacingIndex = 0;
    glm::ivec3 indirectionOriginKey { 0 };
    glm::uvec3 indirectionDims { 0 };
    uint32_t indirectionCellKeys = 0;

    std::vector<IrradianceBrick> bricks;
    // IRRADIANCE_BRICK_NODE_COUNT per brick.
    std::vector<uint32_t> brickNodeIndices;
    // One entry per unique node, parallel to validity.
    std::vector<SHL1RGBHalf> coefficients;
    std::vector<uint8_t> validity;
    std::vector<uint32_t> indirection;

    uint32_t GetNodeCount() const { return uint32_t(validity.size()); }
  };

  class IrradianceVolumeFile
  {
  public:

    // Refuses data that would not load back. Written to path + ".tmp" and moved over path, retried
    // for a while when another process holds path open; if the move still fails, the complete file
    // stays at path + ".tmp" and the error names it.
    static bool Save(const std::string& path, const IrradianceVolumeFileData& data);
    // A failed load leaves outData as it was.
    static bool Load(const std::string& path, IrradianceVolumeFileData& outData);

    // Everything the loader checks: non-zero counts, spacing indices, a finite positive edge fade,
    // blob sizes against the counts, brick node indices and indirection cells in range, and finite
    // coefficients. False with a readable description of the first failure.
    static bool Validate(const IrradianceVolumeFileData& data, std::string& outFailure);
  };
}
