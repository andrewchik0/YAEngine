#include "TileLightBuffer.h"

#include "RenderContext.h"

namespace YAEngine
{
  void TileLightBuffer::Init(const RenderContext& ctx, uint32_t tileCountX, uint32_t tileCountY)
  {
    m_TileCountX = tileCountX;
    m_TileCountY = tileCountY;

    m_DescriptorSets.resize(ctx.maxFramesInFlight);
    m_StorageBuffers.resize(ctx.maxFramesInFlight);

    CreateBuffers(ctx);
  }

  void TileLightBuffer::Resize(const RenderContext& ctx, uint32_t tileCountX, uint32_t tileCountY)
  {
    if (tileCountX == m_TileCountX && tileCountY == m_TileCountY)
      return;

    DestroyBuffers(ctx);

    m_TileCountX = tileCountX;
    m_TileCountY = tileCountY;

    CreateBuffers(ctx);
  }

  void TileLightBuffer::Destroy(const RenderContext& ctx)
  {
    DestroyBuffers(ctx);
  }

  void TileLightBuffer::CreateBuffers(const RenderContext& ctx)
  {
    uint32_t tileCount = m_TileCountX * m_TileCountY;
    VkDeviceSize bufferSize = tileCount * sizeof(TileData);

    VkDescriptorSetLayout layout = nullptr;
    for (size_t i = 0; i < ctx.maxFramesInFlight; i++)
    {
      SetDescription desc = {
        .set = 0,
        .bindings = {
          {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT }
          }
        }
      };
      if (i == 0)
      {
        m_DescriptorSets[i].Init(ctx, desc);
        layout = m_DescriptorSets[i].GetLayout();
      }
      else
      {
        m_DescriptorSets[i].Init(ctx, layout);
      }
      m_StorageBuffers[i].Create(ctx, bufferSize);
      m_DescriptorSets[i].WriteStorageBuffer(0, m_StorageBuffers[i].Get(), bufferSize);
    }
  }

  void TileLightBuffer::DestroyBuffers(const RenderContext& ctx)
  {
    for (auto& set : m_DescriptorSets)
      set.Destroy();
    for (auto& ssbo : m_StorageBuffers)
      ssbo.Destroy(ctx);
  }
}
