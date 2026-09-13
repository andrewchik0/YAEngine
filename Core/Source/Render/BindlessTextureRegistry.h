#pragma once

#include "Pch.h"
#include "VulkanDescriptorPool.h"
#include "VulkanDescriptorSet.h"
#include "VulkanTexture.h"

namespace YAEngine
{
  struct RenderContext;

  // The engine's one bindless texture table: a single global descriptor set holding a
  // runtime-sized array of combined image samplers that every loaded 2D texture claims a
  // slot in. A hit shader has no descriptor set of its own to bind a material's maps
  // through, so it reaches them by index instead - which is what the material record
  // carries.
  //
  // Deliberately NOT per frame in flight. The array is written while it is bound to
  // command buffers still in flight, and UPDATE_AFTER_BIND is exactly the promise that
  // makes that legal; double buffering it would only cost a second copy of every write.
  //
  // Owned by RenderBackend and published as RenderContext::bindlessTextures, the same way
  // the geometry arena is: it has to exist before the first pipeline layout is built, and
  // both the asset layer that fills it and the passes that read it already hold a context.
  //
  // Thread safety: none, and none is needed. Texture decode is parallel, but every GPU
  // upload - and therefore every Register and Release - runs on the main thread (see
  // SceneSerializer's parallel loader, which joins its decode futures before it uploads).
  class BindlessTextureRegistry
  {
  public:

    // Slot 0 is a 1x1 white texture bound for the lifetime of the registry, so a material
    // whose map is missing, still loading or already destroyed resolves to a fetch that
    // multiplies by one instead of to an index nothing wrote.
    static constexpr uint32_t FALLBACK_INDEX = 0;

    // Two textures per material across a few thousand materials with room to spare, at
    // 4096 descriptors of a few bytes each. Clamped to what the device allows.
    static constexpr uint32_t DEFAULT_CAPACITY = 4096;

    // Set index every ray tracing pipeline binds the table at. Set 0 is the frame
    // uniforms, set 1 the per-frame ray tracing scene, and this one never changes
    // between frames, so it is bound once and left alone.
    static constexpr uint32_t BINDLESS_TEXTURE_SET = 2;

    // Does nothing unless the device granted both the descriptor indexing set and ray
    // tracing: the table exists to serve hit shaders, and nothing else reads it.
    void Init(const RenderContext& ctx);
    void Destroy(const RenderContext& ctx);

    bool IsValid() const { return b_Valid; }

    VkDescriptorSet GetSet() const { return m_Set.Get(); }
    VkDescriptorSetLayout GetLayout() const { return m_Set.GetLayout(); }

    uint32_t GetCapacity() const { return m_Capacity; }

    // Claims a slot, writes the descriptor immediately, and returns the index the shader
    // samples with. FALLBACK_INDEX when the registry is off or the table is full.
    uint32_t Register(VkImageView view, VkSampler sampler);

    // Returns a slot to the free list. The descriptor is left pointing at the retired view
    // rather than rewritten: PARTIALLY_BOUND makes an unwritten slot legal only as long as
    // nothing samples it, and nothing does - an index only reaches a shader through a
    // material record, which is rebuilt from live texture handles every frame.
    void Release(uint32_t index);

    // Every slot but the fallback goes back to the free list. For a scene teardown, which
    // destroys the whole texture manager at once.
    void ReleaseAll();

  private:

    void WriteSlot(uint32_t index, VkImageView view, VkSampler sampler);

    VulkanDescriptorPool m_Pool;
    VulkanDescriptorSet m_Set;
    // The registry's own fallback rather than Render::m_NoneTexture: slot 0 has to be
    // bound before any consumer exists, and the backend that owns the table is built long
    // before the renderer's placeholder is. One 1x1 image is a cheap price for not
    // inverting that dependency.
    VulkanTexture m_Fallback;

    uint32_t m_Capacity = 0;
    // Bump frontier over the slots the free list has never held. Starts past the fallback.
    uint32_t m_NextSlot = FALLBACK_INDEX + 1;
    std::vector<uint32_t> m_FreeList;

    bool b_Valid = false;
    bool b_OverflowWarned = false;
  };
}
