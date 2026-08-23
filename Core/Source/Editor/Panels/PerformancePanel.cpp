#include "Editor/Panels/PerformancePanel.h"

#include <imgui.h>
#include <implot.h>

#include "Editor/EditorContext.h"
#include "Render/Render.h"
#include "Utils/Timer.h"

namespace YAEngine
{
  static constexpr float STACK_ORDER_INTERVAL = 0.5f;
  static constexpr float STATS_INTERVAL = 0.5f;
  // The chart is redrawn every frame but re-aggregated at most this often. At 200 fps
  // that alone removes two thirds of the visual churn, and it bounds the cost.
  static constexpr double AGGREGATE_INTERVAL = 1.0 / 60.0;
  // Box blur radius over buckets when smoothing is on.
  static constexpr int32_t SMOOTH_RADIUS = 2;
  // How much of the tail the breakdown averages when the chart is not hovered.
  static constexpr float BREAKDOWN_AVERAGE_SECONDS = 0.25f;
  // The Y axis only drops once the peak has been below this fraction of the current
  // limit for this long. Without it the scale dithers between two steps at the edge.
  static constexpr float Y_SHRINK_FRACTION = 0.7f;
  static constexpr double Y_SHRINK_DELAY = 1.0;
  static constexpr float PI = 3.14159265f;

  // Rounds up to a step proportional to the magnitude - 0.5 over the 1..10 ms range,
  // 0.05 over 0.1..1 - so the axis reads the same whether a whole frame or a single
  // isolated pass is on screen. Plain ceil to a millisecond would flatten the latter.
  static float SnapAxisLimit(float peak)
  {
    if (peak <= 0.0f)
      return 1.0f;

    float magnitude = std::pow(10.0f, std::floor(std::log10(peak)));
    float step = magnitude * 0.5f;
    return std::ceil(peak * 1.05f / step) * step;
  }

  // Golden-ratio hue rotation keeps neighbouring zones distinct at any zone count,
  // and the hue only depends on the zone slot, so colors never dance between frames.
  static ImVec4 ZoneColor(uint32_t zone)
  {
    float hue = std::fmod(static_cast<float>(zone) * 0.6180339887f, 1.0f);
    float value = (zone % 2 == 0) ? 0.95f : 0.70f;
    ImVec4 color(0.0f, 0.0f, 0.0f, 1.0f);
    ImGui::ColorConvertHSVtoRGB(hue, 0.62f, value, color.x, color.y, color.z);
    return color;
  }

  static const char* DomainName(ProfileDomain domain)
  {
    return domain == ProfileDomain::CPU ? "CPU" : "GPU";
  }

  PerformancePanel::~PerformancePanel()
  {
    if (m_ImPlot)
      ImPlot::DestroyContext(m_ImPlot);
  }

  float PerformancePanel::WindowSeconds() const
  {
    static constexpr float WINDOWS[] = { 2.0f, 5.0f, 10.0f };
    return WINDOWS[std::clamp(m_WindowIndex, 0, 2)];
  }

