#pragma once

#include "Editor/IEditorPanel.h"
#include "Editor/EditorCommands.h"
#include "Editor/SequencerEditing.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  // Edits what the Sequencer has bound and selected: the shot (its camera and track settings), the
  // selected camera key, and the car drive (the selected speed key and the motion path). The
  // Sequencer hotkeys work here too.
  class ShotInspectorPanel : public IEditorPanel
  {
  public:

    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "Shot Inspector", .category = EditorPanelCategory::Animation };

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;
    void OnSceneReady(EditorContext& context) override;

  private:

    enum class KeyAction : uint8_t { None, UpdateFromView, Duplicate, Delete };
    enum class PointAction : uint8_t { None, InsertAfter, Delete };

    // Relative drag fields of the Camera Key group, in the order they are drawn
    enum class Nudge : uint8_t { Dolly, Truck, Pedestal, Pan, Tilt, Roll, Count };

    void DrawShot(EditorContext& context, CameraTrackComponent& track);
    void DrawFollow(EditorContext& context, CameraTrackComponent& track);
    void DrawAim(EditorContext& context, CameraTrackComponent& track);
    void DrawMotion(EditorContext& context, const CameraTrackComponent& track);
    void DrawCameraKey(EditorContext& context, CameraTrackComponent& track);
    void DrawKeyNudge(EditorContext& context, CameraTrackComponent& track, int index);
    void DrawKeyBlend(EditorContext& context, CameraTrackComponent& track, int index);
    void DrawKeyAdvanced(EditorContext& context, CameraTrackComponent& track, int index);
    void DrawCarDrive(EditorContext& context, MotionPathComponent& path);
    void DrawSpeedKey(EditorContext& context, MotionPathComponent& path);
    void DrawPath(EditorContext& context, MotionPathComponent& path);
    void DrawPoints(EditorContext& context, MotionPathComponent& path);
    void AddPoint(EditorContext& context, MotionPathComponent& path, const glm::vec3& position);
    // The aim target resolves to an entity, so the keys' rotation is not what the camera shows
    bool IsAimOverridingRotation(EditorContext& context, const CameraTrackComponent& track);

    // Rotation angles as last shown for key m_EulerKey of m_EulerTrack. Converting the quaternion
    // back folds angles past 90 deg pitch, so typed values stay while nothing else turns the key.
    Entity m_EulerTrack { entt::null };
    int m_EulerKey = -1;
    glm::quat m_EulerRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 m_EulerDegrees { 0.0f };

    // What each nudge field shows while it is dragged; each frame applies only the change since the
    // last one, and the field goes back to 0 once it is let go
    std::array<float, size_t(Nudge::Count)> m_Nudge {};
    Entity m_NudgeTrack { entt::null };
    int m_NudgeKey = -1;

    EditorCommands::EntityNameLookup m_AimTargetLookup;
    EditorCommands::EntityNameLookup m_FollowTargetLookup;

    // Fastest camera motion over the whole shot, resampled when anything it depends on changes
    uint64_t m_MotionHash = 0;
    std::vector<SequencerEditing::MotionSample> m_MotionSamples;
    SequencerEditing::MotionSample m_TopSpeed;
    SequencerEditing::MotionSample m_TopTurn;
  };
}
