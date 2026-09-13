#include "BindlessTextureRegistry.h"

#include "RenderContext.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    constexpr VkDescriptorBindingFlags TABLE_BINDING_FLAGS =
      // Written while the set is bound to command buffers still in flight. This bit is the
      // whole reason the descriptor indexing feature set is requested at device creation.
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
      // Slots between the ones textures happen to hold are never written, and sampling one
      // would be undefined without this.
      | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      // Lets the allocation decide the real array size, so the clamp against the device
      // limits below costs nothing when it does not bite.
      | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
  }

  void BindlessTextureRegistry::Init(const RenderContext& ctx)
  {
    if (!ctx.bindlessSupported || !ctx.raytracingSupported)
      return;

    // Both limits bound the same array: one is what a set may hold, the other what a single
    // stage of it may reach, and the table is visible from every stage.
    uint32_t capacity = std::min(DEFAULT_CAPACITY,
      std::min(ctx.maxUpdateAfterBindSampledImages, ctx.maxPerStageUpdateAfterBindSampledImages));

    if (capacity < DEFAULT_CAPACITY)
    {
      YA_LOG_WARN("Vulkan",
        "Bindless texture table clamped to %u slots by the device limits (set %u, per stage %u)",
        capacity, ctx.maxUpdateAfterBindSampledImages, ctx.maxPerStageUpdateAfterBindSampledImages);
    }

    // One slot holds nothing but the fallback, which leaves no table at all.
    if (capacity < 2)
    {
      YA_LOG_ERROR("Vulkan", "Bindless texture table cannot be created: the device allows only %u slots",
        capacity);
      return;
    }

    m_Capacity = capacity;
    m_Pool.InitUpdateAfterBind(ctx.device, m_Capacity);

    const SetDescription setDescription {
      .set = BINDLESS_TEXTURE_SET,
      .bindings = {
        {
          .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          // Reached from compute today and from the ray tracing stages next, and a layout
          // is not worth a permutation per consumer.
          .stages = VK_SHADER_STAGE_ALL,
          .count = m_Capacity,
          .flags = TABLE_BINDING_FLAGS,
        }
      }
    };
    m_Set.Init(ctx, setDescription, m_Pool, m_Capacity);

    uint32_t whitePixel = 0xFFFFFFFF;
    // sRGB to match what a base color map is loaded as, so a material with no texture
    // multiplies by exactly one rather than by a gamma-shifted white.
    m_Fallback.Load(ctx, &whitePixel, 1, 1, 4, VK_FORMAT_R8G8B8A8_SRGB);

    b_Valid = true;
    WriteSlot(FALLBACK_INDEX, m_Fallback.GetView(), m_Fallback.GetSampler());

    YA_LOG_INFO("Vulkan", "Bindless texture table created with %u slots", m_Capacity);
  }

  void BindlessTextureRegistry::Destroy(const RenderContext& ctx)
  {
    if (!b_Valid)
      return;

    m_Fallback.Destroy(ctx);
    // The layout belongs to the context's cache; the set dies with the pool.
    m_Set.Destroy();
    m_Pool.Destroy();

    m_FreeList.clear();
    m_NextSlot = FALLBACK_INDEX + 1;
    m_Capacity = 0;
    b_Valid = false;
  }

  void BindlessTextureRegistry::WriteSlot(uint32_t index, VkImageView view, VkSampler sampler)
  {
    m_Set.WriteCombinedImageSampler(0, view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, index);
  }

  uint32_t BindlessTextureRegistry::Register(VkImageView view, VkSampler sampler)
  {
    if (!b_Valid || view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
      return FALLBACK_INDEX;

    uint32_t index;
    if (!m_FreeList.empty())
    {
      index = m_FreeList.back();
      m_FreeList.pop_back();
    }
    else if (m_NextSlot < m_Capacity)
    {
      index = m_NextSlot++;
    }
    else
    {
      if (!b_OverflowWarned)
      {
        b_OverflowWarned = true;
        YA_LOG_WARN("Vulkan",
          "Bindless texture table is full at %u slots, further textures fall back to white",
          m_Capacity);
      }
      return FALLBACK_INDEX;
    }

    WriteSlot(index, view, sampler);
    return index;
  }

  void BindlessTextureRegistry::Release(uint32_t index)
  {
    if (!b_Valid || index == FALLBACK_INDEX || index >= m_Capacity)
      return;

    m_FreeList.push_back(index);
  }

  void BindlessTextureRegistry::ReleaseAll()
  {
    if (!b_Valid)
      return;

    m_FreeList.clear();
    m_NextSlot = FALLBACK_INDEX + 1;
  }
}
