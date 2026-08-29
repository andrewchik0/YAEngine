#pragma once

#include "Scene/Scene.h"
#include "Assets/Handle.h"

namespace YAEngine
{
  class AssetManager;
  class Render;
  class Timer;
  class EditorTextureCache;
  class ComponentRegistry;
  class CameraTrackPlayer;

  struct EditorContext
  {
    Entity selectedEntity = entt::null;
    MaterialHandle selectedMaterial;
    Scene* scene = nullptr;
    AssetManager* assetManager = nullptr;
    Render* render = nullptr;
    Timer* timer = nullptr;
    ComponentRegistry* componentRegistry = nullptr;
    EditorTextureCache* textureCache = nullptr;
    // The engine's player, so editor playback is the same code path as F9 in a game build
    CameraTrackPlayer* cameraTrackPlayer = nullptr;
    bool selectionChangedFlag = false;
    bool viewportHovered = false;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    glm::vec2 mouseInViewport { 0.0f };
    bool mouseInViewportValid = false;
    Entity revealEntityRequest = entt::null;

    // Scene camera the viewport currently looks through, and the camera to hand control
    // back to when it stops. EditorCameraLayer disables its own input on its own, since
    // it only drives the camera while that camera is the active one.
    Entity previewCamera = entt::null;
    Entity previewRestoreCamera = entt::null;

    // Published by the sequencer so the viewport path visualization highlights the same
    // key the panel has selected. -1 means no key.
    Entity sequencerTrack = entt::null;
    int sequencerSelectedKey = -1;
    // A key clicked in the viewport; the panel adopts it on its next render, since the
    // panel republishes its own selection every frame and would clobber a direct write
    int sequencerKeyPickRequest = -1;
    // Playhead time requested from outside the panel (viewport key drag); negative = none
    float sequencerScrubRequest = -1.0f;

    bool IsPreviewingCamera() const { return previewCamera != entt::null; }

    void StartCameraPreview(Entity e)
    {
      if (scene == nullptr || e == entt::null)
        return;

      // Switching straight from one preview to another must not overwrite the camera the
      // session started from
      if (previewCamera == entt::null)
        previewRestoreCamera = scene->GetActiveCamera();

      previewCamera = e;
      scene->SetActiveCamera(e);
    }

    void StopCameraPreview()
    {
      if (previewCamera == entt::null)
        return;

      previewCamera = entt::null;
      Entity restore = previewRestoreCamera;
      previewRestoreCamera = entt::null;

      if (scene == nullptr)
        return;

      if (restore == entt::null || !scene->GetRegistry().valid(restore))
      {
        // The remembered camera is gone (scene reloaded, entity deleted); the editor
        // camera is the only viewpoint guaranteed to exist.
        restore = entt::null;
        for (auto e : scene->GetView<EditorOnlyTag, CameraComponent>())
        {
          restore = e;
          break;
        }
      }

      if (restore != entt::null)
        scene->SetActiveCamera(restore);
    }

    void SelectEntity(Entity e)
    {
      selectedEntity = e;
      selectionChangedFlag = true;
    }

    void ClearSelection()
    {
      selectedEntity = entt::null;
      selectionChangedFlag = true;
    }

    bool ConsumeSelectionChanged()
    {
      bool changed = selectionChangedFlag;
      selectionChangedFlag = false;
      return changed;
    }

    // Asks the outliner to expand down to this entity and scroll to it. The outliner is
    // rendered before the details panel, so a request raised there lands one frame later.
    void RevealEntity(Entity e)
    {
      revealEntityRequest = e;
      SelectEntity(e);
    }

    Entity ConsumeRevealRequest()
    {
      Entity e = revealEntityRequest;
      revealEntityRequest = entt::null;
      return e;
    }

    void SelectMaterial(MaterialHandle h)
    {
      selectedMaterial = h;
    }

    void ClearMaterialSelection()
    {
      selectedMaterial = MaterialHandle::Invalid();
    }
  };
}
