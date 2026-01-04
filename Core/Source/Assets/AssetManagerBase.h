#pragma once

#include "Pch.h"
#include "Utils/Random.h"

namespace YAEngine
{
  class Asset
  {
  public:
    virtual ~Asset() = default;
  };

  using AssetHandle = uint32_t;

  template<typename T>
  class AssetManagerBase
  {
  public:

    T& Get(AssetHandle handle)
    {
      return *m_Assets.at(handle);
    }

    bool Has(AssetHandle handle)
    {
      return m_Assets.contains(handle);
    }

    std::unordered_map<AssetHandle, std::unique_ptr<T>>& GetAll()
    {
      return m_Assets;
    }

    AssetHandle Load(std::unique_ptr<T> asset)
    {
      AssetHandle newHandle = random<uint32_t>();
      m_Assets.emplace(newHandle, std::move(asset));
      return newHandle;
    }

    void Remove(AssetHandle handle)
    {
      m_Assets.erase(handle);
    }

  private:
    std::unordered_map<AssetHandle, std::unique_ptr<T>> m_Assets;

  };
}
