#include "Scene/CameraTrackPlayer.h"

#include "Render/Render.h"
#include "Utils/Log.h"

#include <glm/gtc/matrix_transform.hpp>

namespace YAEngine
{
  namespace
  {
    Entity FindEntityByName(Scene& scene, const std::string& name)
    {
      if (name.empty())
        return entt::null;

      for (auto [entity, entityName] : scene.GetView<Name>().each())
      {
        if (entityName == name)
          return entity;
      }
      return entt::null;
    }

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
  }

  void CameraTrackPlayer::ApplyTrackPose(Scene& scene, Entity trackEntity, float time)
  {
    if (trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity))
      return;
    if (!scene.HasComponent<CameraTrackComponent>(trackEntity))
      return;

    auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
    if (track.keys.empty())
      return;

    CameraTrackPose pose = EvaluateCameraTrack(track.keys, time);

    glm::vec3 worldPosition = pose.position;
    glm::quat worldRotation = pose.rotation;

    if (track.rotationMode == CameraTrackComponent::RotationMode::AimAt)
    {
      Entity target = FindEntityByName(scene, track.aimTargetName);
      if (target != entt::null && scene.HasComponent<WorldTransform>(target))
      {
        glm::vec3 targetPosition(scene.GetComponent<WorldTransform>(target).world[3]);
        worldRotation = LookAtRotation(worldPosition, targetPosition);
      }
    }

    // Keys are captured in world space, but the pose is written into the LocalTransform:
    // under a parent that would be read as parent-relative and send the camera somewhere
    // else entirely.
    Entity parent = scene.HasComponent<HierarchyComponent>(trackEntity)
      ? scene.GetHierarchy(trackEntity).parent
      : Entity(entt::null);

    auto& transform = scene.GetTransform(trackEntity);
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

    if (scene.HasComponent<CameraComponent>(trackEntity))
      scene.GetComponent<CameraComponent>(trackEntity).fov = pose.fov;

    scene.MarkDirty(trackEntity);
  }

  void CameraTrackPlayer::Start(Scene& scene, Entity trackEntity)
  {
    if (trackEntity == entt::null || !scene.GetRegistry().valid(trackEntity))
    {
      YA_LOG_WARN("Scene", "CameraTrackPlayer: start requested for an invalid entity");
      return;
    }

    if (!scene.HasComponent<CameraTrackComponent>(trackEntity)
      || !scene.HasComponent<CameraComponent>(trackEntity))
    {
      YA_LOG_WARN("Scene", "CameraTrackPlayer: entity '%s' needs both a camera track and a camera",
        scene.GetName(trackEntity).c_str());
      return;
    }

    auto& track = scene.GetComponent<CameraTrackComponent>(trackEntity);
    if (track.keys.empty())
    {
      YA_LOG_WARN("Scene", "CameraTrackPlayer: track on '%s' has no keys",
        scene.GetName(trackEntity).c_str());
      return;
    }

    m_PreviousCamera = scene.GetActiveCamera();
    m_TrackEntity = trackEntity;
    m_Elapsed = 0.0;
    b_Playing = true;
    b_Paused = false;

    scene.SetActiveCamera(trackEntity);
    ApplyTrackPose(scene, trackEntity, float(m_Elapsed));

    if (track.resetPostFXOnStart && m_Render != nullptr)
    {
      m_Render->ResetTAAHistory();
      m_Render->ResetAutoExposure();
    }
  }

  void CameraTrackPlayer::Stop(Scene& scene)
  {
    if (!b_Playing)
      return;

    b_Playing = false;
    b_Paused = false;
    m_TrackEntity = entt::null;

    if (m_PreviousCamera != entt::null && scene.GetRegistry().valid(m_PreviousCamera))
      scene.SetActiveCamera(m_PreviousCamera);

    m_PreviousCamera = entt::null;
  }

  void CameraTrackPlayer::SetElapsed(Scene& scene, double time)
  {
    if (!b_Playing || m_TrackEntity == entt::null
      || !scene.GetRegistry().valid(m_TrackEntity)
      || !scene.HasComponent<CameraTrackComponent>(m_TrackEntity))
      return;

    auto& track = scene.GetComponent<CameraTrackComponent>(m_TrackEntity);
    if (track.keys.empty())
      return;

    m_Elapsed = std::clamp(time, 0.0, double(track.keys.back().time));
    ApplyTrackPose(scene, m_TrackEntity, float(m_Elapsed));
  }

  void CameraTrackPlayer::Update(Scene& scene, double dt)
  {
    if (!b_Playing || b_Paused)
      return;

    if (m_TrackEntity == entt::null || !scene.GetRegistry().valid(m_TrackEntity)
      || !scene.HasComponent<CameraTrackComponent>(m_TrackEntity))
    {
      Stop(scene);
      return;
    }

    auto& track = scene.GetComponent<CameraTrackComponent>(m_TrackEntity);
    if (track.keys.empty())
    {
      Stop(scene);
      return;
    }

    m_Elapsed += dt;

    ApplyTrackPose(scene, m_TrackEntity, float(m_Elapsed));

    if (m_Elapsed >= double(track.keys.back().time))
      Stop(scene);
  }
}
