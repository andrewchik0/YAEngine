#pragma once

#include "VulkanImage.h"
#include "OffscreenRenderer.h"
#include "BakeLimits.h"
#include "Utils/IrradianceGrid.h"

namespace YAEngine
{
  struct RenderContext;
  struct FrameContext;
  struct CubicTextureResources;
  class Scene;

  struct IrradianceVolumeBakeDesc
  {
    // Node world positions come straight from the layout - the lattice is world
    // axis aligned, so the box center and orientation play no part in the capture.
    const IrradianceGridLayout* layout = nullptr;
    uint32_t captureResolution = 32;
    // Collider layers that count as solid geometry a node can be buried in
    uint32_t colliderMask = ~0u;
    const char* volumeName = "";
  };

  struct IrradianceVolumeBakeResult
  {
    uint32_t nodeCount = 0;
    uint32_t rejectedCount = 0;
    bool anyValid = false;
  };

  class IrradianceVolumeBaker
  {
  public:

    void Init(Render& render, uint32_t resolution);
    void Destroy();

    // Captures every node of one volume into SH L1 coefficients.
    // The caller MUST have rendered the shadow atlas for the WHOLE volume before
    // calling this - the node loop never touches shadows. Fitting shadows per node
    // would mean one 8192x8192 atlas render per node and would dominate bake time.
    IrradianceVolumeBakeResult Bake(CubicTextureResources& cubicRes, FrameContext& frame,
      Scene& scene, const IrradianceVolumeBakeDesc& desc,
      std::vector<SHL1RGB>& outCoefficients, std::vector<uint8_t>& outValidity);

  private:

    // The offscreen graph is sized at Init, so a different capture resolution
    // means rebuilding it. Bakes are rare and already wait for the device.
    void EnsureResolution(uint32_t resolution);

    Render* m_Render = nullptr;
    const RenderContext* m_Ctx = nullptr;
    OffscreenRenderer m_OffscreenRenderer;
    uint32_t m_Resolution = 0;
  };
}
