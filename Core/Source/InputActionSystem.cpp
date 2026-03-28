#include "InputActionSystem.h"
#include "InputSystem.h"

namespace YAEngine
{
  void InputActionSystem::Update(const InputSystem& input)
  {
    for (auto& [_, state] : m_JustPressed) state = false;
    for (auto& [_, state] : m_JustReleased) state = false;

    for (auto& [keyCode, actions] : m_KeyToActions)
    {
      Key key = static_cast<Key>(keyCode);
      for (ActionId action : actions)
      {
        if (input.IsKeyPressed(key))
        {
          m_ActionStates[action] = true;
          m_JustPressed[action] = true;
        }
        if (input.IsKeyReleased(key))
        {
          m_ActionStates[action] = false;
          m_JustReleased[action] = true;
        }
      }
    }
  }
}
