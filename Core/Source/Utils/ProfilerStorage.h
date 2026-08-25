#pragma once

#include "Pch.h"

namespace YAEngine
{
  enum class ProfileDomain : uint8_t
  {
    CPU = 0,
    GPU = 1,
    Count = 2
  };

  // Per-frame timings in a zone-major ring buffer (history[zone][frame]), so the panel
  // can hand ImPlot a contiguous series and a zone keeps its slot (color, stack position)
  // even on frames where it does not run.
  class ProfilerStorage
  {
  public:

    // Sized for the render graph passes plus one GPU zone per shadow atlas tile
    // group (4 cascades + 8 spots + 4 point lights) with headroom.
    static constexpr uint32_t MAX_ZONES = 64;
    // Sized for ten seconds of history at 400 fps. The panel aggregates frames into
    // time buckets, so the ring only has to be long enough, not economical.
    static constexpr uint32_t HISTORY_SIZE = 4096;
    // Scene/asset loading makes the first frames cost orders of magnitude more than a
    // steady one, which would pin the chart scale for the whole window, so they are dropped.
    static constexpr uint32_t WARMUP_FRAMES = 120;

    static ProfilerStorage& Get();

    // Advances the frame counter and zeroes the incoming column in both domains; done here
    // (not on resolve) since GPU results land several frames after the frame they belong to.
    void BeginFrame();

    uint64_t GetCurrentFrame() const { return m_CurrentFrame; }
    // Frame that writes should be attributed to. Zero while paused or warming up,
    // and zero is rejected by the write path, so nothing recorded then reaches the ring.
    uint64_t GetRecordingFrame() const { return b_Paused ? 0 : m_CurrentFrame; }
    // How many frames of this domain are readable. GPU results arrive a few frames
    // after the frame they describe, so its window trails the CPU one.
    uint32_t GetAvailableFrames(ProfileDomain domain) const;

    uint32_t RegisterZone(ProfileDomain domain, const char* name);
    void WriteZone(ProfileDomain domain, uint32_t zone, uint64_t frame, float milliseconds);
    void WriteFrameTotal(ProfileDomain domain, uint64_t frame, float milliseconds);

    uint32_t GetZoneCount(ProfileDomain domain) const;
    const char* GetZoneName(ProfileDomain domain, uint32_t zone) const;

    // Unrolls the ring into chronological order, oldest first, ending at the newest frame.
    void CopyZoneHistory(ProfileDomain domain, uint32_t zone, uint32_t count, std::span<float> out) const;
    void CopyFrameTotals(ProfileDomain domain, uint32_t count, std::span<float> out) const;
    // Seconds since startup per frame of the window, so the panel can bucket by elapsed
    // time instead of frame index. Double, since a float loses sub-bucket resolution
    // after an hour of uptime.
    void CopyFrameTimes(ProfileDomain domain, uint32_t count, std::span<double> out) const;
    // Takes effect on the next frame boundary, never mid-frame: the pause control is drawn
    // from inside the swapchain pass, so flipping it immediately would split a column.
    void SetPaused(bool paused) { b_PauseRequested = paused; }
    bool IsPaused() const { return b_PauseRequested; }
    bool IsWarmingUp() const { return m_WarmupLeft > 0; }

  private:

    // Transparent hash so a zone lookup from a string literal does not build a temporary
    // std::string every frame - the render graph looks up every pass by name.
    struct StringHash
    {
      using is_transparent = void;
      size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
    };

    struct Domain
    {
      std::vector<std::string> zoneNames;
      std::unordered_map<std::string, uint32_t, StringHash, std::equal_to<>> zoneLookup;
      std::vector<float> history;
      std::vector<float> totals;
      uint64_t newestFrame = 0;
    };

    ProfilerStorage();

    Domain& GetDomain(ProfileDomain domain) { return m_Domains[static_cast<size_t>(domain)]; }
    const Domain& GetDomain(ProfileDomain domain) const { return m_Domains[static_cast<size_t>(domain)]; }

    std::array<Domain, static_cast<size_t>(ProfileDomain::Count)> m_Domains;
    std::vector<double> m_FrameTimes;
    // Stays at 0 while warming up, and frame 0 is rejected by the write path, so
    // nothing recorded during warmup can reach the ring.
    uint64_t m_CurrentFrame = 0;
    uint32_t m_WarmupLeft = WARMUP_FRAMES;
    bool b_Paused = false;
    bool b_PauseRequested = false;
  };

  // Writes on the first Stop and ignores the rest, so a span that is left open by an
  // early return is still recorded, by the destructor, instead of vanishing.
  class CpuProfileSpan
  {
  public:

    explicit CpuProfileSpan(const char* name)
      : m_Zone(ProfilerStorage::Get().RegisterZone(ProfileDomain::CPU, name)),
        m_Start(glfwGetTime())
    {
    }

    ~CpuProfileSpan()
    {
      Stop();
    }

    void Stop()
    {
      if (b_Stopped)
        return;

      b_Stopped = true;
      auto& storage = ProfilerStorage::Get();
      storage.WriteZone(ProfileDomain::CPU, m_Zone, storage.GetRecordingFrame(),
        static_cast<float>((glfwGetTime() - m_Start) * 1000.0));
    }

    CpuProfileSpan(const CpuProfileSpan&) = delete;
    CpuProfileSpan& operator=(const CpuProfileSpan&) = delete;

  private:

    uint32_t m_Zone;
    double m_Start;
    bool b_Stopped = false;
  };
}

#ifdef YA_EDITOR
  #define YA_PROFILE_CONCAT_INNER(a, b) a##b
  #define YA_PROFILE_CONCAT(a, b) YA_PROFILE_CONCAT_INNER(a, b)
  // Every macro expands to a single statement, so they stay safe inside an unbraced
  // if or loop and behave the same way as their no-op counterparts.
  #define YA_PROFILE_CPU(name) YAEngine::CpuProfileSpan YA_PROFILE_CONCAT(profileSpan, __LINE__)(name)
  // Explicit form for spans that do not fit a scope, such as a stretch of Render::Draw
  // that has to exclude the calls bracketing it. The tag makes the local unique.
  #define YA_PROFILE_CPU_BEGIN(tag, name) YAEngine::CpuProfileSpan profileSpan_##tag(name)
  #define YA_PROFILE_CPU_END(tag) profileSpan_##tag.Stop()
  #define YA_PROFILE_FRAME_BEGIN() YAEngine::ProfilerStorage::Get().BeginFrame()
  #define YA_PROFILE_FRAME_TOTAL(ms) YAEngine::ProfilerStorage::Get().WriteFrameTotal(YAEngine::ProfileDomain::CPU, YAEngine::ProfilerStorage::Get().GetRecordingFrame(), ms)
#else
  #define YA_PROFILE_CPU(name) ((void)0)
  #define YA_PROFILE_CPU_BEGIN(tag, name) ((void)0)
  #define YA_PROFILE_CPU_END(tag) ((void)0)
  #define YA_PROFILE_FRAME_BEGIN() ((void)0)
  #define YA_PROFILE_FRAME_TOTAL(ms) ((void)0)
#endif
