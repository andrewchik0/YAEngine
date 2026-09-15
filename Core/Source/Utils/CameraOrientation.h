#pragma once

#include "Pch.h"

namespace YAEngine
{
  // The editor camera convention: yaw about world up, then pitch about the camera's own X axis,
  // looking down -Z. A capture shot uses the same composition, so a yaw the editor wrote and a
  // yaw a shot asks for point the same way.
  inline glm::quat MakeYawPitchRotation(float yawRadians, float pitchRadians)
  {
    glm::quat pitch = glm::angleAxis(pitchRadians, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::quat yaw = glm::angleAxis(yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::normalize(yaw * pitch);
  }

  // Inverse of MakeYawPitchRotation for a unit forward vector.
  inline void YawPitchFromForward(const glm::vec3& forward, float& outYawRadians, float& outPitchRadians)
  {
    outPitchRadians = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
    outYawRadians = std::atan2(-forward.x, -forward.z);
  }

  // MakeYawPitchRotation followed by a roll about the camera's own view axis. Radians, as
  // (yaw, pitch, roll).
  inline glm::quat MakeYawPitchRollRotation(const glm::vec3& yawPitchRollRadians)
  {
    glm::quat roll = glm::angleAxis(yawPitchRollRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::normalize(MakeYawPitchRotation(yawPitchRollRadians.x, yawPitchRollRadians.y) * roll);
  }

  // Inverse of MakeYawPitchRollRotation, pitch within +-90 deg. Looking straight up or down, yaw
  // and roll turn about the same axis, so the whole turn is reported as yaw.
  inline glm::vec3 YawPitchRollFromRotation(const glm::quat& rotation)
  {
    // Indexed [column][row]: row 1 of column 2 is -sin(pitch)
    const glm::mat3 basis = glm::mat3_cast(glm::normalize(rotation));
    const float sinPitch = glm::clamp(-basis[2][1], -1.0f, 1.0f);
    const float pitch = std::asin(sinPitch);
    if (std::abs(sinPitch) < 0.9999f)
      return glm::vec3(std::atan2(basis[2][0], basis[2][2]), pitch, std::atan2(basis[0][1], basis[1][1]));
    return glm::vec3(std::atan2(-basis[0][2], basis[0][0]), pitch, 0.0f);
  }
}
