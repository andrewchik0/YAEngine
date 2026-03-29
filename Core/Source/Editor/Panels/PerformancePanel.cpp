#include "Editor/Panels/PerformancePanel.h"

#include <imgui.h>

#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Render/Render.h"
#include "Utils/Timer.h"

namespace YAEngine
{
  void PerformancePanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("Performance"))
    {
      ImGui::End();
      return;
    }

    if (!context.timer)
    {
      ImGui::End();
      return;
    }

    auto& timer = *context.timer;

    float frametime = (float)timer.GetDeltaTime() * 1000.0f;

    // Update FPS display at 0.5s interval
    m_UpdateTimer += (float)timer.GetDeltaTime();
    if (m_UpdateTimer >= 0.5f)
    {
      m_DisplayFPS = timer.GetFPS();
      m_UpdateTimer = 0.0f;
    }

    // Record history
    m_FrametimeHistory[m_FrametimeOffset] = frametime;
    m_FrametimeOffset = (m_FrametimeOffset + 1) % FRAMETIME_HISTORY_SIZE;

    // Calculate min/max/avg
    m_MinFrametime = FLT_MAX;
    m_MaxFrametime = 0.0f;
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < FRAMETIME_HISTORY_SIZE; i++)
    {
      float v = m_FrametimeHistory[i];
      if (v <= 0.0f) continue;
      if (v < m_MinFrametime) m_MinFrametime = v;
      if (v > m_MaxFrametime) m_MaxFrametime = v;
      sum += v;
      count++;
    }
    m_AvgFrametime = count > 0 ? sum / (float)count : 0.0f;
    if (m_MinFrametime == FLT_MAX) m_MinFrametime = 0.0f;

    ImGui::Text("FPS: %.1f  |  Frame: %.2f ms", m_DisplayFPS, frametime);
    ImGui::Text("Min: %.2f ms  Max: %.2f ms  Avg: %.2f ms",
      m_MinFrametime, m_MaxFrametime, m_AvgFrametime);

    // Graph
    char overlay[32];
    snprintf(overlay, sizeof(overlay), "%.2f ms", frametime);
    ImGui::PlotLines("##frametime", m_FrametimeHistory, FRAMETIME_HISTORY_SIZE,
      m_FrametimeOffset, overlay, 0.0f, m_MaxFrametime * 1.2f, ImVec2(0, 80));

    // Render Stats section
    ImGui::Separator();
    if (ImGui::CollapsingHeader(ICON_FA_CHART_BAR " Render Stats", ImGuiTreeNodeFlags_DefaultOpen))
    {
      if (context.render)
      {
        auto& stats = context.render->GetStats();
        ImGui::Text("Draw Calls: %u", stats.drawCalls);
        ImGui::Text("Triangles: %u", stats.triangles);
        ImGui::Text("Vertices: %u", stats.vertices);
      }
      else
      {
        ImGui::TextDisabled("N/A");
      }
    }

    ImGui::End();
  }
}
