#pragma once

#include "Pch.h"
#include "Utils/KeyCodes.h"

namespace YAEngine
{
  class InputSystem;

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

    void MapAction(ActionId action, Key key)
    {
      m_KeyToActions[static_cast<int32_t>(key)].push_back(action);
    }

    ActionId MapAction(const std::string& name, Key key)
    {
      ActionId id = RegisterAction(name);
      MapAction(id, key);
      return id;
    }

    void Update(const InputSystem& input);

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

  private:
    std::unordered_map<std::string, ActionId> m_NameToId;
    std::unordered_map<int32_t, std::vector<ActionId>> m_KeyToActions;
    std::unordered_map<ActionId, bool> m_ActionStates;
    std::unordered_map<ActionId, bool> m_JustPressed;
    std::unordered_map<ActionId, bool> m_JustReleased;
    ActionId m_NextActionId = 0;
  };
}
