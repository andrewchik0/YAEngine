#ifdef YA_EDITOR

#include "Render.h"

#include <ImGui/imgui_impl_vulkan.h>

#include "TileCullData.h"
#include "VulkanCommandBuffer.h"
#include "Assets/AssetManager.h"
#include "Assets/IrradianceVolumeFile.h"
#include "Scene/Scene.h"
#include "Scene/SceneSnapshot.h"
#include "Scene/SceneSerializer.h"
#include "Utils/IrradianceGrid.h"
#include "Utils/Log.h"
#include "Utils/Timer.h"

namespace YAEngine
{
  void Render::CreateSceneImGuiDescriptor()
  {
    auto& sceneImage = m_Graph.GetResource(m_SceneColor);
    m_SceneImGuiDescriptor = ImGui_ImplVulkan_AddTexture(
      sceneImage.GetSampler(),
      sceneImage.GetView(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }

  void Render::DestroySceneImGuiDescriptor()
  {
    if (m_SceneImGuiDescriptor != VK_NULL_HANDLE)
    {
      ImGui_ImplVulkan_RemoveTexture(m_SceneImGuiDescriptor);
      m_SceneImGuiDescriptor = VK_NULL_HANDLE;
    }
  }

  void Render::RequestViewportResize(uint32_t w, uint32_t h)
  {
    m_PendingViewportWidth = w;
    m_PendingViewportHeight = h;
  }

  void Render::InitShaderHotReload(ThreadPool* threadPool)
  {
    m_ShaderHotReload.Init(&m_PSOCache, m_Backend.GetContext().device, threadPool);
  }

  namespace
  {
    // Deliberately not using TransitionImageLayout: the image arrives in whatever layout
    // the graph left it in, and the pick copy has to hand it back unchanged so the
    // graph's own layout tracking stays truthful.
    void PickCopyBarrier(VkCommandBuffer cmd, VkImage image,
                         VkImageLayout oldLayout, VkImageLayout newLayout)
    {
      VkImageMemoryBarrier barrier{};
      barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier.oldLayout = oldLayout;
      barrier.newLayout = newLayout;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = image;
      barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
      barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

      vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
  }

  void Render::CreatePickResources()
  {
    auto& ctx = m_Backend.GetContext();
    m_PickSlots.resize(ctx.maxFramesInFlight);
    for (auto& slot : m_PickSlots)
      slot.buffer = VulkanBuffer::CreateReadback(ctx, sizeof(uint32_t));
  }

  void Render::DestroyPickResources()
  {
    auto& ctx = m_Backend.GetContext();
    for (auto& slot : m_PickSlots)
      slot.buffer.Destroy(ctx);
    m_PickSlots.clear();
  }

  void Render::RequestPick(const glm::vec2& normalizedPos)
  {
    m_PickRequestPos = normalizedPos;
    b_PickRequested = true;

    // Only the newest click may answer. A result still sitting from an older request, or
    // one whose copy is still in flight, would otherwise be handed to this click - two
    // clicks a couple of frames apart are enough for that to happen.
    b_PickResultReady = false;
    for (auto& slot : m_PickSlots)
      slot.pending = false;
  }

  void Render::BeginPickFrame()
  {
    b_PickThisFrame = false;

    if (!b_PickRequested)
      return;

    b_PickRequested = false;

    VkExtent2D extent = m_Graph.GetExtent();
    if (extent.width == 0 || extent.height == 0)
      return;

    auto& slot = m_PickSlots[m_Backend.GetCurrentFrameIndex()];
    float u = std::clamp(m_PickRequestPos.x, 0.0f, 1.0f);
    float v = std::clamp(m_PickRequestPos.y, 0.0f, 1.0f);
    slot.pixelX = std::min(uint32_t(u * float(extent.width)), extent.width - 1);
    slot.pixelY = std::min(uint32_t(v * float(extent.height)), extent.height - 1);
    slot.pending = true;
    b_PickThisFrame = true;
  }

  void Render::CopyPickId(VkCommandBuffer cmd)
  {
    auto& slot = m_PickSlots[m_Backend.GetCurrentFrameIndex()];
    auto& image = m_Graph.GetResource(m_PickId);

    VkImageLayout original = image.GetLayout();
    if (original == VK_IMAGE_LAYOUT_UNDEFINED)
      return;

    PickCopyBarrier(cmd, image.GetImage(), original, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { int32_t(slot.pixelX), int32_t(slot.pixelY), 0 };
    region.imageExtent = { 1, 1, 1 };
    vkCmdCopyImageToBuffer(cmd, image.GetImage(),
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.buffer.Get(), 1, &region);

    PickCopyBarrier(cmd, image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, original);
  }

  void Render::LatchPickResult()
  {
    auto& slot = m_PickSlots[m_Backend.GetCurrentFrameIndex()];
    if (!slot.pending)
      return;

    slot.pending = false;

    uint32_t raw = 0;
    std::memcpy(&raw, slot.buffer.GetMapped(), sizeof(uint32_t));

    // The target is cleared to zero and the pass stores id + 1, so zero is unambiguously
    // "nothing was rasterized into that pixel"
    m_PickResult = {};
    if (raw != 0)
    {
      m_PickResult.hit = true;
      m_PickResult.entityId = raw - 1;
    }
    b_PickResultReady = true;
  }

  void Render::ResizeViewport()
  {
    auto& ctx = m_Backend.GetContext();

    // Wait for all GPU work to complete before destroying resources
    vkDeviceWaitIdle(ctx.device);

    uint32_t w = m_PendingViewportWidth;
    uint32_t h = m_PendingViewportHeight;

    // Update HiZ mip count for new dimensions
    uint32_t hizMipCount = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
    m_Graph.SetResourceMipLevels(m_HiZResource, hizMipCount);

    DestroyBloomResources();
    DestroyHiZResources();
    DestroyGTAOResources();
    DestroySceneImGuiDescriptor();

    m_Graph.Resize({w, h});

    CreateSceneImGuiDescriptor();
    CreateHiZResources();
    CreateGTAOResources();
    CreateBloomResources();

    // Resize tile light buffer and update descriptor sets that reference it
    {
      uint32_t tileCountX = (w + TILE_SIZE - 1) / TILE_SIZE;
      uint32_t tileCountY = (h + TILE_SIZE - 1) / TILE_SIZE;
      m_TileLightBuffer.Resize(ctx, tileCountX, tileCountY);
      VkDeviceSize tileBufferSize = tileCountX * tileCountY * sizeof(TileData);
      for (size_t i = 0; i < m_Backend.GetMaxFramesInFlight(); i++)
      {
        m_DeferredLightingLightDescriptorSets[i].WriteStorageBuffer(1,
          m_TileLightBuffer.GetBuffer(uint32_t(i)), tileBufferSize);
      }
    }

    // Recreate TAA external framebuffers
    for (auto& fb : m_TAAFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    for (auto& fb : m_TransparentFramebuffers)
    {
      if (fb != VK_NULL_HANDLE)
      {
        vkDestroyFramebuffer(ctx.device, fb, nullptr);
        fb = VK_NULL_HANDLE;
      }
    }
    m_TAADepth.Destroy(ctx);
    CreateTAAFramebuffers();
    ClearHistoryBuffers();

    m_ViewportWidth = w;
    m_ViewportHeight = h;
  }

  bool Render::BakeIrradianceVolume(entt::entity entity, Scene& scene, AssetManager& assets,
    bool writeToDisk, IrradianceVolumeFileData* outData)
  {
    if (!scene.HasComponent<IrradianceVolumeComponent>(entity))
      return false;

    auto& ctx = m_Backend.GetContext();
    vkDeviceWaitIdle(ctx.device);

    Timer timer;
    timer.Step();

    auto& volume = scene.GetComponent<IrradianceVolumeComponent>(entity);
    auto& wt = scene.GetWorldTransform(entity);
    std::string entityName = scene.GetName(entity);

    glm::vec3 center = glm::vec3(wt.world[3]);
    glm::quat rotation = ExtractIrradianceBoxRotation(wt.world);

    IrradianceGridLayout layout = ComputeIrradianceGridLayout(center, rotation,
      volume.halfExtents, volume.spacing);
    uint32_t nodeCount = layout.GetNodeCount();

    YA_LOG_INFO("Render", "Volume '%s': %ux%ux%u = %u nodes, %u face renders",
      entityName.c_str(), layout.nodeCounts.x, layout.nodeCounts.y, layout.nodeCounts.z,
      nodeCount, layout.GetFaceRenderCount());

    // The details panel disables its own Bake button above this, but "Bake All
    // Volumes" does not go through the panel, and a hand-edited scene does not go
    // through either.
    if (nodeCount == 0 || nodeCount > BakeLimits::VOLUME_MAX_NODE_COUNT)
    {
      YA_LOG_ERROR("Render", "Volume '%s' has %u nodes, the limit is %u - raise the spacing or shrink the box",
        entityName.c_str(), nodeCount, BakeLimits::VOLUME_MAX_NODE_COUNT);
      return false;
    }

    uint32_t captureResolution = std::clamp(volume.captureResolution,
      BakeLimits::VOLUME_MIN_CAPTURE_RESOLUTION,
      BakeLimits::VOLUME_MAX_CAPTURE_RESOLUTION);

    SceneSnapshot snapshot;
    LightBuffer lights {};
    BuildSceneSnapshot(snapshot, lights, scene, assets.Meshes(), assets.Materials());

    FrameContext frame {
      .snapshot = snapshot,
      .lights = lights,
      .assets = assets,
      .cubicResources = m_CubicResources,
      .time = 0.0,
      .windowWidth = captureResolution,
      .windowHeight = captureResolution,
    };

    m_LightBuffer.SetUp(0, lights);

    // Reflection probes stay visible - they carry the ambient the capture is meant
    // to record.
    m_ProbeBuffer.SetUp(0, snapshot.probeBuffer);

    // Neighbouring volumes stay visible so their bounced light reaches this one.
    // Only the volume being rebaked is dropped, otherwise it would feed on its own
    // previous output and drift monotonically brighter with every pass.
    // Unlike probes, slot 0 is a real volume, so the "already in the atlas" test is
    // baked + a valid slot, not a non-zero slot.
    // Dropping one entry keeps the remaining order, and the array is sorted by
    // ascending box volume so nesting still resolves the way the shader expects.
    // Frame 0 is the slot OffscreenRenderer binds.
    {
      const IrradianceVolumeBuffer& allVolumes = m_VolumeStorage.GetBufferData();
      IrradianceVolumeBuffer bakeVolumes {};
      bakeVolumes.atlasInvSize = allVolumes.atlasInvSize;
      bakeVolumes.volumeCount = 0;
      for (int32_t i = 0; i < allVolumes.volumeCount; i++)
      {
        bool isSelf = volume.baked
          && volume.atlasSlot != IrradianceVolumeStorage::INVALID_SLOT
          && volume.atlasSlot == uint32_t(i);
        if (isSelf)
          continue;
        bakeVolumes.volumes[bakeVolumes.volumeCount++] = allVolumes.volumes[i];
      }
      m_VolumeStorage.SetUp(0, bakeVolumes);
    }

    // ONE shadow atlas render for the WHOLE volume. The cascades are fitted around
    // the volume center and inflated by its radius, so every node reuses them.
    // Fitting per node would mean nodeCount renders of the 8192x8192 atlas and
    // would dwarf everything else in the bake.
    // The radius comes from the LATTICE, not from the box: snapping to the world
    // lattice pushes the outermost nodes up to one spacing past the box AABB, and
    // a node outside the fitted cascades would capture unshadowed light.
    {
      glm::vec3 latticeMax = layout.GetWorldPosition(layout.nodeCounts.x - 1,
        layout.nodeCounts.y - 1, layout.nodeCounts.z - 1);
      float volumeRadius = glm::max(
        glm::length(latticeMax - center),
        glm::length(center - layout.latticeOrigin));
      VkCommandBuffer shadowCmd = m_Backend.GetCommandBuffer().BeginSingleTimeCommands();
      RenderShadowMaps(frame, shadowCmd, 0, &center, volumeRadius);
      m_Backend.GetCommandBuffer().EndSingleTimeCommands(shadowCmd);

      if (m_ShadowManager.IsEnabled())
        YA_LOG_INFO("Render", "Volume '%s': shadow atlas rendered once for %u nodes",
          entityName.c_str(), nodeCount);
    }

    IrradianceVolumeBakeDesc bakeDesc {
      .layout = &layout,
      .captureResolution = captureResolution,
      .colliderMask = ~0u,
      .backfaceRatioThreshold = volume.backfaceRatioThreshold,
      .volumeName = entityName.c_str(),
    };

    std::vector<SHL1RGB> coefficients;
    std::vector<uint8_t> validity;
    IrradianceVolumeBakeResult bakeResult = m_VolumeBaker.Bake(
      m_CubicResources, frame, scene, bakeDesc, coefficients, validity);

    if (!bakeResult.anyValid)
      YA_LOG_WARN("Render", "Volume '%s': every node was rejected, nothing was captured",
        entityName.c_str());

    FloodFillIrradianceNodes(layout, coefficients, validity);

    // Whatever happens from here on, the shader must see the real description again
    m_VolumeStorage.SetUp(0, m_VolumeStorage.GetBufferData());

    if (writeToDisk)
    {
      std::string probeDir = assets.GetBasePath() + "/Assets/Probes";
      std::filesystem::create_directories(probeDir);
      std::string volumePath = probeDir + "/" + entityName + "_volume.yaiv";

      IrradianceVolumeFileDesc fileDesc {
        .nodesX = layout.nodeCounts.x,
        .nodesY = layout.nodeCounts.y,
        .nodesZ = layout.nodeCounts.z,
        .spacing = layout.spacing,
        .latticeOrigin = layout.latticeOrigin,
        .position = center,
        .rotation = rotation,
        .halfExtents = layout.halfExtents,
        .coefficients = coefficients.data(),
        .validity = validity.data(),
        .nodeCount = nodeCount,
      };

      if (!IrradianceVolumeFile::Save(volumePath, fileDesc))
      {
        YA_LOG_ERROR("Render", "Volume '%s': failed to save %s",
          entityName.c_str(), volumePath.c_str());
        return false;
      }

      volume.bakedVolumePath = assets.MakeRelative(volumePath);
      volume.baked = true;
      YA_LOG_INFO("Render", "Saved irradiance volume: %s", volume.bakedVolumePath.c_str());
    }

    timer.Step();
    YA_LOG_INFO("Render", "Volume '%s' baked: %u nodes, %u rejected (%u in colliders, %u buried), %.2f s",
      entityName.c_str(), nodeCount, bakeResult.rejectedCount,
      bakeResult.rejectedByColliderCount, bakeResult.rejectedByBackfaceCount,
      timer.GetDeltaTime());

    if (outData)
    {
      *outData = IrradianceVolumeFileData {
        .nodesX = layout.nodeCounts.x,
        .nodesY = layout.nodeCounts.y,
        .nodesZ = layout.nodeCounts.z,
        .spacing = layout.spacing,
        .latticeOrigin = layout.latticeOrigin,
        .position = center,
        .rotation = rotation,
        .halfExtents = layout.halfExtents,
        .coefficients = std::move(coefficients),
        .validity = std::move(validity),
      };
      // The caller owns the atlas refresh - it is the only one holding the freshly
      // baked data of the other volumes, which is not on disk during a bounce pass.
      return true;
    }

    // Reload every volume so the atlas layout and the slot assignment stay
    // consistent - slots are handed out in ascending box volume order.
    SceneSerializer::LoadIrradianceVolumes(scene, assets, *this);
    return true;
  }

  void Render::BakeAllIrradianceVolumes(Scene& scene, AssetManager& assets)
  {
    // Snapshot the entity list first - BuildSceneSnapshot inside the bake can add
    // components and invalidate a live view
    std::vector<entt::entity> volumes;
    {
      auto volumeView = scene.GetView<IrradianceVolumeComponent>();
      for (auto e : volumeView)
        volumes.push_back(e);
    }

    if (volumes.empty())
    {
      YA_LOG_WARN("Render", "Bake all volumes: scene has no irradiance volumes");
      return;
    }

    int bounces = std::clamp(m_VolumeBounceCount, MIN_VOLUME_BOUNCES, MAX_VOLUME_BOUNCES);
    uint32_t volumeCount = uint32_t(volumes.size());

    YA_LOG_INFO("Render", "Baking all irradiance volumes: %u volumes x %d bounce(s)",
      volumeCount, bounces);

    // Intermediate passes must refresh the atlas without writing to disk, and the
    // atlas is rebuilt from the whole set every time, so the freshly baked data of
    // every volume is kept here for the passes that follow. Seeded from disk so a
    // volume that is never reached still contributes what it contributed before.
    std::vector<IrradianceVolumeFileData> baked(volumeCount);
    std::vector<uint8_t> hasData(volumeCount, 0);
    for (uint32_t i = 0; i < volumeCount; i++)
    {
      auto& volume = scene.GetComponent<IrradianceVolumeComponent>(volumes[i]);
      if (volume.bakedVolumePath.empty())
        continue;
      if (IrradianceVolumeFile::Load(assets.ResolvePath(volume.bakedVolumePath), baked[i]))
        hasData[i] = 1;
    }

    // Slot assignment is redone by every Upload call - ApplyIrradianceVolumes writes
    // the fresh slot back into each component, so a reshuffle mid-pass is harmless.
    //
    // The list is patched in place rather than rebuilt: an IrradianceVolumeFileData
    // carries the whole node blob, so rebuilding it after every single bake would
    // deep copy the entire set N times per pass.
    std::vector<entt::entity> uploadEntities;
    std::vector<IrradianceVolumeFileData> uploadVolumes;
    std::vector<size_t> uploadIndex(volumeCount, SIZE_MAX);
    uploadEntities.reserve(volumeCount);
    uploadVolumes.reserve(volumeCount);

    for (int pass = 0; pass < bounces; pass++)
    {
      bool finalPass = (pass == bounces - 1);
      YA_LOG_INFO("Render", "Volume bounce %d/%d", pass + 1, bounces);

      for (uint32_t i = 0; i < volumeCount; i++)
      {
        IrradianceVolumeFileData data;
        if (!BakeIrradianceVolume(volumes[i], scene, assets, finalPass, &data))
          continue;

        baked[i] = std::move(data);
        hasData[i] = 1;

        // Push the new coefficients into the atlas straight away so the volumes
        // baked after this one already pick them up.
        if (uploadIndex[i] == SIZE_MAX)
        {
          uploadIndex[i] = uploadVolumes.size();
          uploadEntities.push_back(volumes[i]);
          uploadVolumes.push_back(baked[i]);
        }
        else
        {
          uploadVolumes[uploadIndex[i]] = baked[i];
        }
        SceneSerializer::ApplyIrradianceVolumes(scene, *this, uploadEntities, uploadVolumes);
      }
    }

    YA_LOG_INFO("Render", "Baking all irradiance volumes done (%u volumes, %d bounce(s))",
      volumeCount, bounces);
  }
}

#endif
