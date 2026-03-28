#pragma once

#include "Pch.h"

namespace YAEngine
{
  class ServiceRegistry
  {
  public:
    template<typename T>
    void Register(T* service)
    {
      m_Services[typeid(T)] = static_cast<void*>(service);
    }

    template<typename T>
    T& Get() const
    {
      auto it = m_Services.find(typeid(T));
      assert(it != m_Services.end());
      return *static_cast<T*>(it->second);
    }

    template<typename T>
    bool Has() const
    {
      return m_Services.contains(typeid(T));
    }

  private:
    std::unordered_map<std::type_index, void*> m_Services;
  };
}
