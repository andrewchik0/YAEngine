#pragma once

#include "Pch.h"
#include "Layer.h"

namespace YAEngine
{
  class LayerManager
  {
  public:

    void SetRegistry(ServiceRegistry& registry) { m_Registry = &registry; }

    template<typename T>
    requires std::is_base_of<Layer, T>::value
    void PushLayer()
    {
      auto layer = std::make_unique<T>();
      if (m_Registry)
        layer->SetRegistry(*m_Registry);
      m_LayerStack.push_back(std::move(layer));
    }

    template<typename T>
    requires std::is_base_of<Layer, T>::value
    T* GetLayer()
    {
      for (const auto& layer : m_LayerStack)
      {
        if (auto casted = dynamic_cast<T*>(layer.get()))
          return casted;
      }
      return nullptr;
    }

    void CallOnAttach()
    {
      for (size_t i = 0; i < m_LayerStack.size(); i++)
        m_LayerStack[i]->OnAttach();
    }

    void CallOnSceneReady()
    {
      for (size_t i = 0; i < m_LayerStack.size(); i++)
        m_LayerStack[i]->OnSceneReady();
    }

    void CallFixedUpdate(double fixedDt)
    {
      for (auto& layer : m_LayerStack)
        layer->FixedUpdate(fixedDt);
    }

    void CallUpdate(double dt)
    {
      for (auto& layer : m_LayerStack)
        layer->Update(dt);
    }

    void CallLateUpdate(double dt)
    {
      for (auto& layer : m_LayerStack)
        layer->LateUpdate(dt);
    }

    void CallRenderUI()
    {
      for (auto& layer : m_LayerStack)
        layer->RenderUI();
    }

    void CallDebugDrawGizmos()
    {
      for (auto& layer : m_LayerStack)
        layer->DebugDrawGizmos();
    }

    void CallOnDetach()
    {
      for (auto& layer : m_LayerStack)
        layer->OnDetach();
    }

  private:
    ServiceRegistry* m_Registry = nullptr;
    std::vector<std::unique_ptr<Layer>> m_LayerStack;
  };
}
