#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct RenderContext;

  // Timestamp queries bracketing render graph passes. Results are read back
  // maxFramesInFlight frames late and without VK_QUERY_RESULT_WAIT_BIT, so the
  // CPU never blocks on the GPU to collect them.
  class GpuProfiler
  {
  public:

    static constexpr uint32_t INVALID_ZONE = UINT32_MAX;

    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    // Resolves whatever the pool still holds from an earlier frame, then resets it.
    // Must be called outside a render pass.
    void BeginFrame(const RenderContext& ctx, VkCommandBuffer cmd, uint32_t frameSlot, uint64_t frameIndex);

    uint32_t BeginZone(VkCommandBuffer cmd, const char* name);
    void EndZone(VkCommandBuffer cmd, uint32_t handle);

    bool IsSupported() const { return b_Supported; }

  private:

    struct FrameSlot
    {
      VkQueryPool pool = VK_NULL_HANDLE;
      std::vector<uint32_t> zones;
      uint64_t frameIndex = 0;
      bool recorded = false;
    };

    void Resolve(const RenderContext& ctx, FrameSlot& slot);

    std::vector<FrameSlot> m_Slots;
    std::vector<uint64_t> m_Results;
    uint32_t m_ActiveSlot = 0;
    float m_TimestampPeriod = 1.0f;
    uint64_t m_TimestampMask = UINT64_MAX;
    bool b_Supported = false;
  };

  class GpuZoneScope
  {
  public:

    GpuZoneScope(GpuProfiler* profiler, VkCommandBuffer cmd, const char* name)
      : m_Profiler(profiler), m_Cmd(cmd),
        m_Handle(profiler ? profiler->BeginZone(cmd, name) : GpuProfiler::INVALID_ZONE)
    {
    }

    ~GpuZoneScope()
    {
      if (m_Profiler)
        m_Profiler->EndZone(m_Cmd, m_Handle);
    }

    GpuZoneScope(const GpuZoneScope&) = delete;
    GpuZoneScope& operator=(const GpuZoneScope&) = delete;

  private:

    GpuProfiler* m_Profiler;
    VkCommandBuffer m_Cmd;
    uint32_t m_Handle;
  };
}