  void PerformancePanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("Performance"))
    {
      ImGui::End();
      return;
    }

    // Created here rather than next to the ImGui context so implot.h stays inside
    // this one translation unit. The panel is destroyed before ImGui shuts down.
    if (!m_ImPlot)
      m_ImPlot = ImPlot::CreateContext();

    if (context.timer)
    {
      float dt = static_cast<float>(context.timer->GetDeltaTime());
      m_StatsTimer += dt;
      m_OrderTimer += dt;
    }

    DrawToolbar();

    if (m_StatsTimer >= STATS_INTERVAL)
    {
      m_StatsTimer = 0.0f;
      if (context.timer)
        m_DisplayFPS = context.timer->GetFPS();
    }

    bool refreshOrder = m_OrderTimer >= STACK_ORDER_INTERVAL;
    if (refreshOrder)
      m_OrderTimer = 0.0f;

    double now = glfwGetTime();
    if (b_ForceAggregate || refreshOrder || now - m_LastAggregate >= AGGREGATE_INTERVAL)
    {
      m_LastAggregate = now;
      b_ForceAggregate = false;

      if (m_Mode != DisplayMode::GPU)
        Aggregate(ProfileDomain::CPU, refreshOrder);
      if (m_Mode != DisplayMode::CPU)
        Aggregate(ProfileDomain::GPU, refreshOrder);
    }

    m_HoveredBucket = -1;

    float contentHeight = ImGui::GetContentRegionAvail().y;

    if (ImGui::BeginTable("##perfLayout", 3,
      ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings))
    {
      ImGui::TableSetupColumn("##summary", ImGuiTableColumnFlags_WidthStretch, 0.15f);
      ImGui::TableSetupColumn("##chart", ImGuiTableColumnFlags_WidthStretch, 0.60f);
      ImGui::TableSetupColumn("##breakdown", ImGuiTableColumnFlags_WidthStretch, 0.25f);
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      DrawSummary(context);

      ImGui::TableSetColumnIndex(1);
      if (m_Mode == DisplayMode::Both)
      {
        float half = (contentHeight - ImGui::GetStyle().ItemSpacing.y) * 0.5f;
        DrawChart(ProfileDomain::CPU, half);
        DrawChart(ProfileDomain::GPU, half);
      }
      else
      {
        DrawChart(m_Mode == DisplayMode::CPU ? ProfileDomain::CPU : ProfileDomain::GPU,
          contentHeight);
      }

      ImGui::TableSetColumnIndex(2);
      if (ImGui::BeginChild("##breakdownScroll", ImVec2(0.0f, contentHeight)))
      {
        // Both modes list both domains rather than switching on hover: the cursor has
        // to cross the other chart on its way to this column, and a domain that
        // changed under the cursor would send the next click to the wrong zone.
        if (m_Mode != DisplayMode::GPU)
          DrawBreakdown(ProfileDomain::CPU);
        if (m_Mode == DisplayMode::Both)
          ImGui::Spacing();
        if (m_Mode != DisplayMode::CPU)
          DrawBreakdown(ProfileDomain::GPU);
      }
      ImGui::EndChild();

      ImGui::EndTable();
    }

    ImGui::End();
  }

  void PerformancePanel::DrawToolbar()
  {
    auto& storage = ProfilerStorage::Get();

    int mode = static_cast<int>(m_Mode);
    ImGui::RadioButton("CPU", &mode, 0); ImGui::SameLine();
    ImGui::RadioButton("GPU", &mode, 1); ImGui::SameLine();
    ImGui::RadioButton("Both", &mode, 2);
    if (mode != static_cast<int>(m_Mode))
    {
      // A domain that was not being aggregated holds buckets, a stack order and a Y
      // limit from whenever it was last shown; drawing a frame against those is wrong.
      m_Mode = static_cast<DisplayMode>(mode);
      b_ForceAggregate = true;
      for (auto& view : m_Views)
        view.lowSince = 0.0;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    bool paused = storage.IsPaused();
    if (ImGui::Checkbox("Pause", &paused))
      storage.SetPaused(paused);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(80.0f);
    const char* windows[] = { "2 s", "5 s", "10 s" };
    if (ImGui::Combo("Window", &m_WindowIndex, windows, IM_ARRAYSIZE(windows)))
      b_ForceAggregate = true;

    ImGui::SameLine();
    if (ImGui::Checkbox("Smooth", &b_Smooth))
      b_ForceAggregate = true;

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    int breakdown = static_cast<int>(m_Breakdown);
    ImGui::RadioButton("Bars", &breakdown, 0); ImGui::SameLine();
    ImGui::RadioButton("Pie", &breakdown, 1);
    m_Breakdown = static_cast<BreakdownMode>(breakdown);

    ImGui::Separator();
  }

  void PerformancePanel::DrawSummary(EditorContext& context)
  {
    auto& storage = ProfilerStorage::Get();

    float cpuTotal = 0.0f;
    float gpuTotal = 0.0f;
    storage.CopyFrameTotals(ProfileDomain::CPU, 1, std::span<float>(&cpuTotal, 1));
    storage.CopyFrameTotals(ProfileDomain::GPU, 1, std::span<float>(&gpuTotal, 1));

    ImGui::Text("FPS  %.1f", m_DisplayFPS);
    ImGui::Text("CPU  %.2f ms", cpuTotal);
    ImGui::Text("GPU  %.2f ms", gpuTotal);

    ImGui::Spacing();
    ImGui::TextDisabled("frame time, last %.0f s", WindowSeconds());
    ImGui::Text("min  %.2f ms", m_MinFrametime);
    ImGui::Text("max  %.2f ms", m_MaxFrametime);
    ImGui::Text("avg  %.2f ms", m_AvgFrametime);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (context.render)
    {
      auto& stats = context.render->GetStats();
      ImGui::Text("Draws  %u", stats.drawCalls);
      ImGui::Text("Tris   %u", stats.triangles);
      ImGui::Text("Verts  %u", stats.vertices);
    }
    else
    {
      ImGui::TextDisabled("No render stats");
    }

    ImGui::Spacing();
    if (storage.IsPaused())
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "PAUSED");
    else if (storage.IsWarmingUp())
      ImGui::TextDisabled("warming up...");
  }

  void PerformancePanel::Aggregate(ProfileDomain domain, bool refreshOrder)
  {
    auto& storage = ProfilerStorage::Get();
    auto& view = GetView(domain);

    uint32_t zoneCount = storage.GetZoneCount(domain);
    uint32_t avail = storage.GetAvailableFrames(domain);

    view.zoneCount = zoneCount;
    view.hasData = false;
    if (zoneCount == 0 || avail < 2)
      return;

    double bucketSeconds = BucketSeconds();

    m_FrameTimes.resize(avail);
    storage.CopyFrameTimes(domain, avail, m_FrameTimes);

    // Buckets sit on an absolute grid rather than on one anchored to the newest
    // frame. With a sliding anchor every new frame shifted all the bucket edges, so
    // a frame near an edge kept hopping between two buckets and their averages
    // visibly changed height as the history scrolled left.
    double newest = m_FrameTimes[avail - 1];
    // The bucket the newest frame falls into is still filling up; ending on the last
    // closed one costs at most one bucket of latency and keeps the right edge still.
    int64_t lastBucket = static_cast<int64_t>(std::floor(newest / bucketSeconds)) - 1;
    int64_t baseBucket = lastBucket - static_cast<int64_t>(BUCKET_COUNT) + 1;
    double start = static_cast<double>(baseBucket) * bucketSeconds;

    // The ring is far longer than any window, and frame times are ordered, so the
    // work is bounded by the window rather than by the ring.
    auto first = std::lower_bound(m_FrameTimes.begin(), m_FrameTimes.begin() + avail, start);
    uint32_t skip = static_cast<uint32_t>(first - m_FrameTimes.begin());
    uint32_t tail = avail - skip;
    if (tail < 2)
      return;

    m_FrameBucket.resize(tail);
    m_BucketCounts.assign(BUCKET_COUNT, 0);
    for (uint32_t i = 0; i < tail; i++)
    {
      int64_t absolute = static_cast<int64_t>(std::floor(m_FrameTimes[skip + i] / bucketSeconds));
      int32_t bucket = static_cast<int32_t>(absolute - baseBucket);
      // Frames past the last closed bucket belong to the one still filling up.
      if (bucket < 0 || bucket >= static_cast<int32_t>(BUCKET_COUNT))
      {
        m_FrameBucket[i] = -1;
        continue;
      }
      m_FrameBucket[i] = bucket;
      m_BucketCounts[bucket]++;
    }

    view.firstBucket = BUCKET_COUNT - 1;
    for (uint32_t bucket = 0; bucket < BUCKET_COUNT; bucket++)
    {
      if (m_BucketCounts[bucket] > 0)
      {
        view.firstBucket = bucket;
        break;
      }
    }

    m_Values.resize(tail);

    // Frame time statistics come from the same window as the plot, so the numbers in
    // the summary and the shape of the chart can never disagree.
    if (domain == ProfileDomain::CPU)
    {
      storage.CopyFrameTotals(domain, tail, m_Values);

      float minimum = FLT_MAX;
      float maximum = 0.0f;
      float sum = 0.0f;
      uint32_t count = 0;
      for (uint32_t i = 0; i < tail; i++)
      {
        float value = m_Values[i];
        if (value <= 0.0f)
          continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        sum += value;
        count++;
      }

      m_MinFrametime = count > 0 ? minimum : 0.0f;
      m_MaxFrametime = maximum;
      m_AvgFrametime = count > 0 ? sum / static_cast<float>(count) : 0.0f;
    }

    view.buckets.assign(static_cast<size_t>(zoneCount) * BUCKET_COUNT, 0.0f);

    for (uint32_t zone = 0; zone < zoneCount; zone++)
    {
      storage.CopyZoneHistory(domain, zone, tail, m_Values);

      float* dst = view.buckets.data() + static_cast<size_t>(zone) * BUCKET_COUNT;
      for (uint32_t i = 0; i < tail; i++)
      {
        if (m_FrameBucket[i] >= 0)
          dst[m_FrameBucket[i]] += m_Values[i];
      }

      // Average per frame, and carry the previous value through buckets that caught
      // no frame at all, so a low frame rate does not punch holes into the curve.
      float previous = 0.0f;
      for (uint32_t bucket = view.firstBucket; bucket < BUCKET_COUNT; bucket++)
      {
        if (m_BucketCounts[bucket] > 0)
          previous = dst[bucket] / static_cast<float>(m_BucketCounts[bucket]);
        dst[bucket] = previous;
      }

      if (b_Smooth)
      {
        m_SmoothScratch.resize(BUCKET_COUNT);
        int32_t from = static_cast<int32_t>(view.firstBucket);
        int32_t last = static_cast<int32_t>(BUCKET_COUNT) - 1;
        for (int32_t bucket = from; bucket <= last; bucket++)
        {
          int32_t lo = std::max(from, bucket - SMOOTH_RADIUS);
          int32_t hi = std::min(last, bucket + SMOOTH_RADIUS);
          float sum = 0.0f;
          for (int32_t j = lo; j <= hi; j++)
            sum += dst[j];
          m_SmoothScratch[bucket] = sum / static_cast<float>(hi - lo + 1);
        }
        std::copy(m_SmoothScratch.begin() + from, m_SmoothScratch.begin() + last + 1, dst + from);
      }
    }

    // Seconds behind the newest closed bucket, so the rightmost column sits at 0.
    m_AxisX.resize(BUCKET_COUNT);
    for (uint32_t bucket = 0; bucket < BUCKET_COUNT; bucket++)
    {
      m_AxisX[bucket] = static_cast<float>(
        (static_cast<double>(bucket) - static_cast<double>(BUCKET_COUNT - 1)) * bucketSeconds);
    }

    // Peak of the stack, which is what the axis has to fit - the tallest single zone
    // says nothing about how high the bands reach once they are piled up.
    float peak = 0.0f;
    for (uint32_t bucket = view.firstBucket; bucket < BUCKET_COUNT; bucket++)
    {
      float stacked = 0.0f;
      for (uint32_t zone = 0; zone < zoneCount; zone++)
      {
        if (!view.hidden[zone])
          stacked += view.buckets[static_cast<size_t>(zone) * BUCKET_COUNT + bucket];
      }
      peak = std::max(peak, stacked);
    }

    // The shrink test compares the raw peak, not the snapped one: snapping the peak
    // and comparing that against the limit makes the two land on the same value at
    // a decade boundary, and the axis then never comes back down.
    float target = SnapAxisLimit(peak);
    double now = glfwGetTime();
    if (target > view.yLimit)
    {
      view.yLimit = target;
      view.lowSince = 0.0;
    }
    else if (peak < view.yLimit * Y_SHRINK_FRACTION)
    {
      if (view.lowSince == 0.0)
        view.lowSince = now;
      else if (now - view.lowSince >= Y_SHRINK_DELAY)
      {
        view.yLimit = target;
        view.lowSince = 0.0;
      }
    }
    else
    {
      view.lowSince = 0.0;
    }

    view.hasData = true;

    if (refreshOrder || view.stackOrder.size() != zoneCount)
      RefreshStackOrder(domain);
  }

  void PerformancePanel::RefreshStackOrder(ProfileDomain domain)
  {
    auto& view = GetView(domain);
    uint32_t zoneCount = view.zoneCount;

    view.stackOrder.resize(zoneCount);
    for (uint32_t zone = 0; zone < zoneCount; zone++)
    {
      view.stackOrder[zone] = zone;

      float sum = 0.0f;
      if (view.hasData)
      {
        const float* src = view.buckets.data() + static_cast<size_t>(zone) * BUCKET_COUNT;
        for (uint32_t bucket = view.firstBucket; bucket < BUCKET_COUNT; bucket++)
          sum += src[bucket];
      }
      m_Averages[zone] = sum;
    }

    // Heaviest zones end up at the bottom of the stack, where a growing band is
    // easiest to read, and the breakdown list reuses this order so its rows never
    // swap places while you are looking at them.
    std::sort(view.stackOrder.begin(), view.stackOrder.end(),
      [this](uint32_t a, uint32_t b) { return m_Averages[a] > m_Averages[b]; });
  }

  void PerformancePanel::DrawChart(ProfileDomain domain, float height)
  {
    auto& view = GetView(domain);

    if (!view.hasData || view.firstBucket + 2 > BUCKET_COUNT)
    {
      ImGui::Dummy(ImVec2(0.0f, height));
      return;
    }

    const char* title = domain == ProfileDomain::CPU ? "##cpuChart" : "##gpuChart";
    if (!ImPlot::BeginPlot(title, ImVec2(-1.0f, height), ImPlotFlags_CanvasOnly))
      return;

    uint32_t offset = view.firstBucket;
    int32_t count = static_cast<int32_t>(BUCKET_COUNT - offset);
    double span = static_cast<double>(BUCKET_COUNT - 1) * BucketSeconds();

    ImPlot::SetupAxes(nullptr, nullptr,
      ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoMenus,
      ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoMenus);
    ImPlot::SetupAxisLimits(ImAxis_X1, -span, 0.0, ImPlotCond_Always);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, static_cast<double>(view.yLimit), ImPlotCond_Always);
    ImPlot::SetupAxisFormat(ImAxis_X1, "%g s");
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%g ms");

    m_Lower.assign(count, 0.0f);
    m_Upper.resize(count);

    for (uint32_t zone : view.stackOrder)
    {
      if (zone >= view.zoneCount || view.hidden[zone])
        continue;

      const float* src = view.buckets.data() + static_cast<size_t>(zone) * BUCKET_COUNT + offset;

      bool hasSamples = false;
      for (int32_t i = 0; i < count; i++)
      {
        m_Upper[i] = m_Lower[i] + src[i];
        hasSamples = hasSamples || src[i] > 0.0f;
      }

      if (hasSamples)
      {
        ImPlotSpec spec;
        spec.FillColor = ZoneColor(zone);
        spec.FillAlpha = 0.9f;
        spec.LineWeight = 0.0f;
        ImPlot::PlotShaded(ProfilerStorage::Get().GetZoneName(domain, zone),
          m_AxisX.data() + offset, m_Lower.data(), m_Upper.data(), count, spec);
      }

      m_Lower.swap(m_Upper);
    }

    if (ImPlot::IsPlotHovered())
    {
      ImPlotPoint mouse = ImPlot::GetPlotMousePos();
      int32_t bucket = static_cast<int32_t>(std::lround(mouse.x / BucketSeconds()))
        + static_cast<int32_t>(BUCKET_COUNT) - 1;
      m_HoveredBucket = std::clamp(bucket, static_cast<int32_t>(offset),
        static_cast<int32_t>(BUCKET_COUNT) - 1);

      double cursor = m_AxisX[m_HoveredBucket];
      ImPlotSpec cursorSpec;
      cursorSpec.LineColor = ImVec4(1.0f, 1.0f, 1.0f, 0.55f);
      cursorSpec.LineWeight = 1.0f;
      ImPlot::PlotInfLines("##cursor", &cursor, 1, cursorSpec);
    }

    ImPlot::EndPlot();
  }

  void PerformancePanel::CacheZoneValues(ProfileDomain domain)
  {
    auto& view = GetView(domain);
    m_ZoneValues.fill(0.0f);

    if (!view.hasData)
      return;

    // Averaging the tail keeps the digits from flickering at a high frame rate.
    int32_t span = std::max(1, static_cast<int32_t>(BREAKDOWN_AVERAGE_SECONDS / BucketSeconds()));
    span = std::min(span, static_cast<int32_t>(BUCKET_COUNT - view.firstBucket));

    for (uint32_t zone = 0; zone < view.zoneCount; zone++)
    {
      const float* src = view.buckets.data() + static_cast<size_t>(zone) * BUCKET_COUNT;

      if (m_HoveredBucket >= 0)
      {
        m_ZoneValues[zone] = src[m_HoveredBucket];
        continue;
      }

      float sum = 0.0f;
      for (int32_t i = 0; i < span; i++)
        sum += src[BUCKET_COUNT - 1 - i];
      m_ZoneValues[zone] = sum / static_cast<float>(span);
    }
  }

  void PerformancePanel::DrawBreakdown(ProfileDomain domain)
  {
    auto& view = GetView(domain);

    if (!view.hasData || view.stackOrder.empty())
    {
      ImGui::TextDisabled("%s: no data yet", DomainName(domain));
      return;
    }

    CacheZoneValues(domain);

    float total = 0.0f;
    for (uint32_t zone : view.stackOrder)
    {
      if (zone < view.zoneCount && !view.hidden[zone])
        total += m_ZoneValues[zone];
    }

    ImGui::Text("%s  %s", DomainName(domain),
      m_HoveredBucket >= 0 ? "(hovered)" : "(recent average)");
    ImGui::TextDisabled("sum %.3f ms over %u zones", total, view.zoneCount);
    ImGui::Separator();

    if (m_Breakdown == BreakdownMode::Pie)
    {
      float side = std::min(ImGui::GetContentRegionAvail().x, 170.0f);
      DrawBreakdownPie(domain, side);
      ImGui::Separator();
    }

    DrawBreakdownRows(domain);
  }

  void PerformancePanel::DrawBreakdownPie(ProfileDomain domain, float height)
  {
    auto& view = GetView(domain);

    float total = 0.0f;
    for (uint32_t zone : view.stackOrder)
    {
      if (zone < view.zoneCount && !view.hidden[zone])
        total += m_ZoneValues[zone];
    }

    float width = ImGui::GetContentRegionAvail().x;
    if (total <= 0.0f || width <= 0.0f)
    {
      ImGui::Dummy(ImVec2(width, height));
      return;
    }

    // Drawn by hand rather than with ImPlot::PlotPieChart: that one ignores the fill
    // colors in the spec and takes each slice from the colormap instead, so the pie
    // would not match the colors of the chart bands or of the rows below it.
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 center(origin.x + width * 0.5f, origin.y + height * 0.5f);
    float radius = std::min(width, height) * 0.5f - 4.0f;

    auto* drawList = ImGui::GetWindowDrawList();
    float angle = -PI * 0.5f;

    for (uint32_t zone : view.stackOrder)
    {
      if (zone >= view.zoneCount || view.hidden[zone])
        continue;

      float value = m_ZoneValues[zone];
      if (value <= 0.0f)
        continue;

      float sweep = value / total * PI * 2.0f;
      ImU32 color = ImGui::ColorConvertFloat4ToU32(ZoneColor(zone));

      // PathFillConvex needs a convex shape, so a slice wider than a half turn has
      // to be filled in two passes.
      int32_t parts = sweep > PI ? 2 : 1;
      for (int32_t part = 0; part < parts; part++)
      {
        float from = angle + sweep * static_cast<float>(part) / static_cast<float>(parts);
        float to = angle + sweep * static_cast<float>(part + 1) / static_cast<float>(parts);
        drawList->PathLineTo(center);
        drawList->PathArcTo(center, radius, from, to, 32);
        drawList->PathFillConvex(color);
      }

      angle += sweep;
    }

    ImGui::Dummy(ImVec2(width, height));
  }

  void PerformancePanel::DrawBreakdownRows(ProfileDomain domain)
  {
    auto& storage = ProfilerStorage::Get();
    auto& view = GetView(domain);

    float maxValue = 0.0f;
    for (uint32_t zone : view.stackOrder)
    {
      if (zone < view.zoneCount)
        maxValue = std::max(maxValue, m_ZoneValues[zone]);
    }

    auto* drawList = ImGui::GetWindowDrawList();
    float rowHeight = ImGui::GetTextLineHeight() + 2.0f;
    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    for (uint32_t zone : view.stackOrder)
    {
      if (zone >= view.zoneCount)
        continue;

      ImGui::PushID(static_cast<int>(domain) * 256 + static_cast<int>(zone));

      float width = ImGui::GetContentRegionAvail().x;
      ImVec2 origin = ImGui::GetCursorScreenPos();

      if (ImGui::Selectable("##row", false, 0, ImVec2(width, rowHeight)))
      {
        view.hidden[zone] = !view.hidden[zone];
        // The Y limit is computed from the visible zones, so it has to be redone
        // before the next draw rather than up to a frame later.
        b_ForceAggregate = true;
      }

      bool hidden = view.hidden[zone];
      float value = m_ZoneValues[zone];
      float fraction = maxValue > 0.0f ? std::min(value / maxValue, 1.0f) : 0.0f;

      ImVec4 color = ZoneColor(zone);
      color.w = hidden ? 0.12f : 0.45f;
      drawList->AddRectFilled(origin, ImVec2(origin.x + width * fraction, origin.y + rowHeight),
        ImGui::ColorConvertFloat4ToU32(color), 2.0f);

      ImU32 rowText = hidden ? dimColor : textColor;
      drawList->AddText(ImVec2(origin.x + 6.0f, origin.y + 1.0f), rowText,
        storage.GetZoneName(domain, zone));

      char text[24];
      snprintf(text, sizeof(text), "%.3f", value);
      float textWidth = ImGui::CalcTextSize(text).x;
      drawList->AddText(ImVec2(origin.x + width - textWidth - 6.0f, origin.y + 1.0f),
        rowText, text);

      ImGui::PopID();
    }
  }
}
