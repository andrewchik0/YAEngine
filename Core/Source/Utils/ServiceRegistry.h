#pragma once

#include "Pch.h"
#include "Utils/Log.h"

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
      if (it == m_Services.end())
      {
        YA_LOG_ERROR("ServiceRegistry", "Service not registered: %s", typeid(T).name());
        throw std::runtime_error("Service not registered");
      }
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
