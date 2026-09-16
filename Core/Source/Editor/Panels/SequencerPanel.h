#pragma once

#include "Editor/IEditorPanel.h"
#include "Editor/EditorCommands.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  // Authoring front end for the scene timeline: a camera track (a shot) and a motion path on one
  // time axis, with transport and scrubbing. Playback goes through the engine SequencePlayer, so
  // what the editor shows is what a game build plays.
  class SequencerPanel : public IEditorPanel
  {
  public:

    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "Sequencer", .category = EditorPanelCategory::Animation };

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;
    void OnSceneReady(EditorContext& context) override;

  private:

    enum class DragKind : uint8_t { None, CameraKey, SpeedKey, Scrub, Pan };
    enum class KeyAction : uint8_t { None, UpdateFromView, Duplicate, Delete };
    enum class PointAction : uint8_t { None, InsertAfter, Delete };

    void ResolveBinding(EditorContext& context);
    void DrawBindingRow(EditorContext& context);
    void DrawTransportRow(EditorContext& context, CameraTrackComponent* track, MotionPathComponent* path);
    void DrawTimeline(EditorContext& context, CameraTrackComponent* track, MotionPathComponent* path);
    void DrawKeyInspector(EditorContext& context, CameraTrackComponent& track);
    void DrawSettings(EditorContext& context, CameraTrackComponent& track);
    void DrawSpeedKeyInspector(EditorContext& context, MotionPathComponent& path);
    void DrawPathSettings(EditorContext& context, MotionPathComponent& path);

    void AddKeyFromView(EditorContext& context, CameraTrackComponent& track);
    void AddSpeedKey(EditorContext& context, MotionPathComponent& path);
    void AddPoint(EditorContext& context, MotionPathComponent& path, const glm::vec3& position);
    // Moves the playhead and poses the whole timeline there
    void SetPlayhead(EditorContext& context, float time);
    float TimelineEnd(EditorContext& context) const;
    // The camera track Play and scrubbing go through: the bound one while it has keys
    Entity PlayableTrack(EditorContext& context) const;

    Entity m_Track { entt::null };
    Entity m_Path { entt::null };
    // Bindings the visible range was last fitted to; rebinding reframes the timeline
    Entity m_FramedTrack { entt::null };
    Entity m_FramedPath { entt::null };
    int m_SelectedKey = -1;
    int m_SelectedSpeedKey = -1;
    int m_SelectedPoint = -1;
    float m_Playhead = 0.0f;

    // Rotation angles as last shown for key m_EulerKey of m_EulerTrack. Converting the quaternion
    // back folds angles past 90 deg pitch, so typed values stay while nothing else turns the key.
    Entity m_EulerTrack { entt::null };
    int m_EulerKey = -1;
    glm::quat m_EulerRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 m_EulerDegrees { 0.0f };

    // Visible time range, so zoom and pan survive between frames
    float m_ViewStart = 0.0f;
    float m_ViewEnd = 10.0f;

    // Timeline drag in progress
    DragKind m_Drag = DragKind::None;
    int m_DragKey = -1;
    float m_PanAnchorTime = 0.0f;

    EditorCommands::EntityNameLookup m_AimTargetLookup;
  };
}
