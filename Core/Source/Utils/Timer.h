#pragma once

#include "Pch.h"

namespace YAEngine
{
  class Timer
  {
  public:
    void Step()
    {
      m_CurrentTime = glfwGetTime();
      if (m_LastTime == 0.0)
        m_LastTime = m_CurrentTime;
      m_DeltaTime = m_CurrentTime - m_LastTime;
      m_LastTime = m_CurrentTime;
    }

    double GetDeltaTime() const
    {
      return m_DeltaTime;
    }

    double GetTime() const
    {
      return m_CurrentTime;
    }

    float GetFPS() const
    {
      return m_DeltaTime > 0.0 ? 1.0f / static_cast<float>(m_DeltaTime) : 0.0f;
    }

  private:
    double m_CurrentTime = 0, m_DeltaTime = 0, m_LastTime = 0;
  };
}