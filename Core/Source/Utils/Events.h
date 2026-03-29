#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct KeyEvent
  {
    int32_t key;
    int32_t scancode;
    int32_t action;
    int32_t mods;
  };

  struct MouseMovedEvent
  {
    double x, y;
  };

  struct MouseWheelEvent
  {
    double x, y;
  };

  struct MouseButtonEvent
  {
    int32_t button;
    int32_t action;
    int32_t mods;
  };

  using WindowEvent = std::variant<KeyEvent, MouseMovedEvent, MouseButtonEvent, MouseWheelEvent>;
}
