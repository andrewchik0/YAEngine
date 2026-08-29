#pragma once

#include "Pch.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class Render;

  // Plays a CameraTrackComponent by driving the entity that owns it. Lives in Core rather
  // than the editor: recording a demo reel is a game build feature, the editor only adds
  // the authoring side on top.
  class CameraTrackPlayer
  {
  public:
    explicit CameraTrackPlayer(Render& render) : m_Render(&render) {}

    // The entity needs both a CameraTrackComponent and a CameraComponent; it becomes the
    // active camera until playback ends.
    void Start(Scene& scene, Entity trackEntity);
    void Stop(Scene& scene);
    // Freezes time without ending the session: the camera stays handed over, so a paused
    // track can be scrubbed through the viewport it already owns.
    void Pause() { b_Paused = b_Playing; }
    void Resume() { b_Paused = false; }
    // Jumps to an arbitrary time and applies the pose right away. A stopped player has no
    // track to jump on - the sequencer calls ApplyTrackPose directly in that case.
    void SetElapsed(Scene& scene, double time);
    // dt must be the real frame delta - an uncapped frame rate is the whole point of
    // playing a track back, so no fixed step and no reel timing here.
    void Update(Scene& scene, double dt);

    // Writes the pose the track holds at an arbitrary time onto the entity, without
    // touching playback state. Sequencer scrubbing reuses this.
    static void ApplyTrackPose(Scene& scene, Entity trackEntity, float time);

    // True while a session holds the camera, whether or not time is advancing
    bool IsPlaying() const { return b_Playing; }
    bool IsPaused() const { return b_Paused; }
    double GetElapsed() const { return m_Elapsed; }
    Entity GetTrackEntity() const { return m_TrackEntity; }

  private:
    Render* m_Render = nullptr;
    Entity m_TrackEntity { entt::null };
    Entity m_PreviousCamera { entt::null };
    double m_Elapsed = 0.0;
    bool b_Playing = false;
    bool b_Paused = false;
  };
}
