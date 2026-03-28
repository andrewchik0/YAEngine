#pragma once

#include "Pch.h"
#include "Events.h"
#include "EventBus.h"

namespace YAEngine
{
  using ActionId = uint32_t;
  static constexpr ActionId INVALID_ACTION = UINT32_MAX;

  class InputActionSystem
  {
  public:
    ActionId RegisterAction(const std::string& name)
    {
      auto it = m_NameToId.find(name);
      if (it != m_NameToId.end())
        return it->second;

      ActionId id = m_NextActionId++;
      m_NameToId[name] = id;
      return id;
    }

    ActionId GetActionId(const std::string& name) const
    {
      auto it = m_NameToId.find(name);
      return it != m_NameToId.end() ? it->second : INVALID_ACTION;
    }

    void MapAction(ActionId action, int key)
    {
      m_KeyToActions[key].push_back(action);
    }

    ActionId MapAction(const std::string& name, int key)
    {
      ActionId id = RegisterAction(name);
      MapAction(id, key);
      return id;
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

    bool IsActive(ActionId action) const
    {
      auto it = m_ActionStates.find(action);
      return it != m_ActionStates.end() && it->second;
    }

    bool WasJustPressed(ActionId action) const
    {
      auto it = m_JustPressed.find(action);
      return it != m_JustPressed.end() && it->second;
    }

    bool WasJustReleased(ActionId action) const
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
      auto it = m_KeyToActions.find(static_cast<int>(e.key));
      if (it == m_KeyToActions.end()) return;

      for (ActionId action : it->second)
      {
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

    std::unordered_map<std::string, ActionId> m_NameToId;
    std::unordered_map<int, std::vector<ActionId>> m_KeyToActions;
    std::unordered_map<ActionId, bool> m_ActionStates;
    std::unordered_map<ActionId, bool> m_JustPressed;
    std::unordered_map<ActionId, bool> m_JustReleased;
    ActionId m_NextActionId = 0;
    EventBus* m_Bus = nullptr;
    SubscriptionId m_Subscription {};
  };
}
