#pragma once

#include "Pch.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class Render;

  // Plays the scene timeline: an optional camera track and every MotionPathComponent, all
  // sampled at the same time. Camera key times and speed key times share that one timeline,
  // so a shot is simply the range between a camera track's first and last key.
  //
  // Lives in Core rather than the editor: recording a demo reel is a game build feature, the
  // editor only adds the authoring side on top.
  //
  // Every transform a session writes is remembered first and put back on Stop, so playing or
  // scrubbing never leaves the scene changed - the editor would otherwise save a car parked
  // wherever the playhead was.
  class SequencePlayer
  {
  public:
    explicit SequencePlayer(Render& render) : m_Render(&render) {}

    // Plays the shot of cameraTrack through that camera, or with no track the whole timeline
    // through whatever camera is active. A running session is stopped first.
    void Play(Scene& scene, Entity cameraTrack);
    // Makes a paused or previewing session advance, handing the viewport to its camera track
    // if it has not been yet. A session at the end of its range starts over.
    void Resume(Scene& scene);
    void Pause() { b_Advancing = false; }
    void Stop(Scene& scene);

    // Poses the timeline at an arbitrary time. Without a session this opens a paused preview
    // bound to cameraTrack that leaves the active camera alone, so authoring can scrub while
    // the viewport keeps its own viewpoint.
    void Scrub(Scene& scene, Entity cameraTrack, double time);

    // dt must be the real frame delta - an uncapped frame rate is the whole point of
    // playing a sequence back, so no fixed step and no reel timing here.
    void Update(Scene& scene, double dt);

    // Remembers the entity's transform the first time it is called in a session, so Stop
    // restores it. Gameplay code driving extra entities (wheels, body parts) calls this too.
    void ProtectTransform(Scene& scene, Entity entity);

    // A session exists: playing, paused or previewing
    bool IsActive() const { return b_Active; }
    bool IsAdvancing() const { return b_Active && b_Advancing; }
    bool IsPaused() const { return b_Active && !b_Advancing; }
    bool HoldsCamera() const { return b_HoldsCamera; }
    // The session poses motion paths: false without a session and for a camera-only shot
    bool DrivesMotionPaths(Scene& scene) const;
    double GetTime() const { return m_Time; }
    double GetStartTime() const { return m_StartTime; }
    double GetEndTime() const { return m_EndTime; }
    Entity GetCameraTrack() const { return m_CameraTrack; }

    // Writes the pose the track holds at an arbitrary time onto the entity, without touching
    // playback state
    static void ApplyTrackPose(Scene& scene, Entity trackEntity, float time);

    // Frame of the track's follow target at a timeline time: the motion path point with its
    // heading when the target has a drivable path, its world transform otherwise. False
    // when there is no follow target to resolve.
    static bool ResolveTargetFrame(Scene& scene, const CameraTrackComponent& track, float time,
      CameraTargetFrame& out);
    // World pose the track holds at a time, aim included. Writes nothing; false when the
    // entity has no track with keys.
    static bool EvaluateTrackPose(Scene& scene, Entity trackEntity, float time, CameraTrackPose& out);
    // A key at time holding a world pose. A Target key is expressed in the follow target's
    // frame at that time; without a resolvable target the key stays World.
    static CameraTrackKey KeyFromWorldPose(Scene& scene, Entity trackEntity, const CameraTrackPose& world,
      CameraKeySpace space, float time);
    // Where a key puts the camera with the follow target as it is at time (no aim)
    static CameraTrackPose KeyToWorldPose(Scene& scene, Entity trackEntity, const CameraTrackKey& key,
      float time);

    // The follow and aim target entities of a track, looked up by name once. The overloads below
    // give the same results as the ones above for callers that evaluate one track many times in a
    // row (the editor draws and samples whole shots), where a name lookup per call adds up.
    struct TrackTargets
    {
      Entity follow { entt::null };
      Entity aim { entt::null };
    };
    static TrackTargets FindTrackTargets(Scene& scene, Entity trackEntity);
    static bool ResolveTargetFrame(Scene& scene, Entity trackEntity, const TrackTargets& targets, float time,
      CameraTargetFrame& out);
    static bool EvaluateTrackPose(Scene& scene, Entity trackEntity, const TrackTargets& targets, float time,
      CameraTrackPose& out);
    static CameraTrackPose KeyToWorldPose(Scene& scene, Entity trackEntity, const TrackTargets& targets,
      const CameraTrackKey& key, float time);

    // Shot range of a camera track: its first and last key
    static float ShotStart(const CameraTrackComponent& track);
    static float ShotEnd(const CameraTrackComponent& track);
    // End of everything on the timeline: the last camera key and the end of the longest drive
    static double TimelineDuration(Scene& scene);

  private:
    struct SavedTransform
    {
      Entity entity { entt::null };
      LocalTransform transform;
    };

    bool IsPlayableTrack(Scene& scene, Entity track) const;
    void Begin(Scene& scene, Entity cameraTrack);
    void TakeCamera(Scene& scene);
    void ApplyPoses(Scene& scene);
    void UpdateRange(Scene& scene);

    Render* m_Render = nullptr;
    Entity m_CameraTrack { entt::null };
    Entity m_PreviousCamera { entt::null };
    std::vector<SavedTransform> m_Saved;
    double m_Time = 0.0;
    double m_StartTime = 0.0;
    double m_EndTime = 0.0;
    bool b_Active = false;
    bool b_Advancing = false;
    bool b_HoldsCamera = false;
  };
}
