#pragma once

#include <vector>
#include <memory>
#include "Layer.h"

namespace YAEngine
{
  class LayerManager
  {
  public:
    template<typename T>
    requires std::is_base_of<Layer, T>::value
    void PushLayer()
    {
      m_LayerStack.push_back(std::make_unique<T>());
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

    void CallUpdate(double dt)
    {
      for (auto& layer : m_LayerStack)
        layer->Update(dt);
    }

    void CallRenderUI()
    {
      for (auto& layer : m_LayerStack)
        layer->RenderUI();
    }

    void CallOnDetach()
    {
      for (auto& layer : m_LayerStack)
        layer->OnDetach();
    }

  private:
    std::vector<std::unique_ptr<Layer>> m_LayerStack;
  };
}
