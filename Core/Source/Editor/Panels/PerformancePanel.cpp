#include "Editor/Panels/PerformancePanel.h"

#include <imgui.h>

#include "Application.h"
#include "Editor/EditorContext.h"
#include "Render/Render.h"

namespace YAEngine
{
  void PerformancePanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("Performance"))
    {
      ImGui::End();
      return;
    }

    auto& timer = Application::Get().GetTimer();

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

    // FPS / ms toggle
    ImGui::Checkbox("Show FPS", &b_ShowFPS);
    ImGui::SameLine();

    if (b_ShowFPS)
    {
      ImGui::Text("FPS: %.1f", m_DisplayFPS);
      ImGui::Text("Min: %.1f  Max: %.1f  Avg: %.1f",
        m_MaxFrametime > 0.0f ? 1000.0f / m_MaxFrametime : 0.0f,
        m_MinFrametime > 0.0f ? 1000.0f / m_MinFrametime : 0.0f,
        m_AvgFrametime > 0.0f ? 1000.0f / m_AvgFrametime : 0.0f);
    }
    else
    {
      ImGui::Text("Frame: %.2f ms", frametime);
      ImGui::Text("Min: %.2f ms  Max: %.2f ms  Avg: %.2f ms",
        m_MinFrametime, m_MaxFrametime, m_AvgFrametime);
    }

    // Graph
    char overlay[32];
    if (b_ShowFPS)
      snprintf(overlay, sizeof(overlay), "%.1f FPS", m_DisplayFPS);
    else
      snprintf(overlay, sizeof(overlay), "%.2f ms", frametime);

    if (b_ShowFPS)
    {
      float maxVal = 0.0f;
      for (int i = 0; i < FRAMETIME_HISTORY_SIZE; i++)
      {
        m_FPSHistory[i] = m_FrametimeHistory[i] > 0.0f ? 1000.0f / m_FrametimeHistory[i] : 0.0f;
        if (m_FPSHistory[i] > maxVal) maxVal = m_FPSHistory[i];
      }
      ImGui::PlotLines("##frametime", m_FPSHistory, FRAMETIME_HISTORY_SIZE,
        m_FrametimeOffset, overlay, 0.0f, maxVal * 1.2f, ImVec2(0, 80));
    }
    else
    {
      ImGui::PlotLines("##frametime", m_FrametimeHistory, FRAMETIME_HISTORY_SIZE,
        m_FrametimeOffset, overlay, 0.0f, m_MaxFrametime * 1.2f, ImVec2(0, 80));
    }

    // Render Stats section
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Render Stats", ImGuiTreeNodeFlags_DefaultOpen))
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
