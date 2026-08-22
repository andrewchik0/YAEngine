// Conservative Hi-Z cell-crossing ray traversal in normalized screen space.
// Requires common.glsl and utils.glsl to be included before this file.
//
// The ray is a segment in (uv, ndcDepth) parameterised by t in [0,1]. Perspective projection
// maps a 3D line to a line in NDC and keeps NDC depth affine along it, so interpolating all
// three components with a single t reproduces the view-space ray exactly.
//
// The traversal leaves a cell only after proving the ray runs in front of that cell's minimum
// depth for its whole span inside it, and it advances exactly to the cell boundary. No cell is
// skipped at any mip level, which is what makes coarse mips a pure speedup instead of a
// quality tradeoff.
//
// Cells are indexed as basePixel >> mip, matching how the reduction actually aggregates
// texels. At a non-power-of-two resolution this differs from uv * mipSize: mip texel (i, j)
// holds the minimum over base region [i << mip, (i + 1) << mip), not over an equal fraction
// of the screen, and the mismatch grows with the distance from the origin.

// Nudge past a cell boundary, as a fraction of the cell size at the current mip. Too small and
// rounding puts the ray back in the cell it just left; too large and a thin cell is skipped at
// grazing angles.
const float HIZ_CROSS_EPSILON = 0.015625;
const float HIZ_T_INFINITY = 1e30;

struct HiZRay
{
  vec3 start;
  vec3 delta;
  vec2 startPx;
  vec2 deltaPx;
  vec2 invDeltaPx;
  ivec2 boundaryStep;
  vec2 crossOffset;
  bvec2 movesXY;
  bvec2 positiveXY;
};

struct HiZTraceResult
{
  bool hit;
  float t;
  vec2 uv;
};

HiZRay hiZMakeRay(vec3 rayStart, vec3 rayEnd, ivec2 baseSize)
{
  HiZRay ray;
  ray.start = rayStart;
  ray.delta = rayEnd - rayStart;
  ray.startPx = rayStart.xy * vec2(baseSize);
  ray.deltaPx = ray.delta.xy * vec2(baseSize);
  ray.movesXY = greaterThan(abs(ray.deltaPx), vec2(1e-6));
  ray.invDeltaPx = 1.0 / mix(vec2(1.0), ray.deltaPx, ray.movesXY);

  ray.positiveXY = greaterThan(ray.deltaPx, vec2(0.0));
  ray.boundaryStep = ivec2(ray.positiveXY);
  ray.crossOffset = mix(vec2(-HIZ_CROSS_EPSILON), vec2(HIZ_CROSS_EPSILON), ray.positiveXY);
  return ray;
}

// t at which the ray leaves the cell holding pxAtT, offset just past the shared edge.
float hiZCellExit(HiZRay ray, vec2 pxAtT, int mip, ivec2 baseSize, out ivec2 cell)
{
  ivec2 mipSize = max(ivec2(1), baseSize >> mip);
  ivec2 rawCell = ivec2(pxAtT) >> mip;
  cell = min(rawCell, mipSize - 1);

  vec2 boundaryPx = vec2((cell + ray.boundaryStep) << mip);
  // The last cell of an NPOT mip also covers the remainder band the shift arithmetic leaves
  // past mipSize << mip (the reduction folds those texels into it), so its exit boundary going
  // forward is the image edge - the nominal boundary would sit behind the ray and stall it.
  bvec2 pastEdge = greaterThan(rawCell, mipSize - 1);
  boundaryPx = mix(boundaryPx, vec2(baseSize),
                   bvec2(pastEdge.x && ray.positiveXY.x, pastEdge.y && ray.positiveXY.y));
  boundaryPx += ray.crossOffset * float(1 << mip);

  vec2 tAxis = (boundaryPx - ray.startPx) * ray.invDeltaPx;
  return min(ray.movesXY.x ? tAxis.x : HIZ_T_INFINITY,
             ray.movesXY.y ? tAxis.y : HIZ_T_INFINITY);
}

HiZTraceResult hiZTrace(sampler2D hiZ, ivec2 baseSize, int maxMip, int maxIterations,
                        vec3 rayStart, vec3 rayEnd, float maxThickness, int maxConsecutiveRejects)
{
  HiZTraceResult result;
  result.hit = false;
  result.t = 0.0;
  result.uv = rayStart.xy;

  HiZRay ray = hiZMakeRay(rayStart, rayEnd, baseSize);
  bool depthIncreases = ray.delta.z > 1e-8;
  float invDeltaZ = depthIncreases ? 1.0 / ray.delta.z : 0.0;

  // The originating pixel's own cell always holds the surface the ray just left, so start past
  // it rather than rejecting the self intersection by a depth threshold afterwards.
  ivec2 startCell;
  float t = hiZCellExit(ray, ray.startPx, 0, baseSize, startCell);

  int mip = 0;
  int consecutiveRejects = 0;

  for (int i = 0; i < maxIterations; i++)
  {
    if (t >= 1.0)
      break;

    vec2 pxAtT = ray.startPx + ray.deltaPx * t;
    if (any(lessThan(pxAtT, vec2(0.0))) || any(greaterThanEqual(pxAtT, vec2(baseSize))))
      break;

    ivec2 cell;
    float tCross = hiZCellExit(ray, pxAtT, mip, baseSize, cell);

    float cellMinDepth = texelFetch(hiZ, cell, mip).r;
    float rayDepth = ray.start.z + ray.delta.z * t;

    // Entering the cell already behind its closest surface, or crossing that depth plane
    // before leaving the cell, both mean an intersection may live inside it.
    bool behind = rayDepth > cellMinDepth;
    float tPlane = depthIncreases ? (cellMinDepth - ray.start.z) * invDeltaZ : 0.0;
    bool crossesPlane = depthIncreases && tPlane < tCross;

    if (behind || crossesPlane)
    {
      float tCandidate = behind ? t : tPlane;

      if (mip > 0)
      {
        mip--;
        t = tCandidate;
        continue;
      }

      float rayLinearDepth = linearizeDepth(ray.start.z + ray.delta.z * tCandidate);
      float sampleLinearDepth = linearizeDepth(cellMinDepth);

      // A depth buffer carries no thickness, so a hit far behind the stored surface is a ray
      // that passed under geometry. Step past this cell and keep going, staying at mip 0 - the
      // parent holds the same geometry and would send the ray straight back down.
      if (rayLinearDepth - sampleLinearDepth > maxThickness)
      {
        // Behind geometry the traversal degenerates to one cell per iteration (coarser mips
        // would need a max pyramid to skip safely), so a long crawl is cut as a miss.
        consecutiveRejects++;
        if (consecutiveRejects >= maxConsecutiveRejects)
          break;
        t = tCross;
        continue;
      }

      result.hit = true;
      result.t = tCandidate;
      result.uv = ray.start.xy + ray.delta.xy * tCandidate;
      return result;
    }

    consecutiveRejects = 0;
    t = tCross;

    // Climbing is safe only once the ray has left the parent cell too, otherwise the parent
    // sends it back down on the next iteration and the traversal oscillates in place.
    int parentMip = min(mip + 1, maxMip);
    if (parentMip != mip)
    {
      ivec2 parentSize = max(ivec2(1), baseSize >> parentMip);
      ivec2 parentBefore = min(ivec2(pxAtT) >> parentMip, parentSize - 1);
      ivec2 parentAfter = min(ivec2(ray.startPx + ray.deltaPx * t) >> parentMip, parentSize - 1);
      if (parentBefore != parentAfter)
        mip = parentMip;
    }
  }

  return result;
}
