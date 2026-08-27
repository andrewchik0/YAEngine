#ifndef SSGI_BITMASK_GLSL
#define SSGI_BITMASK_GLSL

// Visibility bitmask for the GTAO slice loop, after Therrien et al. 2023,
// "Screen-Space Indirect Lighting with Visibility Bitmask".
//
// Requires gtao_common.glsl (GTAO_PI_HALF, gtaoFastAcos) to be included first.
//
// One 32-bit mask per slice covers the normal-oriented half-arc
// [n - PI/2, n + PI/2], where angles are measured from the view vector inside
// the slice plane and n is the angle of the projected normal. The key deviation
// from the paper: sectors are NOT uniform in angle. Bit boundaries follow the
// cumulative cosine-weighted arc measure of GTAO,
//
//   d(mu) = sin|theta| * cos(theta - n) * d(theta),
//
// so every sector carries exactly Total/32 of the measure, where
// Total = n*sin(n) + cos(n) is the closed-form full-arc value - the same number
// the horizon-search integral produces for an empty scene. Consequences:
//   - visibility = (1 - bitCount(mask)/32) * Total, a popcount instead of a
//     per-sector transcendental sum;
//   - an empty mask reproduces the horizon-search result EXACTLY, which is the
//     acceptance invariant of the whole SSGI plan;
//   - the newly-set-bit count of a sample is directly its cosine-weighted share
//     of the irradiance integral, so radiance needs no extra weighting.

const float SSGI_SECTOR_COUNT = 32.0;

// Cumulative arc measure from theta = 0, monotonic over [n - PI/2, n + PI/2].
// Piecewise antiderivative of sin|theta| * cos(theta - n); both halves meet at 0.
float ssgiArcCDF(float theta, float n, float sinN, float cosN)
{
  float f = 0.25 * (2.0 * theta * sinN - cos(2.0 * theta - n) + cosN);
  return theta >= 0.0 ? f : -f;
}

// Bits covered by the measure range [u0, u1], both in [0, 1]. Round-to-nearest
// on both ends counts a sector when the interval covers most of it, matching the
// reference implementation of the paper.
uint ssgiBitsForRange(float u0, float u1)
{
  uint a = uint(clamp(round(u0 * SSGI_SECTOR_COUNT), 0.0, SSGI_SECTOR_COUNT));
  uint b = uint(clamp(round(u1 * SSGI_SECTOR_COUNT), 0.0, SSGI_SECTOR_COUNT));
  uint count = b - a;
  if (count == 0u || a >= 32u)
    return 0u;

  // 1u << 32 is undefined, so the full mask is special-cased.
  uint mask = count >= 32u ? 0xFFFFFFFFu : ((1u << count) - 1u);
  return mask << a;
}

// Angular interval one sample occludes, as a bit mask. thetaFront is the front
// surface angle, thetaBack the same surface pushed away from the camera by the
// thickness parameter. The falloff weight w shrinks the interval towards the
// front angle so a sample fades out continuously at the effect radius instead
// of popping - the bitmask analog of XeGTAO lerping shc to the low horizon.
// outInterval receives the shrunk, clamped interval for the bent normal math.
uint ssgiBitsForAngles(float thetaFront, float thetaBack, float w,
  float n, float sinN, float cosN, float cdfMin, float invTotal, out vec2 outInterval)
{
  float shrunkBack = thetaFront + (thetaBack - thetaFront) * w;
  float lo = clamp(min(thetaFront, shrunkBack), n - GTAO_PI_HALF, n + GTAO_PI_HALF);
  float hi = clamp(max(thetaFront, shrunkBack), n - GTAO_PI_HALF, n + GTAO_PI_HALF);
  outInterval = vec2(lo, hi);

  float u0 = (ssgiArcCDF(lo, n, sinN, cosN) - cdfMin) * invTotal;
  float u1 = (ssgiArcCDF(hi, n, sinN, cosN) - cdfMin) * invTotal;
  return ssgiBitsForRange(u0, u1);
}

// Fraction of the slice measure a mask occludes.
float ssgiMaskFraction(uint mask)
{
  return float(bitCount(mask)) * (1.0 / SSGI_SECTOR_COUNT);
}

// Direction at angle theta inside the slice plane: theta = 0 is the view
// vector, positive theta rotates towards the slice tangent (the +omega side).
vec3 ssgiSliceDir(float theta, vec3 viewVec, vec3 sliceTangent)
{
  return cos(theta) * viewVec + sin(theta) * sliceTangent;
}

// Closed form of the measure-weighted direction integral over the full arc,
// integral of dir(theta) * d(mu) over [n - PI/2, n + PI/2]. Both components
// reduce to polynomials in sin(n)/cos(n); summed over slices this approximates
// the cosine-weighted mean unoccluded direction, which on an unoccluded plane
// is the surface normal itself.
vec3 ssgiFullArcDir(float sinN, float cosN, vec3 viewVec, vec3 sliceTangent)
{
  return (2.0 / 3.0) * cosN * viewVec + (4.0 / 3.0) * sinN * sliceTangent;
}

#endif
