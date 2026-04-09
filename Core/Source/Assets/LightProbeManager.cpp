#include "LightProbeManager.h"

#include "Render/RenderContext.h"
#include "Utils/Log.h"

namespace YAEngine
{
  LightProbeHandle LightProbeManager::Create(uint32_t resolution)
  {
    auto probe = std::make_unique<LightProbe>();
    probe->resolution = resolution;
    probe->baked = false;
    probe->generation = 0;

    auto handle = Store(std::move(probe));
    YA_LOG_INFO("Assets", "Created light probe (resolution=%u)", resolution);
    return handle;
  }

  void LightProbeManager::Destroy(LightProbeHandle handle)
  {
    auto& probe = Get(handle);
    if (probe.baked)
      probe.m_CubeTexture.Destroy(*m_Ctx);
    Remove(handle);
  }

  void LightProbeManager::DestroyAll()
  {
    ForEach([this](LightProbe& probe) {
      if (probe.baked)
        probe.m_CubeTexture.Destroy(*m_Ctx);
    });
    Clear();
  }
}
