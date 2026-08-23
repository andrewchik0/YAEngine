#include "Utils/ProfilerStorage.h"

namespace YAEngine
{
  ProfilerStorage::ProfilerStorage()
  {
    for (auto& domain : m_Domains)
    {
      domain.zoneNames.reserve(MAX_ZONES);
      domain.history.assign(size_t(MAX_ZONES) * HISTORY_SIZE, 0.0f);
      domain.totals.assign(HISTORY_SIZE, 0.0f);
    }
    m_FrameTimes.assign(HISTORY_SIZE, 0.0);
  }

  ProfilerStorage& ProfilerStorage::Get()
  {
    // Constructed on first use, so a build without the editor never pays for the buffers.
    static ProfilerStorage instance;
    return instance;
  }

  void ProfilerStorage::BeginFrame()
  {
    // Applied here and nowhere else, so a column is never split by a toggle that
    // happens while the frame is already half recorded.
    b_Paused = b_PauseRequested;

    if (b_Paused)
      return;

    if (m_WarmupLeft > 0)
    {
      m_WarmupLeft--;
      return;
    }

    m_CurrentFrame++;

    uint32_t slot = static_cast<uint32_t>(m_CurrentFrame % HISTORY_SIZE);
    m_FrameTimes[slot] = glfwGetTime();
    for (auto& domain : m_Domains)
    {
      for (uint32_t zone = 0; zone < MAX_ZONES; zone++)
        domain.history[size_t(zone) * HISTORY_SIZE + slot] = 0.0f;
      domain.totals[slot] = 0.0f;
    }
  }

  uint32_t ProfilerStorage::GetAvailableFrames(ProfileDomain domain) const
  {
    uint64_t newest = GetDomain(domain).newestFrame;
    if (newest == 0)
      return 0;

    // Frames older than the ring window were already zeroed by BeginFrame, and a
    // lagging domain loses that many slots off the back of its own window.
    uint64_t lag = m_CurrentFrame - newest;
    uint64_t window = lag < HISTORY_SIZE ? HISTORY_SIZE - lag : 0;
    return static_cast<uint32_t>(std::min(newest, window));
  }

  uint32_t ProfilerStorage::RegisterZone(ProfileDomain domain, const char* name)
  {
    auto& d = GetDomain(domain);

    auto it = d.zoneLookup.find(name);
    if (it != d.zoneLookup.end())
      return it->second;

    if (d.zoneNames.size() >= MAX_ZONES)
      return MAX_ZONES;

    uint32_t zone = static_cast<uint32_t>(d.zoneNames.size());
    d.zoneNames.emplace_back(name);
    d.zoneLookup.emplace(name, zone);
    return zone;
  }

  void ProfilerStorage::WriteZone(ProfileDomain domain, uint32_t zone, uint64_t frame, float milliseconds)
  {
    if (frame == 0 || zone >= MAX_ZONES)
      return;

    GetDomain(domain).history[size_t(zone) * HISTORY_SIZE + frame % HISTORY_SIZE] = milliseconds;
  }

  void ProfilerStorage::WriteFrameTotal(ProfileDomain domain, uint64_t frame, float milliseconds)
  {
    if (frame == 0)
      return;

    auto& d = GetDomain(domain);
    d.totals[frame % HISTORY_SIZE] = milliseconds;
    if (frame > d.newestFrame)
      d.newestFrame = frame;
  }

  uint32_t ProfilerStorage::GetZoneCount(ProfileDomain domain) const
  {
    return static_cast<uint32_t>(GetDomain(domain).zoneNames.size());
  }

  const char* ProfilerStorage::GetZoneName(ProfileDomain domain, uint32_t zone) const
  {
    auto& d = GetDomain(domain);
    return zone < d.zoneNames.size() ? d.zoneNames[zone].c_str() : "";
  }

  void ProfilerStorage::CopyZoneHistory(ProfileDomain domain, uint32_t zone, uint32_t count,
    std::span<float> out) const
  {
    if (zone >= MAX_ZONES || count > HISTORY_SIZE || out.size() < count)
      return;

    const auto& d = GetDomain(domain);
    if (d.newestFrame < count)
      return;

    const float* src = d.history.data() + size_t(zone) * HISTORY_SIZE;
    uint64_t oldest = d.newestFrame - count + 1;
    for (uint32_t i = 0; i < count; i++)
      out[i] = src[(oldest + i) % HISTORY_SIZE];
  }

  void ProfilerStorage::CopyFrameTotals(ProfileDomain domain, uint32_t count, std::span<float> out) const
  {
    if (count > HISTORY_SIZE || out.size() < count)
      return;

    const auto& d = GetDomain(domain);
    if (d.newestFrame < count)
      return;

    const float* src = d.totals.data();
    uint64_t oldest = d.newestFrame - count + 1;
    for (uint32_t i = 0; i < count; i++)
      out[i] = src[(oldest + i) % HISTORY_SIZE];
  }

  void ProfilerStorage::CopyFrameTimes(ProfileDomain domain, uint32_t count, std::span<double> out) const
  {
    if (count > HISTORY_SIZE || out.size() < count)
      return;

    uint64_t newest = GetDomain(domain).newestFrame;
    if (newest < count)
      return;

    uint64_t oldest = newest - count + 1;
    for (uint32_t i = 0; i < count; i++)
      out[i] = m_FrameTimes[(oldest + i) % HISTORY_SIZE];
  }

}
