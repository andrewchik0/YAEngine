#include "Scene/SequencePlayer.h"

#include "Render/Render.h"
#include "Utils/Log.h"

#include <glm/gtc/matrix_transform.hpp>

namespace YAEngine
{
  namespace
  {
    // Resuming exactly at the end would stop again on the next tick
    constexpr double END_EPSILON = 1e-4;

    // Camera basis: looking down -Z with world up, the same convention the editor camera
    // and the snapshot use.
    glm::quat LookAtRotation(const glm::vec3& from, const glm::vec3& target)
    {
      glm::vec3 forward = target - from;
      float length = glm::length(forward);
      if (length <= 1e-5f)
        return glm::quat(1, 0, 0, 0);

      forward /= length;

      glm::vec3 up(0.0f, 1.0f, 0.0f);
      // Aiming straight up or down leaves world up parallel to the view direction and the
      // cross product degenerate; any horizontal vector rolls the camera predictably.
      if (std::abs(forward.y) > 0.999f)
        up = glm::vec3(0.0f, 0.0f, 1.0f);

      glm::vec3 right = glm::normalize(glm::cross(forward, up));
      glm::vec3 trueUp = glm::cross(right, forward);
      return glm::normalize(glm::quat_cast(glm::mat3(right, trueUp, -forward)));
    }

    // Rotation of a world matrix with the scale divided out. A non-uniformly scaled or
    // sheared parent has no single rotation to extract; camera parents are rigs, so this
    // stays an orthonormalization rather than a full polar decomposition.
    glm::quat WorldRotation(const glm::mat4& world)
    {
      glm::mat3 basis(world);
      for (int i = 0; i < 3; i++)
      {
        float length = glm::length(basis[i]);
        if (length > 1e-6f)
          basis[i] /= length;
      }
      return glm::normalize(glm::quat_cast(basis));
    }

    // Tracks and paths are authored in world space, but a pose is written into the
    // LocalTransform: under a parent that would be read as parent-relative and send the
    // entity somewhere else entirely.
    void SetWorldPose(Scene& scene, Entity entity, const glm::vec3& worldPosition, const glm::quat& worldRotation)
    {
      Entity parent = scene.HasComponent<HierarchyComponent>(entity)
        ? scene.GetHierarchy(entity).parent
        : Entity(entt::null);

      auto& transform = scene.GetTransform(entity);
      if (parent != entt::null && scene.HasComponent<WorldTransform>(parent))
      {
        const glm::mat4& parentWorld = scene.GetComponent<WorldTransform>(parent).world;
        transform.position = glm::vec3(glm::inverse(parentWorld) * glm::vec4(worldPosition, 1.0f));
        transform.rotation = glm::normalize(glm::inverse(WorldRotation(parentWorld)) * worldRotation);
      }
      else
      {
        transform.position = worldPosition;
        transform.rotation = worldRotation;
      }

      scene.MarkDirty(entity);
    }

    bool IsDrivablePath(const MotionPathComponent& path)
    {
      return path.points.size() >= 2;
    }
  }

  void SequencePlayer::ApplyTrackPose(Scene& scene, Entity trackEntity, float time)
  {
    if (trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity))
      return;
    if (!scene.HasComponent<CameraTrackComponent>(trackEntity))
      return;

    auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
    if (track.keys.empty())
      return;

    CameraTrackPose pose = EvaluateCameraTrack(track.keys, time);

    glm::quat worldRotation = pose.rotation;
    if (track.rotationMode == CameraTrackComponent::RotationMode::AimAt)
    {
      Entity target = track.aimTargetName.empty() ? Entity(entt::null)
        : FindEntityByName(scene.GetRegistry(), track.aimTargetName);
      if (target != entt::null && scene.HasComponent<WorldTransform>(target))
      {
        glm::vec3 targetPosition(scene.GetComponent<WorldTransform>(target).world[3]);
        worldRotation = LookAtRotation(pose.position, targetPosition);
      }
    }

    SetWorldPose(scene, trackEntity, pose.position, worldRotation);

