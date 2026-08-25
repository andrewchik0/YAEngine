#pragma once

#include "Editor/IEditorPanel.h"
#include "Utils/ProfilerStorage.h"

struct ImPlotContext;

namespace YAEngine
{
  class PerformancePanel : public IEditorPanel
  {
  public:

    ~PerformancePanel() override;

    const char* GetName() const override { return "Performance"; }
    void OnRender(EditorContext& context) override;

  private:

    // Columns of the chart. Frames are folded into this many time buckets regardless
    // of the window length, so the plot keeps the same density at any frame rate.
    static constexpr uint32_t BUCKET_COUNT = 300;

    enum class DisplayMode : uint8_t
    {
      CPU = 0,
      GPU,
      Both
    };

    enum class BreakdownMode : uint8_t
    {
      Bars = 0,
      Pie
    };

    struct DomainView
    {
      std::array<bool, ProfilerStorage::MAX_ZONES> hidden {};
      std::vector<uint32_t> stackOrder;
      // Bucketed history, zone-major, handed straight to PlotShaded.
      std::vector<float> buckets;
      uint32_t zoneCount = 0;
      bool hasData = false;
      // Upper bound of the Y axis, snapped to a step and lowered only after the peak
      // has stayed well below it for a while, so the scale does not breathe.
      float yLimit = 0.0f;
      double lowSince = 0.0;
      // Leading buckets hold no frame when the ring is shorter than the window; the
      // chart starts here instead of drawing a flat zero that reads as "no work".
      uint32_t firstBucket = 0;
    };

    void DrawToolbar();
    void DrawSummary(EditorContext& context);
    void DrawChart(ProfileDomain domain, float height);
    void DrawBreakdown(ProfileDomain domain);
    void DrawBreakdownRows(ProfileDomain domain);
    void DrawBreakdownPie(ProfileDomain domain, float height);

    void Aggregate(ProfileDomain domain, bool refreshOrder);
    void RefreshStackOrder(ProfileDomain domain);
    // Fills m_ZoneValues for the bucket the breakdown describes: the hovered one, or
    // an average of the most recent quarter second when nothing is hovered. Computed
    // once per column instead of per row, per widget.
    void CacheZoneValues(ProfileDomain domain);
    // Recomputes the percentile blocks from the profiler ring. Runs on the stats
    // interval - sorting a thousand samples every frame would be waste.
    void UpdatePercentiles();
    // Recomputes the summary min/max/avg frame-time block. Runs on the
    // aggregation cadence in every display mode - the CPU chart aggregation it
    // used to live in is skipped entirely while the GPU mode is selected.
    void UpdateFrameTimeStats();

    DomainView& GetView(ProfileDomain domain) { return m_Views[static_cast<size_t>(domain)]; }
    const DomainView& GetView(ProfileDomain domain) const { return m_Views[static_cast<size_t>(domain)]; }

    float WindowSeconds() const;
    double BucketSeconds() const { return double(WindowSeconds()) / double(BUCKET_COUNT); }

    ImPlotContext* m_ImPlot = nullptr;

    DisplayMode m_Mode = DisplayMode::GPU;
    BreakdownMode m_Breakdown = BreakdownMode::Bars;
    int m_WindowIndex = 1;
    bool b_Smooth = true;

    std::array<DomainView, static_cast<size_t>(ProfileDomain::Count)> m_Views;

    // Scratch reused every aggregation pass; sized once, never reallocated per frame.
    std::vector<float> m_AxisX;
    std::vector<float> m_Values;
    std::vector<double> m_FrameTimes;
    std::vector<int32_t> m_FrameBucket;
    std::vector<uint32_t> m_BucketCounts;
    std::vector<float> m_SmoothScratch;
    std::vector<float> m_Lower;
    std::vector<float> m_Upper;
    std::array<float, ProfilerStorage::MAX_ZONES> m_Averages {};
    std::array<float, ProfilerStorage::MAX_ZONES> m_ZoneValues {};

    int32_t m_HoveredBucket = -1;
    ProfileDomain m_BreakdownDomain = ProfileDomain::GPU;

    float m_DisplayFPS = 0.0f;
    // Frame time statistics over the same seconds window the chart shows,
    // refreshed by UpdateFrameTimeStats in every display mode.
    float m_MinFrametime = 0.0f;
    float m_MaxFrametime = 0.0f;
    float m_AvgFrametime = 0.0f;

    // Sliding-window percentiles - the primary success metric of the shadow
    // caching project: CPU and GPU frame time and the "Shadows" GPU zone.
    static constexpr uint32_t PERCENTILE_WINDOW = 1000;
    struct Percentiles
    {
      float p50 = 0.0f;
      float p99 = 0.0f;
      float p999 = 0.0f;
      float max = 0.0f;
      uint32_t samples = 0;
    };
    Percentiles m_CpuPercentiles;
    Percentiles m_GpuPercentiles;
    Percentiles m_ShadowPercentiles;
    std::vector<float> m_PercentileScratch;

    double m_LastAggregate = 0.0;
    float m_StatsTimer = 0.0f;
    float m_OrderTimer = 0.0f;
    bool b_ForceAggregate = true;
  };
}
