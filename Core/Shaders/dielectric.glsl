#ifndef DIELECTRIC_GLSL
#define DIELECTRIC_GLSL

// The smooth dielectric interface every PT-transmissive surface is, shared by the path estimator,
// its ray reconstruction guides and the shadow any-hit, so all three weigh one interface. Pure
// math, no bindings.

// Unpolarized Fresnel reflectance of a smooth interface for light arriving at cosIncident to its
// normal, eta = n incident / n transmitted. Exact rather than Schlick's approximation from f0, which
// from inside the denser medium misses total internal reflection and is far off near it. Returns 1
// at total internal reflection, with cosTransmitted 0.
float fresnelDielectric(float cosIncident, float eta, out float cosTransmitted)
{
  float sin2Transmitted = eta * eta * max(1.0 - cosIncident * cosIncident, 0.0);
  if (sin2Transmitted >= 1.0)
  {
    cosTransmitted = 0.0;
    return 1.0;
  }

  cosTransmitted = sqrt(1.0 - sin2Transmitted);
  float perpendicular = (eta * cosIncident - cosTransmitted) / (eta * cosIncident + cosTransmitted);
  float parallel = (cosIncident - eta * cosTransmitted) / (cosIncident + eta * cosTransmitted);
  return 0.5 * (perpendicular * perpendicular + parallel * parallel);
}

// Reflectance of a thin slab with every internal bounce summed, R + T^2 R / (1 - R^2) with
// T = 1 - R, which reduces to 2R / (1 + R) and stays finite at R = 1. eta is the slab's front
// interface's, as above.
float thinSlabReflectance(float cosIncident, float eta)
{
  float cosTransmitted;
  float reflectance = fresnelDielectric(cosIncident, eta, cosTransmitted);
  return 2.0 * reflectance / (1.0 + reflectance);
}

#endif