    if (scene.HasComponent<CameraComponent>(trackEntity))
      scene.GetComponent<CameraComponent>(trackEntity).fov = pose.fov;
  }

  float SequencePlayer::ShotStart(const CameraTrackComponent& track)
  {
    return track.keys.empty() ? 0.0f : track.keys.front().time;
  }

  float SequencePlayer::ShotEnd(const CameraTrackComponent& track)
  {
    return track.keys.empty() ? 0.0f : track.keys.back().time;
  }

  double SequencePlayer::TimelineDuration(Scene& scene)
  {
    double end = 0.0;
    for (auto [entity, track] : scene.GetView<CameraTrackComponent>().each())
      end = std::max(end, double(ShotEnd(track)));
    for (auto [entity, path] : scene.GetView<MotionPathComponent>().each())
    {
      if (IsDrivablePath(path))
        end = std::max(end, double(MotionPathDuration(path.GetTable(), path.speedKeys)));
    }
    return end;
  }

  bool SequencePlayer::IsPlayableTrack(Scene& scene, Entity track) const
  {
    return track != entt::null && scene.GetRegistry().valid(track)
      && scene.HasComponent<CameraTrackComponent>(track)
      && scene.HasComponent<CameraComponent>(track)
      && !scene.GetComponent<CameraTrackComponent>(track).keys.empty();
  }

  void SequencePlayer::Play(Scene& scene, Entity cameraTrack)
  {
    if (cameraTrack != entt::null && !IsPlayableTrack(scene, cameraTrack))
    {
      if (!scene.GetRegistry().valid(cameraTrack))
        YA_LOG_WARN("Scene", "SequencePlayer: play requested for an invalid entity");
      else
        YA_LOG_WARN("Scene", "SequencePlayer: '%s' needs a camera and a camera track with keys",
          scene.GetName(cameraTrack).c_str());
      return;
    }

    Stop(scene);
    Begin(scene, cameraTrack);
    b_Advancing = true;
    TakeCamera(scene);

    // Without a track the view does not change, but everything on the timeline jumps to its start
    bool resetPostFX = cameraTrack == entt::null
      || scene.GetComponent<CameraTrackComponent>(cameraTrack).resetPostFXOnStart;
    if (resetPostFX && m_Render != nullptr)
    {
      m_Render->ResetTAAHistory();
      m_Render->ResetAutoExposure();
    }

    ApplyPoses(scene);
  }

  void SequencePlayer::Resume(Scene& scene)
  {
    if (!b_Active)
      return;

    if (m_CameraTrack != entt::null && !IsPlayableTrack(scene, m_CameraTrack))
    {
      Stop(scene);
      return;
    }

    UpdateRange(scene);

    bool restart = m_Time >= m_EndTime - END_EPSILON;
    if (restart)
      m_Time = m_StartTime;

    bool tookCamera = !b_HoldsCamera && m_CameraTrack != entt::null;
    TakeCamera(scene);
    b_Advancing = true;

    bool resetPostFX = m_CameraTrack == entt::null
      || scene.GetComponent<CameraTrackComponent>(m_CameraTrack).resetPostFXOnStart;
    if ((restart || tookCamera) && resetPostFX && m_Render != nullptr)
    {
      m_Render->ResetTAAHistory();
      m_Render->ResetAutoExposure();
    }

    ApplyPoses(scene);
  }

  void SequencePlayer::Stop(Scene& scene)
  {
    if (!b_Active)
      return;

    auto& registry = scene.GetRegistry();
    for (const SavedTransform& saved : m_Saved)
    {
      if (!registry.valid(saved.entity) || !scene.HasComponent<LocalTransform>(saved.entity))
        continue;
      scene.GetTransform(saved.entity) = saved.transform;
      scene.MarkDirty(saved.entity);
    }

    if (b_HoldsCamera && m_PreviousCamera != entt::null && registry.valid(m_PreviousCamera))
      scene.SetActiveCamera(m_PreviousCamera);

    m_Saved.clear();
    m_CameraTrack = entt::null;
    m_PreviousCamera = entt::null;
    m_Time = 0.0;
    m_StartTime = 0.0;
    m_EndTime = 0.0;
    b_Active = false;
    b_Advancing = false;
    b_HoldsCamera = false;
  }

  void SequencePlayer::Scrub(Scene& scene, Entity cameraTrack, double time)
  {
    if (cameraTrack != entt::null && !IsPlayableTrack(scene, cameraTrack))
      cameraTrack = entt::null;

    // A different shot is a different session: the old camera goes back first
    if (b_Active && cameraTrack != m_CameraTrack)
      Stop(scene);
    if (!b_Active)
      Begin(scene, cameraTrack);

    UpdateRange(scene);
    m_Time = glm::clamp(time, 0.0, std::max(TimelineDuration(scene), m_EndTime));
    ApplyPoses(scene);
  }

  void SequencePlayer::Update(Scene& scene, double dt)
  {
    if (!b_Active)
      return;

    if (m_CameraTrack != entt::null && !IsPlayableTrack(scene, m_CameraTrack))
    {
      Stop(scene);
      return;
    }

    if (b_Advancing)
    {
      // Paths edited mid-playback move the end of a camera-less session
      if (m_CameraTrack == entt::null)
        UpdateRange(scene);

      m_Time += dt;
      if (m_Time >= m_EndTime)
      {
        Stop(scene);
        return;
      }
    }

    ApplyPoses(scene);
  }

  void SequencePlayer::ProtectTransform(Scene& scene, Entity entity)
  {
    if (!b_Active || entity == entt::null || !scene.GetRegistry().valid(entity)
      || !scene.HasComponent<LocalTransform>(entity))
    {
      return;
    }

    for (const SavedTransform& saved : m_Saved)
    {
      if (saved.entity == entity)
        return;
    }

    m_Saved.push_back({ .entity = entity, .transform = scene.GetTransform(entity) });
  }

  void SequencePlayer::Begin(Scene& scene, Entity cameraTrack)
  {
    b_Active = true;
    b_Advancing = false;
    b_HoldsCamera = false;
    m_CameraTrack = cameraTrack;
    m_PreviousCamera = entt::null;
    m_Saved.clear();
    UpdateRange(scene);
    m_Time = m_StartTime;
  }

  void SequencePlayer::TakeCamera(Scene& scene)
  {
    if (b_HoldsCamera || m_CameraTrack == entt::null)
      return;

    m_PreviousCamera = scene.GetActiveCamera();
    ProtectTransform(scene, m_CameraTrack);
    scene.SetActiveCamera(m_CameraTrack);
    b_HoldsCamera = true;
  }

  void SequencePlayer::UpdateRange(Scene& scene)
  {
    if (m_CameraTrack != entt::null && IsPlayableTrack(scene, m_CameraTrack))
    {
      const auto& track = scene.GetComponent<CameraTrackComponent>(m_CameraTrack);
      m_StartTime = ShotStart(track);
      m_EndTime = ShotEnd(track);
      return;
    }

    m_StartTime = 0.0;
    m_EndTime = TimelineDuration(scene);
  }

  void SequencePlayer::ApplyPoses(Scene& scene)
  {
    float time = float(m_Time);

    if (m_CameraTrack != entt::null)
    {
      ProtectTransform(scene, m_CameraTrack);
      ApplyTrackPose(scene, m_CameraTrack, time);
    }

    // Collected first: posing marks entities dirty, which is no business of a live view
    std::vector<Entity> followers;
    for (auto [entity, path] : scene.GetView<MotionPathComponent>().each())
    {
      if (!IsDrivablePath(path))
        continue;
      ProtectTransform(scene, entity);
      if (!path.externallyDriven)
        followers.push_back(entity);
    }

    // A plain follower sits on the curve and faces the direction of travel
    for (Entity entity : followers)
    {
      const auto& path = scene.GetComponent<MotionPathComponent>(entity);
      const MotionPathPose pose = EvaluateMotionPath(path.GetTable(), path.speedKeys, time);
      SetWorldPose(scene, entity, pose.position, glm::angleAxis(pose.yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
    }
  }
}
