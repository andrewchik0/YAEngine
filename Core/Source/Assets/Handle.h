#pragma once

#include "Pch.h"

namespace YAEngine
{
  template<typename Tag>
  struct Handle
  {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    static Handle Invalid() { return {}; }

    explicit operator bool() const { return index != UINT32_MAX; }

    bool operator==(const Handle&) const = default;
    bool operator!=(const Handle&) const = default;
  };

  struct TextureTag {};
  struct MeshTag {};
  struct MaterialTag {};
  struct ModelTag {};
  struct CubeMapTag {};

  using TextureHandle = Handle<TextureTag>;
  using MeshHandle = Handle<MeshTag>;
  using MaterialHandle = Handle<MaterialTag>;
  using ModelHandle = Handle<ModelTag>;
  using CubeMapHandle = Handle<CubeMapTag>;
}
