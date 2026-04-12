#pragma once

#include "Handle.h"
#include "SlotMap.h"

namespace YAEngine
{
  template<typename T, typename Tag>
  class AssetManagerBase
  {
  public:
    using HandleType = Handle<Tag>;

    T& Get(HandleType handle)
    {
      T* ptr = m_Assets.Get(handle.index, handle.generation);
      assert(ptr != nullptr);
      return *ptr;
    }

    bool Has(HandleType handle) const
    {
      return m_Assets.Has(handle.index, handle.generation);
    }

    T* TryGet(HandleType handle)
    {
      return m_Assets.Get(handle.index, handle.generation);
    }

    template<typename Fn>
    void ForEach(Fn&& fn)
    {
      m_Assets.ForEach(std::forward<Fn>(fn));
    }

    template<typename Fn>
    void ForEachWithHandle(Fn&& fn)
    {
      m_Assets.ForEachWithKey([&](auto key, T& value)
      {
        fn(HandleType{ key.index, key.generation }, value);
      });
    }

    uint32_t Size() const { return m_Assets.Size(); }

  protected:

    HandleType Store(std::unique_ptr<T> asset)
    {
      auto key = m_Assets.Insert(std::move(asset));
      return { key.index, key.generation };
    }

    void Remove(HandleType handle)
    {
      m_Assets.Remove(handle.index, handle.generation);
    }

    void Clear()
    {
      m_Assets.Clear();
    }

  private:
    SlotMap<T> m_Assets;
  };
}
