#ifdef YA_EDITOR

#include "Render.h"

#include <ImGui/imgui_impl_vulkan.h>

#include "BindlessTextureRegistry.h"
#include "TileCullData.h"
#include "VulkanCommandBuffer.h"
#include "Assets/AssetManager.h"
#include "Assets/IrradianceVolumeFile.h"
#include "Scene/Scene.h"
#include "Scene/SceneSnapshot.h"
#include "Scene/SceneSerializer.h"
#include "Utils/IrradianceBrickBake.h"
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

  void Render::RequestSwapchainReadback()
  {
    // A copy already in flight answers this request as well
    if (m_SwapchainReadbackState == SwapchainReadbackState::Recorded)
      return;

    m_SwapchainReadback = {};
    m_SwapchainReadbackState = SwapchainReadbackState::Requested;
  }

  bool Render::ConsumeSwapchainReadback(SwapchainReadback& outReadback)
  {
    if (m_SwapchainReadbackState != SwapchainReadbackState::Ready)
      return false;

    outReadback = std::move(m_SwapchainReadback);
    m_SwapchainReadback = {};
    m_SwapchainReadbackState = SwapchainReadbackState::Idle;
    return true;
  }

  void Render::RecordSwapchainReadback(VkCommandBuffer cmd, uint32_t imageIndex)
  {
    if (m_SwapchainReadbackState != SwapchainReadbackState::Requested)
      return;

    auto& swapchain = m_Backend.GetSwapChain();
    VkFormat format = swapchain.GetFormat();
    VkExtent2D extent = swapchain.GetExt();
    bool eightBitRgba = format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_B8G8R8A8_UNORM
      || format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_R8G8B8A8_UNORM;

    const char* error = nullptr;
    if (!swapchain.SupportsTransferSource())
      error = "the window surface does not allow copying presented images";
    else if (!eightBitRgba)
      error = "the swapchain format is not 8-bit RGBA";
    else if (extent.width == 0 || extent.height == 0)
      error = "the editor window has no size";

    if (error != nullptr)
    {
      m_SwapchainReadback = {};
      m_SwapchainReadback.error = error;
      m_SwapchainReadbackState = SwapchainReadbackState::Ready;
      return;
    }

    auto& ctx = m_Backend.GetContext();
    m_SwapchainReadbackBuffer = VulkanBuffer::CreateReadback(ctx, VkDeviceSize(extent.width) * extent.height * 4);

    // The swapchain pass has just left the image ready to present
    VkImage image = swapchain.GetImage(imageIndex);
    PickCopyBarrier(cmd, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { extent.width, extent.height, 1 };
    vkCmdCopyImageToBuffer(cmd, image,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_SwapchainReadbackBuffer.Get(), 1, &region);

    PickCopyBarrier(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    m_SwapchainReadbackExtent = extent;
    m_SwapchainReadbackFormat = format;
    m_SwapchainReadbackSlot = m_Backend.GetCurrentFrameIndex();
    m_SwapchainReadbackState = SwapchainReadbackState::Recorded;
  }

  void Render::LatchSwapchainReadback()
  {
    if (m_SwapchainReadbackState != SwapchainReadbackState::Recorded
      || m_SwapchainReadbackSlot != m_Backend.GetCurrentFrameIndex())
      return;

    size_t byteCount = size_t(m_SwapchainReadbackExtent.width) * m_SwapchainReadbackExtent.height * 4;
    m_SwapchainReadback = {};
    m_SwapchainReadback.width = m_SwapchainReadbackExtent.width;
    m_SwapchainReadback.height = m_SwapchainReadbackExtent.height;
    m_SwapchainReadback.rgba.resize(byteCount);
    std::memcpy(m_SwapchainReadback.rgba.data(), m_SwapchainReadbackBuffer.GetMapped(), byteCount);

    if (m_SwapchainReadbackFormat == VK_FORMAT_B8G8R8A8_SRGB || m_SwapchainReadbackFormat == VK_FORMAT_B8G8R8A8_UNORM)
    {
      for (size_t i = 0; i < byteCount; i += 4)
        std::swap(m_SwapchainReadback.rgba[i], m_SwapchainReadback.rgba[i + 2]);
    }

    m_SwapchainReadbackBuffer.Destroy(m_Backend.GetContext());
    m_SwapchainReadbackState = SwapchainReadbackState::Ready;
  }

  void Render::DestroySwapchainReadback()
  {
    if (m_SwapchainReadbackState == SwapchainReadbackState::Recorded)
      m_SwapchainReadbackBuffer.Destroy(m_Backend.GetContext());

    m_SwapchainReadback = {};
    m_SwapchainReadbackState = SwapchainReadbackState::Idle;
  }

  void Render::ResizeViewport()
  {
    // The panel size is the output resolution; the render half follows from the mode.
    VkExtent2D outputExtent { m_PendingViewportWidth, m_PendingViewportHeight };
    VkExtent2D renderExtent = ComputeRenderExtent(m_EffectiveAntialiasingMode, outputExtent);

    ResizeGraph(renderExtent, outputExtent);

    m_ViewportWidth = outputExtent.width;
    m_ViewportHeight = outputExtent.height;
    m_ResolutionMode = m_EffectiveAntialiasingMode;
    m_ResolutionPath = m_EffectiveRenderPath;
    b_ResolutionDevResolve = b_PathTraceDevResolve;
    m_ResolutionOutputExtent = outputExtent;
  }

  bool Render::BuildRayTracingBakeScene(const SceneSnapshot& snapshot, AssetManager& assets)
  {
    auto& ctx = m_Backend.GetContext();

    if (!ctx.raytracingSupported)
    {
      YA_LOG_ERROR("Render", "Ray traced bake scene: this device has no hardware ray tracing");
      return false;
    }

    // A hit reaches material textures only through the bindless slots the material records
    // carry, so without the table the records would name textures that are not there.
    if (ctx.bindlessTextures == nullptr || !ctx.bindlessTextures->IsValid())
    {
      YA_LOG_ERROR("Render", "Ray traced bake scene: the bindless texture table is unavailable");
      return false;
    }

    const uint32_t tlasSlot = m_TlasBuilder.GetBakeSlot();
    const uint32_t materialSlot = m_MaterialTable.GetBakeSlot();

    // Material records before the build, as in the frame loop, so every material index a
    // TLAS record carries names a record written from the same MaterialManager state.
    m_MaterialTable.Update(ctx, materialSlot, assets.Materials(), assets.Textures());

    VkCommandBuffer cmd = m_Backend.GetCommandBuffer().BeginSingleTimeCommands();
    m_TlasBuilder.Build(ctx, cmd, tlasSlot, snapshot, assets.Meshes(), assets.Materials());
    m_Backend.GetCommandBuffer().EndSingleTimeCommands(cmd);

    const bool valid = m_TlasBuilder.IsValid(tlasSlot) && m_MaterialTable.IsValid(materialSlot);

    YA_LOG_INFO("Render", "Bake scene: %u TLAS instances over %zu objects, %u objects and %u lights excluded from bakes",
      m_TlasBuilder.GetInstanceCount(tlasSlot), snapshot.objects.size(),
      snapshot.bakeExcludedObjectCount, snapshot.bakeExcludedLightCount);

    if (!valid)
      YA_LOG_WARN("Render", "Bake scene: the bake slot holds nothing traceable");

    return valid;
  }

  namespace
  {
    void LogBrickLevels(const std::array<IrradianceBrickLevelStats, IRRADIANCE_SPACINGS.size()>& levelStats,
      uint32_t minSpacingIndex, uint32_t maxSpacingIndex)
    {
      for (uint32_t level = maxSpacingIndex + 1; level-- > minSpacingIndex;)
      {
        const IrradianceBrickLevelStats& stats = levelStats[level];
        YA_LOG_INFO("Render", "  %g m: %u bricks, %u unique nodes, %u stitched",
          double(IRRADIANCE_SPACINGS[level]), stats.bricks, stats.uniqueNodes, stats.stitchedNodes);
      }
    }

    enum class IntegratedNodeClass : uint8_t
    {
      Valid,
      Buried,
      TooClose,
      Enclosed,
    };

    // Shared by the main and the virtual offset integration, so an offset node is accepted by
    // exactly the tests that rejected it.
    IntegratedNodeClass ClassifyIntegratedNode(const ProbeIntegrateResult& probe, float backfaceThreshold,
      float spacing)
    {
      if (probe.backfaceFraction > backfaceThreshold)
        return IntegratedNodeClass::Buried;
      if (probe.nearestHitDistance < BakeLimits::VOLUME_MIN_PROBE_CLEARANCE_FRACTION * spacing)
        return IntegratedNodeClass::TooClose;
      if (probe.closeHitFraction >= BakeLimits::VOLUME_MAX_ENCLOSED_FRACTION)
        return IntegratedNodeClass::Enclosed;
      return IntegratedNodeClass::Valid;
    }

    // XORed into every component of a node key to seed its virtual offset integration. Keys are
    // finest lattice coordinates, far below 2^29 in magnitude, so their bits 30 and 31 agree; with
    // bit 30 flipped they differ, and no offset seed repeats any node's main seed.
    constexpr int32_t VIRTUAL_OFFSET_SEED_SALT = 0x40000000;
  }

  IrradianceBrickLayout Render::BuildIrradianceVolumeBrickLayout(const IrradianceVolumePlacementFingerprint& placement,
    double& outQuerySeconds)
  {
    const float backfaceThreshold = std::clamp(placement.backfaceRatioThreshold,
      BakeLimits::VOLUME_MIN_BACKFACE_THRESHOLD, BakeLimits::VOLUME_MAX_BACKFACE_THRESHOLD);

    outQuerySeconds = 0.0;
    std::vector<ProbeBakePoint> queryPoints;
    std::vector<ProbeGeometryResult> queryResults;

    const IrradianceBrickLayoutDesc desc {
      .center = placement.center,
      .rotation = placement.rotation,
      .halfExtents = placement.halfExtents,
      .minSpacing = placement.minSpacing,
      .maxSpacing = placement.maxSpacing,
      .limits = {
        .maxBricks = BakeLimits::VOLUME_MAX_BRICKS,
        .maxUniqueNodes = BakeLimits::VOLUME_MAX_UNIQUE_NODES,
        .maxIndirectionCells = BakeLimits::VOLUME_MAX_INDIRECTION_CELLS,
      },
      .query = [&](std::span<const IrradianceBrickQueryPoint> points, std::vector<IrradianceBrickQueryResult>& outResults) {
        queryPoints.resize(points.size());
        for (size_t i = 0; i < points.size(); i++)
          queryPoints[i] = ProbeBakePoint { .position = points[i].position, .seedKey = points[i].seedKey };

        Timer queryTimer;
        queryTimer.Step();
        const bool traced = m_RayTracedProbeBaker.GeometryQuery(queryPoints,
          BakeLimits::VOLUME_PLACEMENT_QUERY_RAYS, queryResults);
        queryTimer.Step();
        outQuerySeconds += queryTimer.GetDeltaTime();

        if (!traced || queryResults.size() != points.size())
          return false;

        outResults.resize(points.size());
        for (size_t i = 0; i < points.size(); i++)
        {
          outResults[i] = IrradianceBrickQueryResult {
            .nearestDistance = queryResults[i].nearestHitDistance,
            .buried = queryResults[i].backfaceFraction > backfaceThreshold,
          };
        }
        return true;
      },
    };

    return BuildIrradianceBrickLayout(desc);
  }

  bool Render::BakeIrradianceVolume(entt::entity entity, Scene& scene, AssetManager& assets)
  {
    if (!scene.HasComponent<IrradianceVolumeComponent>(entity))
      return false;

    if (!m_RayTracedProbeBaker.IsAvailable())
    {
      YA_LOG_ERROR("Render", "Volume '%s': irradiance volumes bake by ray tracing, and the ray traced baker is unavailable",
        scene.GetName(entity).c_str());
      return false;
    }

    vkDeviceWaitIdle(m_Backend.GetContext().device);

    SceneSnapshot snapshot;
    LightBuffer lights {};
    BuildBakeSceneSnapshot(snapshot, lights, scene, assets.Meshes(), assets.Materials());
    if (!BuildRayTracingBakeScene(snapshot, assets))
    {
      YA_LOG_ERROR("Render", "Volume '%s': the ray traced bake scene could not be built",
        scene.GetName(entity).c_str());
      return false;
    }

    IrradianceVolumeBakeCounts counts;
    if (!BakeIrradianceVolumeInBakeScene(entity, scene, assets, lights, counts))
      return false;

    // Reload every volume so the atlas layout and the slot assignment stay
    // consistent - slots are handed out in ascending box volume order.
    SceneSerializer::LoadIrradianceVolumes(scene, assets, *this);
    return true;
  }

  bool Render::BakeIrradianceVolumeInBakeScene(entt::entity entity, Scene& scene, AssetManager& assets,
    const LightBuffer& lights, IrradianceVolumeBakeCounts& outCounts)
  {
    Timer totalTimer;
    totalTimer.Step();
    Timer stepTimer;
    stepTimer.Step();

    const std::string entityName = scene.GetName(entity);
    const IrradianceVolumePlacementFingerprint placement = ComputeIrradianceVolumePlacementFingerprint(scene, entity);

    double querySeconds = 0.0;
    IrradianceBrickLayout layout = BuildIrradianceVolumeBrickLayout(placement, querySeconds);
    stepTimer.Step();
    const double layoutSeconds = stepTimer.GetDeltaTime();

    if (!layout.IsValid())
    {
      YA_LOG_ERROR("Render", "Volume '%s': brick layout failed: %s", entityName.c_str(), layout.errorMessage.c_str());
      return false;
    }

    std::string validationFailure;
    if (!ValidateIrradianceBrickLayout(layout, validationFailure))
    {
      YA_LOG_ERROR("Render", "Volume '%s': brick layout failed validation: %s", entityName.c_str(),
        validationFailure.c_str());
      return false;
    }
    stepTimer.Step();
    const double validateSeconds = stepTimer.GetDeltaTime();

    const uint32_t nodeCount = uint32_t(layout.nodes.size());
    const uint32_t stitchedCount = uint32_t(layout.stitches.size());
    const uint32_t integratedCount = nodeCount - stitchedCount;
    YA_LOG_INFO("Render", "Volume '%s': %g..%g m, %zu bricks, %u unique nodes, %u stitched",
      entityName.c_str(), double(IRRADIANCE_SPACINGS[layout.minSpacingIndex]),
      double(IRRADIANCE_SPACINGS[layout.maxSpacingIndex]), layout.bricks.size(), nodeCount, stitchedCount);
    LogBrickLevels(layout.levelStats, layout.minSpacingIndex, layout.maxSpacingIndex);

    const float backfaceThreshold = std::clamp(placement.backfaceRatioThreshold,
      BakeLimits::VOLUME_MIN_BACKFACE_THRESHOLD, BakeLimits::VOLUME_MAX_BACKFACE_THRESHOLD);
    const IrradianceVolumeComponent& volumeSettings = scene.GetComponent<IrradianceVolumeComponent>(entity);
    const bool virtualOffset = volumeSettings.virtualOffset;
    const float virtualOffsetBias = std::clamp(volumeSettings.virtualOffsetBias,
      BakeLimits::VOLUME_MIN_VIRTUAL_OFFSET_BIAS, BakeLimits::VOLUME_MAX_VIRTUAL_OFFSET_BIAS);

    // Allocated only once integration is done: at millions of nodes, holding them next to the
    // points and the integration results is what set the peak memory of the bake.
    std::vector<SHL1RGB> coefficients;
    std::vector<uint8_t> validity;
    uint32_t buriedCount = 0;
    uint32_t tooCloseCount = 0;
    uint32_t enclosedCount = 0;
    uint32_t offsetCandidateCount = 0;
    uint32_t offsetBeyondSpacingCount = 0;
    uint32_t offsetAcceptedCount = 0;
    double offsetSeconds = 0.0;
    {
      // The finest brick referencing a node is the spacing its value spreads over, and so what
      // decides how near geometry it may stand.
      std::vector<uint8_t> finestLevels(nodeCount);
      ComputeIrradianceBrickNodeFinestLevels(layout, finestLevels);

      // Stitched nodes take their value from their coarser neighbour, so only the rest is traced.
      std::vector<ProbeBakePoint> points;
      points.reserve(integratedCount);
      for (uint32_t n = 0; n < nodeCount; n++)
      {
        const IrradianceBrickNode& node = layout.nodes[n];
        if (node.stitchIndex != IRRADIANCE_BRICK_INVALID)
          continue;

        points.push_back(ProbeBakePoint {
          .position = node.worldPosition,
          .closeHitDistance = BakeLimits::VOLUME_ENCLOSURE_DISTANCE_FRACTION * IRRADIANCE_SPACINGS[finestLevels[n]],
          .seedKey = node.key,
        });
      }

      const ProbeIntegrateDesc integrateDesc {
        .samplesPerProbe = uint32_t(std::clamp(m_VolumeSampleCount, MIN_VOLUME_SAMPLES, MAX_VOLUME_SAMPLES)),
        .samplesPerPass = BakeLimits::RT_PROBE_DEFAULT_SAMPLES_PER_PASS,
        .maxBounces = std::clamp(m_VolumeBounceCount, MIN_VOLUME_BOUNCES, MAX_VOLUME_BOUNCES),
        .fireflyClamp = std::clamp(m_VolumeFireflyClamp, PT_MIN_FIREFLY_CLAMP, PT_MAX_FIREFLY_CLAMP),
      };

      YA_LOG_INFO("Render", "Volume '%s': integrating %u nodes x %u samples, %d bounces, firefly clamp %.1f",
        entityName.c_str(), integratedCount, integrateDesc.samplesPerProbe, integrateDesc.maxBounces,
        integrateDesc.fireflyClamp);

      std::vector<ProbeIntegrateResult> results;
      const bool integrated = m_RayTracedProbeBaker.Integrate(points, lights, integrateDesc, results)
        && results.size() == points.size();
      std::vector<ProbeBakePoint>().swap(points);
      if (!integrated)
      {
        YA_LOG_ERROR("Render", "Volume '%s': ray traced integration failed", entityName.c_str());
        return false;
      }

      // Offset positions and the node each belongs to, in the same order.
      std::vector<ProbeBakePoint> offsetPoints;
      std::vector<uint32_t> offsetNodes;

      coefficients.resize(nodeCount);
      validity.assign(nodeCount, 0);
      size_t result = 0;
      for (uint32_t n = 0; n < nodeCount; n++)
      {
        const IrradianceBrickNode& node = layout.nodes[n];
        if (node.stitchIndex != IRRADIANCE_BRICK_INVALID)
          continue;

        const ProbeIntegrateResult& probe = results[result++];
        const float spacing = IRRADIANCE_SPACINGS[finestLevels[n]];
        coefficients[n] = probe.coefficients;
        const IntegratedNodeClass nodeClass = ClassifyIntegratedNode(probe, backfaceThreshold, spacing);
        switch (nodeClass)
        {
        case IntegratedNodeClass::Buried: buriedCount++; break;
        case IntegratedNodeClass::TooClose: tooCloseCount++; break;
        case IntegratedNodeClass::Enclosed: enclosedCount++; break;
        case IntegratedNodeClass::Valid: validity[n] = 1; break;
        }

        // Deep inside a hollow building no back face is this near, which keeps the extra
        // integration to a shell around surfaces.
        if (!virtualOffset || nodeClass != IntegratedNodeClass::Buried
          || probe.nearestBackfaceDistance == RayTracedProbeBaker::NO_HIT || probe.nearestBackfaceDistance > spacing)
          continue;

        offsetCandidateCount++;
        // Past the face by the clearance the too close test demands, or the node would fail it against
        // the very face it crossed, and by the bias beyond that. Capped at the spacing: farther out the
        // node would stand for light from beyond the spacing its value spreads over.
        const float offsetLength = probe.nearestBackfaceDistance
          + BakeLimits::VOLUME_MIN_PROBE_CLEARANCE_FRACTION * spacing + virtualOffsetBias;
        if (offsetLength > spacing)
        {
          offsetBeyondSpacingCount++;
          continue;
        }

        offsetPoints.push_back(ProbeBakePoint {
          .position = node.worldPosition + probe.nearestBackfaceDirection * offsetLength,
          .closeHitDistance = BakeLimits::VOLUME_ENCLOSURE_DISTANCE_FRACTION * spacing,
          .seedKey = node.key ^ glm::ivec3(VIRTUAL_OFFSET_SEED_SALT),
        });
        offsetNodes.push_back(n);
      }
      std::vector<ProbeIntegrateResult>().swap(results);

      if (!offsetPoints.empty())
      {
        Timer offsetTimer;
        offsetTimer.Step();

        YA_LOG_INFO("Render", "Volume '%s': integrating %zu virtual offset nodes, clearance %.2f of their spacing plus bias %.3f m in front of their nearest back face",
          entityName.c_str(), offsetPoints.size(), double(BakeLimits::VOLUME_MIN_PROBE_CLEARANCE_FRACTION),
          double(virtualOffsetBias));

        std::vector<ProbeIntegrateResult> offsetResults;
        const bool offsetIntegrated = m_RayTracedProbeBaker.Integrate(offsetPoints, lights, integrateDesc, offsetResults)
          && offsetResults.size() == offsetPoints.size();
        std::vector<ProbeBakePoint>().swap(offsetPoints);
        if (!offsetIntegrated)
        {
          YA_LOG_ERROR("Render", "Volume '%s': ray traced integration of the virtual offset nodes failed",
            entityName.c_str());
          return false;
        }

        for (size_t i = 0; i < offsetResults.size(); i++)
        {
          const uint32_t n = offsetNodes[i];
          if (ClassifyIntegratedNode(offsetResults[i], backfaceThreshold, IRRADIANCE_SPACINGS[finestLevels[n]])
            != IntegratedNodeClass::Valid)
            continue;

          coefficients[n] = offsetResults[i].coefficients;
          validity[n] = 1;
          offsetAcceptedCount++;
        }

        offsetTimer.Step();
        offsetSeconds = offsetTimer.GetDeltaTime();
      }
    }
    stepTimer.Step();
    const double integrateSeconds = stepTimer.GetDeltaTime();

    const uint32_t offsetRejectedCount = offsetCandidateCount - offsetAcceptedCount;
    if (virtualOffset)
    {
      YA_LOG_INFO("Render", "Volume '%s': virtual offset of %u buried nodes within their spacing of a back face: %u accepted, %u rejected (%u of them untraced, clearance %.2f of their spacing plus bias %.3f m taking their offset beyond their spacing), %.2f s",
        entityName.c_str(), offsetCandidateCount, offsetAcceptedCount, offsetRejectedCount, offsetBeyondSpacingCount,
        double(BakeLimits::VOLUME_MIN_PROBE_CLEARANCE_FRACTION), double(virtualOffsetBias), offsetSeconds);
    }

    // Stitched nodes derive their validity from these, so none valid here leaves none valid at all.
    if (buriedCount + tooCloseCount + enclosedCount - offsetAcceptedCount == integratedCount)
    {
      YA_LOG_ERROR("Render", "Volume '%s': all %u integrated nodes are rejected, %u buried (backface fraction above %.2f) with no virtual offset accepted, %u too close to geometry (nearer than %.2f of their spacing) and %u enclosed (at least %.2f of their rays hitting geometry within %.2f of their spacing), the volume would be black - not saved",
        entityName.c_str(), integratedCount, buriedCount, double(backfaceThreshold), tooCloseCount,
        double(BakeLimits::VOLUME_MIN_PROBE_CLEARANCE_FRACTION), enclosedCount,
        double(BakeLimits::VOLUME_MAX_ENCLOSED_FRACTION), double(BakeLimits::VOLUME_ENCLOSURE_DISTANCE_FRACTION));
      return false;
    }

    // Rejected nodes get their neighbours' light but keep validity 0: validity records what the bake
    // rejected, not what is left unfilled.
    IrradianceBrickDilationResult dilation;
    {
      std::vector<uint8_t> filled = validity;
      dilation = DilateIrradianceBrickNodes(layout, coefficients, filled);
    }
    if (dilation.unreachable > 0)
    {
      YA_LOG_WARN("Render", "Volume '%s': %u buried, too close or enclosed nodes are connected to no valid node and are left black",
        entityName.c_str(), dilation.unreachable);
    }
    stepTimer.Step();
    const double dilateSeconds = stepTimer.GetDeltaTime();

    StitchIrradianceBrickNodes(layout, coefficients, validity);
    // Save needs the bricks, their node indices and the indirection only.
    std::vector<IrradianceBrickNode>().swap(layout.nodes);
    std::vector<IrradianceBrickStitch>().swap(layout.stitches);
    stepTimer.Step();
    const double stitchSeconds = stepTimer.GetDeltaTime();

    IrradianceVolumeFileData data {
      .position = layout.center,
      .rotation = layout.rotation,
      .halfExtents = layout.halfExtents,
      .edgeFade = std::clamp(scene.GetComponent<IrradianceVolumeComponent>(entity).edgeFade,
        BakeLimits::VOLUME_MIN_EDGE_FADE, BakeLimits::VOLUME_MAX_EDGE_FADE),
      .minSpacingIndex = layout.minSpacingIndex,
      .maxSpacingIndex = layout.maxSpacingIndex,
      .indirectionOriginKey = layout.indirectionOriginKey,
      .indirectionDims = layout.indirectionDims,
      .indirectionCellKeys = uint32_t(layout.indirectionCellKeys),
      .bricks = std::move(layout.bricks),
      .brickNodeIndices = std::move(layout.brickNodeIndices),
      .validity = std::move(validity),
      .indirection = std::move(layout.indirection),
    };
    data.coefficients.resize(nodeCount);
    for (uint32_t n = 0; n < nodeCount; n++)
      data.coefficients[n] = PackSHL1RGBHalf(coefficients[n]);
    std::vector<SHL1RGB>().swap(coefficients);

    std::string probeDir = assets.GetBasePath() + "/Assets/Probes";
    std::filesystem::create_directories(probeDir);
    std::string volumePath = probeDir + "/" + entityName + "_volume.yaiv";

    if (!IrradianceVolumeFile::Save(volumePath, data))
    {
      YA_LOG_ERROR("Render", "Volume '%s': failed to save %s",
        entityName.c_str(), volumePath.c_str());
      return false;
    }
    stepTimer.Step();
    const double saveSeconds = stepTimer.GetDeltaTime();

    auto& volume = scene.GetComponent<IrradianceVolumeComponent>(entity);
    volume.bakedVolumePath = assets.MakeRelative(volumePath);
    volume.baked = true;
    YA_LOG_INFO("Render", "Saved irradiance volume: %s", volume.bakedVolumePath.c_str());

    totalTimer.Step();
    YA_LOG_INFO("Render", "Volume '%s' baked: %u unique nodes, %u integrated, %u buried (%u recovered by virtual offset), %u too close, %u enclosed, %u dilated in %u waves, %u unreachable, %u stitched",
      entityName.c_str(), nodeCount, integratedCount, buriedCount, offsetAcceptedCount, tooCloseCount, enclosedCount,
      dilation.dilated, dilation.waves, dilation.unreachable, stitchedCount);
    YA_LOG_INFO("Render", "Volume '%s' timings: layout and queries %.2f s (queries %.2f s), validation %.2f s, integrate %.2f s (virtual offset %.2f s), dilate %.2f s, stitch %.2f s, save %.2f s, total %.2f s",
      entityName.c_str(), layoutSeconds, querySeconds, validateSeconds, integrateSeconds, offsetSeconds, dilateSeconds,
      stitchSeconds, saveSeconds, totalTimer.GetDeltaTime());

    outCounts = IrradianceVolumeBakeCounts {
      .uniqueNodes = nodeCount,
      .integratedNodes = integratedCount,
      .buriedNodes = buriedCount,
      .tooCloseNodes = tooCloseCount,
      .enclosedNodes = enclosedCount,
      .virtualOffsetCandidates = offsetCandidateCount,
      .virtualOffsetAccepted = offsetAcceptedCount,
      .virtualOffsetRejected = offsetRejectedCount,
      .virtualOffsetSeconds = offsetSeconds,
    };
    return true;
  }

  IrradianceVolumeBakeAllResult Render::BakeAllIrradianceVolumes(Scene& scene, AssetManager& assets)
  {
    IrradianceVolumeBakeAllResult result;

    // Copied so the count is known before anything is built. Not a guard against view
    // invalidation: entt keeps one pool per component type, and nothing below adds or
    // removes an IrradianceVolumeComponent.
    std::vector<entt::entity> volumes;
    {
      auto volumeView = scene.GetView<IrradianceVolumeComponent>();
      for (auto e : volumeView)
        volumes.push_back(e);
    }

    if (volumes.empty())
    {
      YA_LOG_WARN("Render", "Bake all volumes: scene has no irradiance volumes");
      return result;
    }

    if (!m_RayTracedProbeBaker.IsAvailable())
    {
      YA_LOG_ERROR("Render", "Bake all volumes: irradiance volumes bake by ray tracing, and the ray traced baker is unavailable");
      return result;
    }

    Timer timer;
    timer.Step();

    uint32_t volumeCount = uint32_t(volumes.size());
    YA_LOG_INFO("Render", "Baking all irradiance volumes: %u volumes", volumeCount);

    vkDeviceWaitIdle(m_Backend.GetContext().device);

    // One bake scene serves every volume: the ray traced baker never reads irradiance
    // volumes, so no volume depends on another's result.
    SceneSnapshot snapshot;
    LightBuffer lights {};
    BuildBakeSceneSnapshot(snapshot, lights, scene, assets.Meshes(), assets.Materials());
    if (!BuildRayTracingBakeScene(snapshot, assets))
    {
      YA_LOG_ERROR("Render", "Bake all volumes: the ray traced bake scene could not be built");
      return result;
    }

    result.bakeSceneBuilt = true;
    result.volumes.reserve(volumes.size());

    uint32_t bakedCount = 0;
    uint64_t totalNodes = 0;
    uint64_t totalIntegrated = 0;
    uint64_t totalBuried = 0;
    uint64_t totalTooClose = 0;
    uint64_t totalEnclosed = 0;
    uint64_t totalOffsetCandidates = 0;
    uint64_t totalOffsetAccepted = 0;
    uint64_t totalOffsetRejected = 0;
    double totalOffsetSeconds = 0.0;
    for (entt::entity entity : volumes)
    {
      result.volumes.push_back({ .entity = entity });

      IrradianceVolumeBakeCounts counts;
      if (!BakeIrradianceVolumeInBakeScene(entity, scene, assets, lights, counts))
        continue;

      result.volumes.back().rebaked = true;
      bakedCount++;
      totalNodes += counts.uniqueNodes;
      totalIntegrated += counts.integratedNodes;
      totalBuried += counts.buriedNodes;
      totalTooClose += counts.tooCloseNodes;
      totalEnclosed += counts.enclosedNodes;
      totalOffsetCandidates += counts.virtualOffsetCandidates;
      totalOffsetAccepted += counts.virtualOffsetAccepted;
      totalOffsetRejected += counts.virtualOffsetRejected;
      totalOffsetSeconds += counts.virtualOffsetSeconds;
    }

    // Once, after every file is written: slots are handed out over the whole set in
    // ascending box volume order.
    SceneSerializer::LoadIrradianceVolumes(scene, assets, *this);

    timer.Step();
    YA_LOG_INFO("Render", "Baking all irradiance volumes done: %u of %u volumes, %llu unique nodes, %llu integrated, %llu buried, %llu too close, %llu enclosed, virtual offset %llu candidates, %llu accepted, %llu rejected in %.2f s, %.2f s",
      bakedCount, volumeCount, (unsigned long long)totalNodes, (unsigned long long)totalIntegrated,
      (unsigned long long)totalBuried, (unsigned long long)totalTooClose, (unsigned long long)totalEnclosed,
      (unsigned long long)totalOffsetCandidates, (unsigned long long)totalOffsetAccepted,
      (unsigned long long)totalOffsetRejected, totalOffsetSeconds, timer.GetDeltaTime());

    return result;
  }

  IrradianceVolumePlacementFingerprint Render::ComputeIrradianceVolumePlacementFingerprint(Scene& scene,
    entt::entity entity)
  {
    const auto& volume = scene.GetComponent<IrradianceVolumeComponent>(entity);
    const glm::mat4& world = scene.GetWorldTransform(entity).world;
    return IrradianceVolumePlacementFingerprint {
      .center = glm::vec3(world[3]),
      .rotation = ExtractIrradianceBoxRotation(world),
      .halfExtents = volume.halfExtents,
      .minSpacing = volume.minSpacing,
      .maxSpacing = volume.maxSpacing,
      .backfaceRatioThreshold = volume.backfaceRatioThreshold,
    };
  }

  IrradianceVolumePlacementEstimate Render::EstimateIrradianceVolumePlacement(const IrradianceVolumePlacementPreview& preview,
    uint32_t samplesPerProbe)
  {
    // At runtime a brick node is a texel of three RGBA16F coefficient textures and one R8 validity
    // texture. On disk a unique node is 12 half floats and a validity byte, a brick its node
    // indices as uint32 and a header.
    constexpr uint64_t RUNTIME_TEXEL_BYTES = 3 * 4 * 2 + 1;
    constexpr uint64_t DISK_NODE_BYTES = 12 * 2 + 1;
    constexpr uint64_t DISK_BRICK_HEADER_BYTES = 16;
    constexpr uint64_t DISK_BRICK_BYTES = uint64_t(IRRADIANCE_BRICK_NODE_COUNT) * 4 + DISK_BRICK_HEADER_BYTES;
    constexpr uint64_t INDIRECTION_CELL_BYTES = 4;

    IrradianceVolumePlacementEstimate estimate;
    for (const IrradianceBrickLevelStats& level : preview.levelStats)
    {
      estimate.bricks += level.bricks;
      estimate.uniqueNodes += level.uniqueNodes;
      estimate.stitchedNodes += level.stitchedNodes;
    }
    estimate.bakedNodes = estimate.uniqueNodes - estimate.stitchedNodes;
    estimate.indirectionCells = preview.indirectionCells;

    estimate.runtimeBytes = uint64_t(estimate.bricks) * IRRADIANCE_BRICK_NODE_COUNT * RUNTIME_TEXEL_BYTES
      + uint64_t(estimate.indirectionCells) * INDIRECTION_CELL_BYTES;
    estimate.diskBytes = uint64_t(estimate.uniqueNodes) * DISK_NODE_BYTES
      + uint64_t(estimate.bricks) * DISK_BRICK_BYTES
      + uint64_t(estimate.indirectionCells) * INDIRECTION_CELL_BYTES;
    estimate.primarySamples = uint64_t(estimate.bakedNodes) * samplesPerProbe;
    return estimate;
  }

  std::array<uint32_t, IRRADIANCE_SPACINGS.size()> Render::GetPreviewBrickDrawStrides(
    const IrradianceVolumePlacementPreview& preview, uint32_t drawnNodeGizmos)
  {
    std::array<uint32_t, IRRADIANCE_SPACINGS.size()> strides;
    strides.fill(1);

    uint32_t budget = MAX_DRAWN_PREVIEW_BRICKS
      - std::min(drawnNodeGizmos, MAX_DRAWN_PREVIEW_BRICKS - MAX_DRAWN_PREVIEW_BRICKS_WITH_NODES);
    std::array<bool, IRRADIANCE_SPACINGS.size()> open {};
    uint32_t openCount = 0;
    for (size_t level = 0; level < open.size(); level++)
    {
      open[level] = preview.levelStats[level].bricks > 0;
      openCount += open[level] ? 1 : 0;
    }

    while (openCount > 0)
    {
      const uint32_t share = std::max(budget / openCount, 1u);
      bool settled = false;
      for (size_t level = 0; level < open.size(); level++)
      {
        if (!open[level] || preview.levelStats[level].bricks > share)
          continue;
        budget -= std::min(budget, preview.levelStats[level].bricks);
        open[level] = false;
        openCount--;
        settled = true;
      }

      if (settled)
        continue;

      for (size_t level = 0; level < open.size(); level++)
      {
        if (open[level])
          strides[level] = (preview.levelStats[level].bricks + share - 1) / share;
      }
      break;
    }
    return strides;
  }

  glm::vec4 Render::GetPlacementBrickColor(uint32_t spacingIndex)
  {
    static_assert(IRRADIANCE_SPACINGS.size() == 5, "IrradianceVolumeData.h defines one IRRADIANCE_LEVEL_COLOR per spacing");
    static const std::array<glm::vec4, IRRADIANCE_SPACINGS.size()> COLORS = {
      glm::vec4(IRRADIANCE_LEVEL_COLOR_0),
      glm::vec4(IRRADIANCE_LEVEL_COLOR_1),
      glm::vec4(IRRADIANCE_LEVEL_COLOR_2),
      glm::vec4(IRRADIANCE_LEVEL_COLOR_3),
      glm::vec4(IRRADIANCE_LEVEL_COLOR_4),
    };
    return COLORS[std::min(spacingIndex, uint32_t(COLORS.size() - 1))];
  }

  const IrradianceVolumePlacementPreview* Render::PreviewIrradianceVolumePlacement(entt::entity entity,
    Scene& scene, AssetManager& assets)
  {
    if (!scene.HasComponent<IrradianceVolumeComponent>(entity))
    {
      YA_LOG_ERROR("Render", "Placement preview: entity %u has no irradiance volume",
        uint32_t(entt::to_integral(entity)));
      return nullptr;
    }

    std::string entityName = scene.GetName(entity);
    Timer totalTimer;
    totalTimer.Step();

    IrradianceVolumePlacementPreview preview;
    preview.fingerprint = ComputeIrradianceVolumePlacementFingerprint(scene, entity);

    // Replaces the kept preview, so an earlier layout never passes for the volume as last asked about.
    auto keepFailure = [&](const char* error)
    {
      preview.error = error;
      m_VolumePlacementPreviews[entity] = std::move(preview);
    };

    if (!m_RayTracedProbeBaker.IsAvailable())
    {
      YA_LOG_ERROR("Render", "Volume '%s': the placement preview finds geometry by ray tracing, and the ray traced baker is unavailable",
        entityName.c_str());
      keepFailure("the ray traced baker is unavailable");
      return nullptr;
    }

    vkDeviceWaitIdle(m_Backend.GetContext().device);

    SceneSnapshot snapshot;
    LightBuffer lights {};
    BuildBakeSceneSnapshot(snapshot, lights, scene, assets.Meshes(), assets.Materials());
    if (!BuildRayTracingBakeScene(snapshot, assets))
    {
      YA_LOG_ERROR("Render", "Volume '%s': the ray traced bake scene for the placement preview could not be built",
        entityName.c_str());
      keepFailure("the ray traced bake scene could not be built (nothing traceable in the scene)");
      return nullptr;
    }

    Timer layoutTimer;
    layoutTimer.Step();
    IrradianceBrickLayout layout = BuildIrradianceVolumeBrickLayout(preview.fingerprint, preview.querySeconds);
    layoutTimer.Step();
    preview.layoutSeconds = layoutTimer.GetDeltaTime();
    preview.queryBatches = layout.queryBatches;
    preview.queryPoints = layout.queryPoints;

    if (!layout.IsValid())
    {
      YA_LOG_ERROR("Render", "Volume '%s': placement layout failed: %s", entityName.c_str(), layout.errorMessage.c_str());
      preview.error = layout.errorMessage;
    }
    else
    {
      preview.validationPassed = ValidateIrradianceBrickLayout(layout, preview.validationFailure);
      if (!preview.validationPassed)
      {
        YA_LOG_ERROR("Render", "Volume '%s': placement layout failed validation: %s",
          entityName.c_str(), preview.validationFailure.c_str());
      }

      preview.minSpacingIndex = layout.minSpacingIndex;
      preview.maxSpacingIndex = layout.maxSpacingIndex;
      preview.levelStats = layout.levelStats;
      preview.indirectionCells = uint32_t(layout.indirection.size());
      preview.bricks = std::move(layout.bricks);

      const IrradianceVolumePlacementEstimate totals = EstimateIrradianceVolumePlacement(preview, 0);
      YA_LOG_INFO("Render", "Volume '%s': placement %g..%g m, %u bricks, %u unique nodes (%u stitched), %u indirection cells",
        entityName.c_str(), double(IRRADIANCE_SPACINGS[preview.minSpacingIndex]),
        double(IRRADIANCE_SPACINGS[preview.maxSpacingIndex]), totals.bricks, totals.uniqueNodes,
        totals.stitchedNodes, totals.indirectionCells);
      LogBrickLevels(preview.levelStats, preview.minSpacingIndex, preview.maxSpacingIndex);
    }

    totalTimer.Step();
    preview.totalSeconds = totalTimer.GetDeltaTime();
    YA_LOG_INFO("Render", "Volume '%s': placement preview %.2f s, layout %.2f s, GPU queries %.2f s (%u batches, %u points x %u rays)",
      entityName.c_str(), preview.totalSeconds, preview.layoutSeconds, preview.querySeconds,
      preview.queryBatches, preview.queryPoints, BakeLimits::VOLUME_PLACEMENT_QUERY_RAYS);

    IrradianceVolumePlacementPreview& stored = m_VolumePlacementPreviews[entity];
    stored = std::move(preview);
    return &stored;
  }

  const IrradianceVolumePlacementPreview* Render::FindIrradianceVolumePlacementPreview(entt::entity entity) const
  {
    auto it = m_VolumePlacementPreviews.find(entity);
    return it != m_VolumePlacementPreviews.end() ? &it->second : nullptr;
  }

  void Render::PruneIrradianceVolumePlacementPreviews(Scene& scene)
  {
    std::erase_if(m_VolumePlacementPreviews, [&scene](const auto& entry) {
      return !scene.GetRegistry().valid(entry.first) || !scene.HasComponent<IrradianceVolumeComponent>(entry.first);
    });
  }
}

#endif
