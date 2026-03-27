#pragma once

#include "Pch.h"

namespace YAEngine
{
  template<typename T>
  class SlotMap
  {
  public:

    struct Key
    {
      uint32_t index;
      uint32_t generation;
    };

    Key Insert(std::unique_ptr<T> value)
    {
      uint32_t idx;
      if (!m_FreeList.empty())
      {
        idx = m_FreeList.back();
        m_FreeList.pop_back();
      }
      else
      {
        idx = static_cast<uint32_t>(m_Slots.size());
        m_Slots.emplace_back();
      }

      auto& slot = m_Slots[idx];
      slot.data = std::move(value);
      m_Count++;

      return { idx, slot.generation };
    }

    bool Remove(uint32_t index, uint32_t generation)
    {
      if (index >= m_Slots.size()) return false;
      auto& slot = m_Slots[index];
      if (slot.generation != generation || !slot.data) return false;

      slot.data.reset();
      slot.generation++;
      if (slot.generation == 0)
        slot.generation = 1;
      m_FreeList.push_back(index);
      m_Count--;
      return true;
    }

    T* Get(uint32_t index, uint32_t generation)
    {
      if (index >= m_Slots.size()) return nullptr;
      auto& slot = m_Slots[index];
      if (slot.generation != generation || !slot.data) return nullptr;
      return slot.data.get();
    }

    const T* Get(uint32_t index, uint32_t generation) const
    {
      if (index >= m_Slots.size()) return nullptr;
      auto& slot = m_Slots[index];
      if (slot.generation != generation || !slot.data) return nullptr;
      return slot.data.get();
    }

    bool Has(uint32_t index, uint32_t generation) const
    {
      if (index >= m_Slots.size()) return false;
      auto& slot = m_Slots[index];
      return slot.generation == generation && slot.data != nullptr;
    }

    template<typename Fn>
    void ForEach(Fn&& fn)
    {
      for (auto& slot : m_Slots)
      {
        if (slot.data)
          fn(*slot.data);
      }
    }

    template<typename Fn>
    void ForEachWithKey(Fn&& fn)
    {
      for (uint32_t i = 0; i < static_cast<uint32_t>(m_Slots.size()); ++i)
      {
        auto& slot = m_Slots[i];
        if (slot.data)
          fn(Key{ i, slot.generation }, *slot.data);
      }
    }

    void Clear()
    {
      m_Slots.clear();
      m_FreeList.clear();
      m_Count = 0;
    }

    uint32_t Size() const { return m_Count; }

  private:

    struct Slot
    {
      std::unique_ptr<T> data;
      uint32_t generation = 1;
    };

    std::vector<Slot> m_Slots;
    std::vector<uint32_t> m_FreeList;
    uint32_t m_Count = 0;
  };
}
