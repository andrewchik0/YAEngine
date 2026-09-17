#pragma once

#include "Editor/EditorCommands.h"
#include "Editor/Utils/KeyRangeTiming.h"
#include "Editor/Utils/SpeedKeyTiming.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  struct EditorContext;

  // Timeline authoring operations shared by the Sequencer, the Shot Inspector, the viewport and the
  // bridge actions, so a hotkey, a button and an agent all go through the same code. The state they
  // work on is the sequencer* and pilot* part of EditorContext.
  namespace SequencerEditing
  {
    // Two keys at the same time make a zero-length segment the evaluators cannot use
    constexpr float MIN_KEY_GAP = 0.01f;
    // How far past the last key a key may be moved in one edit
    constexpr float MAX_KEY_ADVANCE = 3600.0f;
    // Camera motion above these is hard for a viewer to follow; the Sequencer and the Shot Inspector flag it
    constexpr float MOTION_SPEED_WARNING = 20.0f;
    constexpr float MOTION_TURN_WARNING = 90.0f;
    // A speed key range edit never drives faster than this, nor below this share of the speeds it started from
    constexpr float MAX_DRIVE_SPEED = 100.0f;
    constexpr float MIN_DRIVE_SPEED_SCALE = 0.05f;

    // Bounds that keep an edited key between its neighbours, so the sorted invariant holds
    // without resorting
    template<typename Key>
    float KeyLowerBound(const std::vector<Key>& keys, int index)
    {
      return index > 0 ? keys[index - 1].time + MIN_KEY_GAP : 0.0f;
    }

    template<typename Key>
    float KeyUpperBound(const std::vector<Key>& keys, int index)
    {
      float lower = KeyLowerBound(keys, index);
      if (index + 1 >= int(keys.size()))
        return std::max(lower, keys[index].time + MAX_KEY_ADVANCE);
      return std::max(lower, keys[index + 1].time - MIN_KEY_GAP);
    }

    // KeyRangeTiming with the limits above, shared by the timeline drags and the bridge actions. Return
    // the delta / duration applied after clamping.
    template<typename Key>
    float MoveKeyRange(std::vector<Key>& keys, int first, int last, float delta)
    {
      return KeyRangeTiming::MoveKeyRange(keys, size_t(first), size_t(last), delta, MIN_KEY_GAP, MAX_KEY_ADVANCE);
    }

    template<typename Key>
    float StretchKeyRange(std::vector<Key>& keys, int first, int last, float duration,
      KeyRangeTiming::StretchAnchor anchor)
    {
      return KeyRangeTiming::StretchKeyRange(keys, size_t(first), size_t(last), duration, anchor, MIN_KEY_GAP,
        MAX_KEY_ADVANCE);
    }

    // The same for motion path speed keys: the speeds are rescaled so the drive covers the same road
    inline SpeedKeyTiming::Result MoveSpeedKeyRange(std::vector<MotionSpeedKey>& keys, int first, int last, float delta)
    {
      return SpeedKeyTiming::MoveSpeedKeyRange(keys, size_t(first), size_t(last), delta, MIN_KEY_GAP, MAX_KEY_ADVANCE,
        MIN_DRIVE_SPEED_SCALE, MAX_DRIVE_SPEED);
    }

    inline SpeedKeyTiming::Result StretchSpeedKeyRange(std::vector<MotionSpeedKey>& keys, int first, int last,
      float duration, KeyRangeTiming::StretchAnchor anchor)
    {
      return SpeedKeyTiming::StretchSpeedKeyRange(keys, size_t(first), size_t(last), duration, anchor, MIN_KEY_GAP,
        MAX_KEY_ADVANCE, MIN_DRIVE_SPEED_SCALE, MAX_DRIVE_SPEED);
    }

    // Null while nothing valid is bound
    CameraTrackComponent* GetBoundTrack(EditorContext& context);
    MotionPathComponent* GetBoundPath(EditorContext& context);

    // Binds a selected track or path, drops stale bindings and selections, follows a playing
    // session with the playhead and adopts a scrub requested from outside. Idempotent: the editor
    // runs it once a frame and every sequencer panel again before it draws.
    void ResolveBinding(EditorContext& context);
    // Explicit binding; the selection that belonged to the previous binding goes, and so does a
    // pilot of another track
    void BindTrack(EditorContext& context, Entity track);
    void BindPath(EditorContext& context, Entity path);
    // Clears everything the old scene's entities invalidated
    void ResetForScene(EditorContext& context);

    float GetTimelineEnd(EditorContext& context);
    // The camera track Play and scrubbing go through: the bound one while it has keys and a camera
    Entity GetPlayableTrack(EditorContext& context);
    // Moves the playhead and poses the whole timeline there; a pilot is put back on the track pose
    void SetPlayhead(EditorContext& context, float time);
    // Poses the timeline again after an edit, at the same playhead. A pilot that was flown stays
    // where it is, one that was not follows the edit.
    void Repose(EditorContext& context);
    void TogglePlayback(EditorContext& context);
    // Null while Play is available
    const char* GetPlayUnavailableReason(EditorContext& context);

    // K. Piloting, writes the editor camera pose as the key at the playhead (a key already there
    // keeps its time and roll). Otherwise adds a key from the editor camera, or selects the key
    // already at the playhead. New keys are relative to the follow target when the track has one.
    void SetKey(EditorContext& context);
    // Replaces the pose of a key with the editor camera's and its fov with the track camera's
    void UpdateKeyFromView(EditorContext& context, int keyIndex);
    // Writes a world pose (fov included) into an existing key of the bound track at the key's time,
    // expressed in the key's own space
    void SetKeyWorldPose(EditorContext& context, int keyIndex, const CameraTrackPose& world);
    // Poses the timeline again after a key was edited. A pilot standing on that key follows the edit
    // even when it was flown.
    void KeyEdited(EditorContext& context, int keyIndex);

    // Null while keys of the bound track can be made relative to its follow target
    const char* GetTargetSpaceUnavailableReason(EditorContext& context);
    // The space a new key of the bound track gets: Target with a resolvable follow target, else World
    CameraKeySpace GetNewKeySpace(EditorContext& context);
    // Re-expresses a key in another space, so the camera stays where it is at the key's time
    void SetKeySpace(EditorContext& context, int keyIndex, CameraKeySpace space);

    // Selects one camera key of the bound track (-1: none) and drops the key range. Every plain
    // selection goes through here.
    void SelectKey(EditorContext& context, int index);
    // Shift+click: the range runs from its anchor (the selected key while no range is active) to index,
    // which becomes the selected key
    void ExtendKeyRange(EditorContext& context, int index);
    void ClearKeyRange(EditorContext& context);
    // First and last index of the key range; false while no range of two or more keys is active
    bool GetKeyRange(EditorContext& context, int& first, int& last);

    // The same for the speed keys of the bound path
    void SelectSpeedKey(EditorContext& context, int index);
    void ExtendSpeedKeyRange(EditorContext& context, int index);
    void ClearSpeedKeyRange(EditorContext& context);
    bool GetSpeedKeyRange(EditorContext& context, int& first, int& last);

    // What a retime keeps the camera's travel in step with
    enum class RetimePace : uint8_t
    {
      // Even Out Speed: equal travel in equal time
      Steady,
      // Match Car Pace: the camera's share of its travel follows the car's share of its distance
      Car
    };

    struct RetimeResult
    {
      // Empty on success, else why nothing was retimed
      std::string error;
      int first = -1;
      int last = -1;
      std::vector<float> keyTimesBefore;
      std::vector<float> keyTimesAfter;
      // Where the camera travel was measured: "target" (around the follow target), "world" or "mixed"
      const char* mode = "";
      int32_t iterations = 0;
      // Running the retime again would move no key by 1 ms or more. Otherwise shift is the largest move
      // the applied pass made, about what another run may still move a key by.
      bool settled = false;
      float shift = 0.0f;
    };

    // Retimes the keys strictly between first and last of the bound track; -1 for both takes the key
    // range, else the whole track. Camera travel is the arc length of the evaluated curve: between two
    // keys relative to the follow target it is measured in the target's frame, elsewhere in the world.
    // The first and last key keep their times and so do Hold segments. Shows the outcome as the status.
    RetimeResult RetimeKeys(EditorContext& context, RetimePace pace, int first = -1, int last = -1);
    // Empty while RetimeKeys can run with these arguments
    std::string GetRetimeUnavailableReason(EditorContext& context, RetimePace pace, int first = -1, int last = -1);

    // World motion of a camera track at one time: metres per second and degrees per second
    struct MotionSample
    {
      float time = 0.0f;
      float speed = 0.0f;
      float turnRate = 0.0f;
    };

    // Changes whenever anything the track's world pose depends on changes: its keys and settings,
    // the camera aspect, and the follow and aim targets (their motion paths or transforms)
    uint64_t HashTrackMotionInputs(Scene& scene, Entity trackEntity);
    // count samples at even times over [start, end], each measured over a short step inside the range
    void SampleTrackMotion(Scene& scene, Entity trackEntity, float start, float end, int32_t count,
      std::vector<MotionSample>& out);

    // The views a shot added now starts from: the editor camera, and for Fly To the pose the bound
    // track holds at start (5 m ahead of the editor camera while it has no keys)
    ShotViewPoses CaptureShotViews(EditorContext& context, float start);
    // EditorCommands::AddShot from the editor camera. On success binds the camera, selects the first
    // new key and moves the playhead to the start; the outcome is shown as the sequencer status.
    EditorCommands::AddShotResult AddShot(EditorContext& context, ShotTemplate shot, const ShotTemplateParams& params,
      Entity target, Entity camera);
    // "Add Shot" button with its template menu; picking a template opens the Add Shot window
    void DrawAddShotButton(EditorContext& context);
    // The Add Shot window while it is open. Once a frame, outside the panels.
    void DrawAddShotWindow(EditorContext& context);
    void CloseAddShotWindow();
    // Adds a speed key at the playhead holding the speed the drive already has there, or selects
    // the key already at the playhead
    void AddSpeedKey(EditorContext& context);
    // Selects the camera key before (direction < 0) or after the playhead and moves the playhead to it
    void StepToNeighbourKey(EditorContext& context, int direction);
    void ShowStatus(EditorContext& context, const char* text);

    // Space, K, P, Esc (piloting, not over the viewport), [ ], arrows. Call inside a sequencer panel
    // window: the keys are taken only while that window has focus.
    void HandleHotkeys(EditorContext& context);
    // P and K while piloting. Call inside the viewport window. Esc stays with the game there.
    void HandleViewportHotkeys(EditorContext& context);

    // Null while the track can be piloted
    const char* GetPilotUnavailableReason(EditorContext& context, Entity track);
    // Binds the track, remembers the editor camera and puts it on the track pose at the playhead
    bool StartPilot(EditorContext& context, Entity track);
    // Puts the editor camera pose and fov back
    void StopPilot(EditorContext& context);
    // Once a frame, after the sequence player and before the editor camera moves: ends a pilot whose
    // track went away, keeps the viewport on the editor camera and makes it follow a playing shot.
    void UpdatePilot(EditorContext& context);
    // Called when the editor camera was flown this frame
    void NotifyEditorCameraFlown(EditorContext& context);
    // Output frame of the shot inside a viewport of this size: the largest centred 16:9 rectangle
    void GetPilotOutputFrame(float viewportWidth, float viewportHeight, float& outX, float& outY,
      float& outWidth, float& outHeight);
  }
}
