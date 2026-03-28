#pragma once

#include "Pch.h"
#include "Events.h"
#include "EventBus.h"

namespace YAEngine
{
  class InputActionSystem
  {
  public:
    void MapAction(const std::string& action, int key)
    {
      m_ActionKeys[action].push_back(key);
    }

    void Connect(EventBus& bus)
    {
      m_Subscription = bus.Subscribe<KeyEvent>([this](const KeyEvent& e)
      {
        OnKeyEvent(e);
      });
      m_Bus = &bus;
    }

    void Disconnect()
    {
      if (m_Bus)
      {
        m_Bus->Unsubscribe<KeyEvent>(m_Subscription);
        m_Bus = nullptr;
      }
    }

    bool IsActive(const std::string& action) const
    {
      auto it = m_ActionStates.find(action);
      return it != m_ActionStates.end() && it->second;
    }

    bool WasJustPressed(const std::string& action) const
    {
      auto it = m_JustPressed.find(action);
      return it != m_JustPressed.end() && it->second;
    }

    bool WasJustReleased(const std::string& action) const
    {
      auto it = m_JustReleased.find(action);
      return it != m_JustReleased.end() && it->second;
    }

    void ResetFrameState()
    {
      for (auto& [action, state] : m_JustPressed) state = false;
      for (auto& [action, state] : m_JustReleased) state = false;
    }

  private:
    void OnKeyEvent(const KeyEvent& e)
    {
      for (auto& [action, keys] : m_ActionKeys)
      {
        for (int key : keys)
        {
          if (static_cast<int>(e.key) != key) continue;

          if (e.action == GLFW_PRESS)
          {
            m_ActionStates[action] = true;
            m_JustPressed[action] = true;
          }
          else if (e.action == GLFW_RELEASE)
          {
            m_ActionStates[action] = false;
            m_JustReleased[action] = true;
          }
        }
      }
    }

    std::unordered_map<std::string, std::vector<int>> m_ActionKeys;
    std::unordered_map<std::string, bool> m_ActionStates;
    std::unordered_map<std::string, bool> m_JustPressed;
    std::unordered_map<std::string, bool> m_JustReleased;
    EventBus* m_Bus = nullptr;
    SubscriptionId m_Subscription {};
  };
}
