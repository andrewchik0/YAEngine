#pragma once

#include "Pch.h"
#include "IrradianceBrickLayout.h"
#include "SphericalHarmonics.h"

namespace YAEngine
{
  // The value passes of a brick bake that follow integration, over one coefficient set per
  // layout node. Purely numeric - no Vulkan, no Scene.

  struct IrradianceBrickDilationResult
  {
    uint32_t dilated = 0;
    // Nodes no filled node reaches; their coefficients are zeroed.
    uint32_t unreachable = 0;
    uint32_t waves = 0;
  };

  // Fills every non-stitched node whose filled flag is 0 from its filled neighbours and sets its
  // flag. A node's neighbour along each of the 6 axis directions is the nearest non-stitched node on
  // that axis line: probed every 2^minSpacingIndex keys, walking past keys that hold no node and past
  // stitched nodes, giving up at the first key no brick covers or beyond the brick size of
  // maxSpacingIndex. Two nodes therefore find each other at the same distance. In waves: every
  // unfilled node with at least one filled neighbour takes their inverse distance weighted average,
  // which reproduces a linear field wherever both neighbours along each axis are filled, written
  // only after the whole wave, so the result does not depend on node order. Stitched nodes are
  // neither read nor written. coefficients and filled hold one entry per layout node.
  IrradianceBrickDilationResult DilateIrradianceBrickNodes(const IrradianceBrickLayout& layout,
    std::span<SHL1RGB> coefficients, std::span<uint8_t> filled);

  // Spacing index of the finest brick referencing each node, the spacing its value is interpolated
  // over. outLevels holds one entry per layout node.
  void ComputeIrradianceBrickNodeFinestLevels(const IrradianceBrickLayout& layout, std::span<uint8_t> outLevels);

  // Evaluates the stitch records in their stored order, so a record reading a stitched node reads
  // its resolved value and validity: the stitched node becomes the trilinear interpolation of its
  // source brick's nodes at localCoord, valid when any of those nodes with a non-zero weight is.
  // Every non-stitched node must hold its final value and validity. coefficients and validity hold
  // one entry per layout node.
  void StitchIrradianceBrickNodes(const IrradianceBrickLayout& layout, std::span<SHL1RGB> coefficients,
    std::span<uint8_t> validity);
}
