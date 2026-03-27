#pragma once

#include "Assets/Handle.h"

#include <vulkan/vulkan.h>

namespace YAEngine
{
  class AssetManager;

  class EditorTextureCache
  {
  public:

    void Init(AssetManager* assetManager);
    void Destroy();

    VkDescriptorSet GetOrRegister(TextureHandle handle);
    void Invalidate(TextureHandle handle);

  private:

    struct HandleHash
    {
      size_t operator()(TextureHandle h) const
      {
        return std::hash<uint64_t>{}(
          (static_cast<uint64_t>(h.index) << 32) | h.generation);
      }
    };

    AssetManager* m_AssetManager = nullptr;
    std::unordered_map<TextureHandle, VkDescriptorSet, HandleHash> m_Cache;
  };
}
