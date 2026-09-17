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
  class SequencePlayer;
  class EditorCameraLayer;
  struct FrameCaptureSessionResult;
  struct BridgeCaptureStatus;

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
    SequencePlayer* sequencePlayer = nullptr;
    // The camera pilot mode flies
    EditorCameraLayer* editorCamera = nullptr;
    // Capture work that owns Render's single capture request while it runs: a --capture session
    // and an agent bridge shot
    const FrameCaptureSessionResult* captureSession = nullptr;
    const BridgeCaptureStatus* bridgeCapture = nullptr;
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

    // Timeline authoring state, shared by the Sequencer, the Shot Inspector and the viewport
    // (which highlights and picks the same keys and points). Written directly by all of them;
    // SequencerEditing::ResolveBinding drops stale bindings and indices every frame. -1 means
    // no key or point.
    Entity sequencerTrack = entt::null;
    Entity sequencerPath = entt::null;
    int sequencerSelectedKey = -1;
    // Other end of a Camera lane key range (Shift+click); the selected key is the end the last click
    // moved. -1 = only the selected key. SequencerEditing::GetKeyRange drops a range whose track no
    // longer has sequencerKeyRangeKeyCount keys, since inserted or erased keys shift the indices.
    int sequencerKeyRangeAnchor = -1;
    size_t sequencerKeyRangeKeyCount = 0;
    int sequencerSelectedSpeedKey = -1;
    // The same for a Speed lane key range, against the bound path's speed keys
    int sequencerSpeedKeyRangeAnchor = -1;
    size_t sequencerSpeedKeyRangeKeyCount = 0;
    int sequencerSelectedPoint = -1;
    float sequencerPlayhead = 0.0f;
    // Playhead time requested from outside the sequencer panels (bridge scrub), adopted like a
    // scrub on the next frame; negative = none
    float sequencerScrubRequest = -1.0f;
    // A timeline drag is running; keyboard shortcuts must not change the keys under it
    bool sequencerDragActive = false;
    // Timeline time the Sequencer should widen its view to after the timeline grew; negative = none
    float sequencerRevealTime = -1.0f;
    // Transient message shown under the Sequencer transport row until the expiry (ImGui time)
    std::string sequencerStatus;
    double sequencerStatusExpiry = 0.0;

    // Pilot mode: the editor camera sits on the pose and fov of camera track pilotTrack at the
    // playhead and can be flown from there; K writes its pose as the key at the playhead. The
    // viewport keeps looking through the editor camera, so the player posing the track camera
    // never fights it. SequencerEditing owns the transitions.
    Entity pilotTrack = entt::null;
    // The editor camera was flown since it was last put on the track pose
    bool pilotModified = false;
    // Vertical fov of the track at the playhead; the editor camera gets it widened to the 16:9 output
    float pilotTrackFov = 0.0f;
    // Editor camera pose and fov to put back when the pilot ends
    glm::vec3 pilotRestorePosition { 0.0f };
    float pilotRestoreYaw = 0.0f;
    float pilotRestorePitch = 0.0f;
    float pilotRestoreFov = 0.0f;

    // Volume bakes asked for by a panel. The panels are drawn while the frame is being recorded,
    // and a bake re-uploads the volume textures that frame has already bound, so EditorLayer
    // runs them before the next frame instead.
    Entity volumeBakeRequest = entt::null;
    bool bakeAllVolumesRequest = false;

    bool IsPreviewingCamera() const { return previewCamera != entt::null; }
    bool IsPiloting() const { return pilotTrack != entt::null; }

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
