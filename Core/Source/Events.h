#pragma once

namespace YAEngine
{
  enum class EventType
  {
    None = 0,
    MouseMoved,
    MouseButton,
    MouseScroll,
    Key,
    Resize
  };

  class Event
  {
  public:
    virtual ~Event() = default;

    EventType type = EventType::None;
  };

  class MouseMovedEvent : public Event
  {
  public:
    double x, y;

    MouseMovedEvent(double x, double y)
      : x(x), y(y)
    {
      type = EventType::MouseMoved;
    }
  };

  class MouseWheelEvent : public Event
  {
  public:
    double x, y;

    MouseWheelEvent(double x, double y)
      : x(x), y(y)
    {
      type = EventType::MouseScroll;
    }
  };

  class MouseButtonEvent : public Event
  {
  public:
    uint32_t button, action, mods;

    MouseButtonEvent(uint32_t button, uint32_t action, uint32_t mods)
      : button(button), action(action), mods(mods)
    {
      type = EventType::MouseButton;
    }
  };

  class KeyEvent : public Event
  {
  public:
    uint32_t key, scancode, action, mods;

    KeyEvent(uint32_t key, uint32_t scancode, uint32_t action, uint32_t mods)
      : key(key), scancode(scancode), action(action), mods(mods)
    {
      type = EventType::Key;
    }
  };

  class  ResizeEvent : public Event
  {
  public:
    uint32_t width, height;

    ResizeEvent(uint32_t width, uint32_t height)
      : width(width), height(height)
    {
      type = EventType::Resize;
    }

  };


}