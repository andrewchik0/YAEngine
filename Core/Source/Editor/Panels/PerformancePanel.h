#pragma once

#include "Editor/IEditorPanel.h"

namespace YAEngine
{
  class PerformancePanel : public IEditorPanel
  {
  public:

    const char* GetName() const override { return "Performance"; }
    void OnRender(EditorContext& context) override;

  private:

    static constexpr int FRAMETIME_HISTORY_SIZE = 300;
    float m_FrametimeHistory[FRAMETIME_HISTORY_SIZE] = {};
    int m_FrametimeOffset = 0;

    float m_UpdateTimer = 0.0f;
    float m_DisplayFPS = 0.0f;
    float m_MinFrametime = 0.0f;
    float m_MaxFrametime = 0.0f;
    float m_AvgFrametime = 0.0f;

  };
}
