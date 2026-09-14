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
}
