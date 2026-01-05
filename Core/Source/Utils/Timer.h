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
      return 1.0f / m_DeltaTime;
    }

  private:
    double m_CurrentTime = 0, m_DeltaTime = 0, m_LastTime = 0;
  };
}