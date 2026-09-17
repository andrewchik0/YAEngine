#include "Scene/SequencePlayer.h"

#include "Render/Render.h"
#include "Utils/Log.h"

#include <glm/gtc/constants.hpp>
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

    bool HasDrivablePath(Scene& scene, Entity entity)
    {
      return entity != entt::null && scene.GetRegistry().valid(entity)
        && scene.HasComponent<MotionPathComponent>(entity)
        && IsDrivablePath(scene.GetComponent<MotionPathComponent>(entity));
    }

    const glm::vec3 WORLD_UP(0.0f, 1.0f, 0.0f);

    // Smoothing is a centred window sampled at fixed offsets rather than a filter with
    // state, so a pose stays a pure function of time and scrubbing stays exact
    constexpr int WINDOW_SAMPLES = 9;

    float WindowSampleTime(float time, float window, int index)
    {
      return time + window * (float(index) / float(WINDOW_SAMPLES - 1) - 0.5f);
    }

    bool ResolveEntityFrame(Scene& scene, Entity entity, float time,
      CameraTrackComponent::FollowRotation rotation, float smoothing, CameraTargetFrame& out)
    {
      using FollowRotation = CameraTrackComponent::FollowRotation;

      if (entity == entt::null || !scene.GetRegistry().valid(entity))
        return false;

      if (HasDrivablePath(scene, entity))
      {
        const auto& path = scene.GetComponent<MotionPathComponent>(entity);
        const MotionPathTable& table = path.GetTable();
        const MotionPathPose pose = EvaluateMotionPath(table, path.speedKeys, time);
        out.position = pose.position;

        float yaw = pose.yaw;
        if (rotation == FollowRotation::Smoothed && smoothing > 0.0f)
        {
          float previous = EvaluateMotionPath(table, path.speedKeys, WindowSampleTime(time, smoothing, 0)).yaw;
          float sum = previous;
          for (int i = 1; i < WINDOW_SAMPLES; i++)
          {
            float sample = EvaluateMotionPath(table, path.speedKeys, WindowSampleTime(time, smoothing, i)).yaw;
            sample = previous + std::remainder(sample - previous, glm::two_pi<float>());
            sum += sample;
            previous = sample;
          }
          yaw = sum / float(WINDOW_SAMPLES);
        }

        out.rotation = rotation == FollowRotation::PositionOnly ? glm::quat(1, 0, 0, 0)
          : glm::angleAxis(yaw, WORLD_UP);
        return true;
      }

      if (!scene.HasComponent<WorldTransform>(entity))
        return false;

      const glm::mat4& world = scene.GetComponent<WorldTransform>(entity).world;
      out.position = glm::vec3(world[3]);
      out.rotation = rotation == FollowRotation::PositionOnly ? glm::quat(1, 0, 0, 0) : WorldRotation(world);
      return true;
    }

    bool ResolveAimPoint(Scene& scene, Entity target, const CameraTrackComponent& track, float time,
      glm::vec3& out)
    {
      auto sample = [&](float at, glm::vec3& point)
      {
        CameraTargetFrame frame;
        if (!ResolveEntityFrame(scene, target, at, track.followRotation, track.followSmoothing, frame))
          return false;
        point = frame.position + frame.rotation * track.aimOffset;
        return true;
      };

      // A plain transform has no history to average
      if (track.aimSmoothing <= 0.0f || !HasDrivablePath(scene, target))
        return sample(time, out);

      glm::vec3 sum(0.0f);
      for (int i = 0; i < WINDOW_SAMPLES; i++)
      {
        glm::vec3 point;
        if (!sample(WindowSampleTime(time, track.aimSmoothing, i), point))
          return false;
        sum += point;
      }
      out = sum / float(WINDOW_SAMPLES);
      return true;
    }

    // Look-at that puts the aim point at a normalized image position instead of the centre.
    // Solved directly for the yaw and pitch of a camera without roll, so the point lands
    // exactly where asked at any pitch; rotating a centred look-at by the offset angles
    // would drift off it as soon as the camera pitches.
    glm::quat AimRotation(const glm::vec3& from, const glm::vec3& aimPoint, const glm::vec2& screenOffset,
      float fov, float aspect)
    {
      glm::quat lookAt = LookAtRotation(from, aimPoint);
      if (screenOffset == glm::vec2(0.0f))
        return lookAt;

      glm::vec3 toAim = aimPoint - from;
      float distance = glm::length(toAim);
      // Too close to vertical for a yaw: the look-at fallback basis stays
      if (distance <= 1e-5f || std::abs(toAim.y / distance) > 0.999f)
        return lookAt;
      glm::vec3 direction = toAim / distance;

      // View-space direction the aim point must have to project onto screenOffset
      float tanHalf = std::tan(0.5f * fov);
      glm::vec3 view = glm::normalize(glm::vec3(screenOffset.x * tanHalf * aspect, screenOffset.y * tanHalf, -1.0f));

      // Yaw leaves heights alone, so the pitch alone brings the view direction to the height
      // of the aim direction: view.y cos(p) - view.z sin(p) = direction.y
      float reach = glm::length(glm::vec2(view.y, view.z));
      float pitch = -std::atan2(view.z, view.y) - std::acos(glm::clamp(direction.y / reach, -1.0f, 1.0f));
      glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
      glm::vec3 pitched = pitchRotation * view;
      float yaw = std::atan2(direction.x, direction.z) - std::atan2(pitched.x, pitched.z);
      return glm::normalize(glm::angleAxis(yaw, WORLD_UP) * pitchRotation);
    }

    // The follow frame of a track entity, or false with nothing to follow
    bool ResolveTrackFrame(Scene& scene, Entity trackEntity, float time, CameraTargetFrame& out)
    {
      if (trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity)
        || !scene.HasComponent<CameraTrackComponent>(trackEntity))
      {
        return false;
      }
      return SequencePlayer::ResolveTargetFrame(scene, scene.GetComponent<CameraTrackComponent>(trackEntity),
        time, out);
    }
  }

  bool SequencePlayer::ResolveTargetFrame(Scene& scene, const CameraTrackComponent& track, float time,
    CameraTargetFrame& out)
  {
    if (track.followTargetName.empty())
      return false;

    Entity target = FindEntityByName(scene.GetRegistry(), track.followTargetName);
    // A camera following itself would read back its own last pose and run away
    if (target != entt::null && scene.GetRegistry().valid(target)
      && scene.HasComponent<CameraTrackComponent>(target)
      && &scene.GetComponent<CameraTrackComponent>(target) == &track)
    {
      return false;
    }

    return ResolveEntityFrame(scene, target, time, track.followRotation, track.followSmoothing, out);
  }

  bool SequencePlayer::EvaluateTrackPose(Scene& scene, Entity trackEntity, float time, CameraTrackPose& out)
  {
    return EvaluateTrackPose(scene, trackEntity, FindTrackTargets(scene, trackEntity), time, out);
  }

  SequencePlayer::TrackTargets SequencePlayer::FindTrackTargets(Scene& scene, Entity trackEntity)
  {
    TrackTargets targets;
    if (trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity)
      || !scene.HasComponent<CameraTrackComponent>(trackEntity))
    {
      return targets;
    }

    const auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
    if (!track.followTargetName.empty())
    {
      targets.follow = FindEntityByName(scene.GetRegistry(), track.followTargetName);
      // The same guard as ResolveTargetFrame: a camera following itself would run away
      if (targets.follow == trackEntity)
        targets.follow = entt::null;
    }
    if (!track.aimTargetName.empty())
      targets.aim = FindEntityByName(scene.GetRegistry(), track.aimTargetName);
    return targets;
  }

  bool SequencePlayer::ResolveTargetFrame(Scene& scene, Entity trackEntity, const TrackTargets& targets, float time,
    CameraTargetFrame& out)
  {
    if (trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity)
      || !scene.HasComponent<CameraTrackComponent>(trackEntity))
    {
      return false;
    }

    const auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
    return ResolveEntityFrame(scene, targets.follow, time, track.followRotation, track.followSmoothing, out);
  }

  bool SequencePlayer::EvaluateTrackPose(Scene& scene, Entity trackEntity, const TrackTargets& targets, float time,
    CameraTrackPose& out)
  {
    if (trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity))
      return false;
    if (!scene.HasComponent<CameraTrackComponent>(trackEntity))
      return false;

    const auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
    if (track.keys.empty())
      return false;

    CameraTargetFrame frame;
    const bool following = ResolveEntityFrame(scene, targets.follow, time, track.followRotation,
      track.followSmoothing, frame);
    out = EvaluateCameraTrack(track.keys, time, following ? &frame : nullptr);

    if (track.rotationMode == CameraTrackComponent::RotationMode::AimAt && !track.aimTargetName.empty())
    {
      glm::vec3 aimPoint;
      if (ResolveAimPoint(scene, targets.aim, track, time, aimPoint))
      {
        float aspect = scene.HasComponent<CameraComponent>(trackEntity)
          ? scene.GetComponent<CameraComponent>(trackEntity).aspectRatio
          : CameraComponent {}.aspectRatio;
        out.rotation = AimRotation(out.position, aimPoint, track.aimScreenOffset, out.fov, aspect);
      }
    }

    return true;
  }

  CameraTrackKey SequencePlayer::KeyFromWorldPose(Scene& scene, Entity trackEntity, const CameraTrackPose& world,
    CameraKeySpace space, float time)
  {
    CameraTrackKey key;
    key.time = time;
    key.position = world.position;
    key.rotation = world.rotation;
    key.fov = world.fov;

    CameraTargetFrame frame;
    if (space == CameraKeySpace::Target && ResolveTrackFrame(scene, trackEntity, time, frame))
    {
      glm::quat toFrame = glm::inverse(frame.rotation);
      key.space = CameraKeySpace::Target;
      key.position = toFrame * (world.position - frame.position);
      key.rotation = glm::normalize(toFrame * world.rotation);
    }
    return key;
  }

  CameraTrackPose SequencePlayer::KeyToWorldPose(Scene& scene, Entity trackEntity, const CameraTrackKey& key,
    float time)
  {
    CameraTrackPose pose { .position = key.position, .rotation = key.rotation, .fov = key.fov };

    CameraTargetFrame frame;
    if (key.space == CameraKeySpace::Target && ResolveTrackFrame(scene, trackEntity, time, frame))
    {
      pose.position = frame.rotation * key.position + frame.position;
      pose.rotation = glm::normalize(frame.rotation * key.rotation);
    }
    return pose;
  }

  CameraTrackPose SequencePlayer::KeyToWorldPose(Scene& scene, Entity trackEntity, const TrackTargets& targets,
    const CameraTrackKey& key, float time)
  {
    CameraTrackPose pose { .position = key.position, .rotation = key.rotation, .fov = key.fov };
    if (key.space != CameraKeySpace::Target || trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity)
      || !scene.HasComponent<CameraTrackComponent>(trackEntity))
    {
      return pose;
    }

    const auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
    CameraTargetFrame frame;
    if (ResolveEntityFrame(scene, targets.follow, time, track.followRotation, track.followSmoothing, frame))
    {
      pose.position = frame.rotation * key.position + frame.position;
      pose.rotation = glm::normalize(frame.rotation * key.rotation);
    }
    return pose;
  }

  void SequencePlayer::ApplyTrackPose(Scene& scene, Entity trackEntity, float time)
  {
    CameraTrackPose pose;
    if (!EvaluateTrackPose(scene, trackEntity, time, pose))
      return;

    SetWorldPose(scene, trackEntity, pose.position, pose.rotation);

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
