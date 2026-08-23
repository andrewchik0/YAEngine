#include "Render/GpuProfiler.h"

#include "Render/RenderContext.h"
#include "Utils/Log.h"
#include "Utils/ProfilerStorage.h"

namespace YAEngine
{
  static constexpr uint32_t QUERIES_PER_POOL = ProfilerStorage::MAX_ZONES * 2;
  // A zone longer than this cannot be real at any frame rate we care about; it means
  // the pool handed back garbage. Dropping it keeps one bad sample from pinning the
  // chart scale for the whole history window.
  static constexpr float MAX_PLAUSIBLE_ZONE_MS = 1000.0f;

  void GpuProfiler::Init(const RenderContext& ctx)
  {
    b_Supported = ctx.timestampsSupported;
    m_TimestampPeriod = ctx.timestampPeriod;
    m_TimestampMask = ctx.timestampValidBits >= 64
      ? UINT64_MAX : ((uint64_t(1) << ctx.timestampValidBits) - 1);

    if (!b_Supported)
    {
      YA_LOG_WARN("Render", "Device reports no timestamp support, GPU profiler disabled");
      return;
    }

    m_Slots.resize(ctx.maxFramesInFlight);
    m_Results.resize(size_t(QUERIES_PER_POOL) * 2);

    VkQueryPoolCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = QUERIES_PER_POOL;

    for (auto& slot : m_Slots)
    {
      slot.zones.reserve(ProfilerStorage::MAX_ZONES);
      if (vkCreateQueryPool(ctx.device, &info, nullptr, &slot.pool) != VK_SUCCESS)
      {
        YA_LOG_ERROR("Render", "Failed to create timestamp query pool");
        throw std::runtime_error("Failed to create timestamp query pool!");
      }
    }
  }

  void GpuProfiler::Destroy(const RenderContext& ctx)
  {
    for (auto& slot : m_Slots)
    {
      if (slot.pool != VK_NULL_HANDLE)
        vkDestroyQueryPool(ctx.device, slot.pool, nullptr);
    }
    m_Slots.clear();
  }

  void GpuProfiler::BeginFrame(const RenderContext& ctx, VkCommandBuffer cmd,
    uint32_t frameSlot, uint64_t frameIndex)
  {
    if (!b_Supported || frameSlot >= m_Slots.size())
      return;

    m_ActiveSlot = frameSlot;
    auto& slot = m_Slots[frameSlot];

    Resolve(ctx, slot);

    vkCmdResetQueryPool(cmd, slot.pool, 0, QUERIES_PER_POOL);
    slot.zones.clear();
    slot.frameIndex = frameIndex;
    slot.recorded = true;
  }

  uint32_t GpuProfiler::BeginZone(VkCommandBuffer cmd, const char* name)
  {
    if (!b_Supported)
      return INVALID_ZONE;

    auto& slot = m_Slots[m_ActiveSlot];
    if (!slot.recorded || slot.zones.size() >= ProfilerStorage::MAX_ZONES)
      return INVALID_ZONE;

    uint32_t handle = static_cast<uint32_t>(slot.zones.size());
    slot.zones.push_back(ProfilerStorage::Get().RegisterZone(ProfileDomain::GPU, name));

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.pool, handle * 2);
    return handle;
  }

  void GpuProfiler::EndZone(VkCommandBuffer cmd, uint32_t handle)
  {
    if (!b_Supported || handle == INVALID_ZONE)
      return;

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      m_Slots[m_ActiveSlot].pool, handle * 2 + 1);
  }

  void GpuProfiler::Resolve(const RenderContext& ctx, FrameSlot& slot)
  {
    if (!slot.recorded || slot.zones.empty())
      return;

    uint32_t queryCount = static_cast<uint32_t>(slot.zones.size()) * 2;

    // Two uint64 per query: the tick value followed by its availability.
    auto result = vkGetQueryPoolResults(ctx.device, slot.pool, 0, queryCount,
      queryCount * 2 * sizeof(uint64_t), m_Results.data(), 2 * sizeof(uint64_t),
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    // VK_NOT_READY means the frame is still in flight - drop it rather than stall.
    if (result != VK_SUCCESS)
      return;

    auto& storage = ProfilerStorage::Get();
    uint64_t firstBegin = UINT64_MAX;
    uint64_t lastEnd = 0;

    for (size_t i = 0; i < slot.zones.size(); i++)
    {
      // VK_SUCCESS does not promise every query in the batch was ready, so the
      // availability word next to each value has to be checked before using it.
      if (m_Results[i * 4 + 1] == 0 || m_Results[i * 4 + 3] == 0)
        continue;

      // Only the low timestampValidBits bits carry data; the rest is undefined.
      uint64_t begin = m_Results[i * 4] & m_TimestampMask;
      uint64_t end = m_Results[i * 4 + 2] & m_TimestampMask;
      if (end <= begin)
        continue;

      float milliseconds = static_cast<float>(double(end - begin) * m_TimestampPeriod * 1e-6);
      if (milliseconds > MAX_PLAUSIBLE_ZONE_MS)
        continue;

      storage.WriteZone(ProfileDomain::GPU, slot.zones[i], slot.frameIndex, milliseconds);

      firstBegin = std::min(firstBegin, begin);
      lastEnd = std::max(lastEnd, end);
    }

    // Zones overlap when passes are not separated by barriers, so their sum is not
    // the frame cost. The span from the first to the last timestamp is.
    if (lastEnd > firstBegin)
    {
      float total = static_cast<float>(double(lastEnd - firstBegin) * m_TimestampPeriod * 1e-6);
      if (total <= MAX_PLAUSIBLE_ZONE_MS)
        storage.WriteFrameTotal(ProfileDomain::GPU, slot.frameIndex, total);
    }

    slot.recorded = false;
  }
}
