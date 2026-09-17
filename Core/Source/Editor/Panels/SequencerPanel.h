#pragma once

#include "Editor/IEditorPanel.h"
#include "Editor/SequencerEditing.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  // Authoring front end for the scene timeline: a camera track (a shot) and a motion path on one
  // time axis, with transport, pilot mode and scrubbing. The selected keys and points are edited in
  // the Shot Inspector. Playback goes through the engine SequencePlayer, so what the editor shows is
  // what a game build plays.
  class SequencerPanel : public IEditorPanel
  {
  public:

    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "Sequencer", .category = EditorPanelCategory::Animation };

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;
    void OnSceneReady(EditorContext& context) override;

  private:

    // KeyRange*: the selected camera key range, moved whole or stretched by its left or right edge;
    // SpeedRange*: the same for the selected speed key range
    enum class DragKind : uint8_t
    {
      None, CameraKey, SpeedKey, Scrub, Pan,
      KeyRangeMove, KeyRangeLeft, KeyRangeRight,
      SpeedRangeMove, SpeedRangeLeft, SpeedRangeRight
    };

    // A camera track on the Shots lane, from its first to its last key
    struct ShotClip
    {
      Entity entity { entt::null };
      float start = 0.0f;
      float end = 0.0f;
      int32_t row = 0;
    };

    void DrawBindingRow(EditorContext& context);
    void DrawTransportRow(EditorContext& context, CameraTrackComponent* track, MotionPathComponent* path);
    void DrawStatus(EditorContext& context);
    void DrawTimeline(EditorContext& context, CameraTrackComponent* track, MotionPathComponent* path);
    void EndDrag(EditorContext& context);
    // Fills m_ShotClips with stacked rows; returns the row count
    int32_t CollectShotClips(Scene& scene);
    // Resamples the camera motion over [start, end] when anything it depends on changed
    void UpdateMotionSamples(EditorContext& context, float start, float end, int32_t count);

    // Bindings the visible range was last fitted to; rebinding reframes the timeline
    Entity m_FramedTrack { entt::null };
    Entity m_FramedPath { entt::null };

    // Visible time range, so zoom and pan survive between frames
    float m_ViewStart = 0.0f;
    float m_ViewEnd = 10.0f;

    // Timeline drag in progress. A key drag leaves the key alone until the mouse is out of the dead zone.
    DragKind m_Drag = DragKind::None;
    int m_DragKey = -1;
    bool b_DragMoved = false;
    // Time under the mouse at the press: a pan keeps it under the mouse, a key drag moves by the offset from it
    float m_DragAnchorTime = 0.0f;
    // State at the press, restored when a key drag is cancelled
    float m_DragStartTime = 0.0f;
    float m_DragStartPlayhead = 0.0f;
    // Key range drag: the range and every key time of its track at the press. Each frame starts from these,
    // since a stretch with ripple moves keys outside the range too; a cancel puts them all back. m_DragKey is
    // the key the press landed on (-1: none), selected alone when the press turns out to be a click.
    Entity m_DragTrack { entt::null };
    int m_DragRangeFirst = -1;
    int m_DragRangeLast = -1;
    std::vector<float> m_DragStartTimes;
    // Speed key range drag: the path and every speed of its keys at the press, since the edit rescales them
    Entity m_DragPath { entt::null };
    std::vector<float> m_DragStartSpeeds;

    std::vector<ShotClip> m_ShotClips;

    // Camera speed and turn rate across the visible part of the shot, with what they were sampled from
    uint64_t m_MotionHash = 0;
    std::vector<SequencerEditing::MotionSample> m_MotionSamples;
    float m_MotionMaxSpeed = 0.0f;
    float m_MotionMaxTurn = 0.0f;
  };
}
