#pragma once

#include <yaml-cpp/yaml.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace YAEngine
{
  inline YAML::Node SerializeVec3(const glm::vec3& v)
  {
    YAML::Node n;
    n.push_back(v.x);
    n.push_back(v.y);
    n.push_back(v.z);
    n.SetStyle(YAML::EmitterStyle::Flow);
    return n;
  }

  inline glm::vec3 DeserializeVec3(const YAML::Node& n)
  {
    return { n[0].as<float>(), n[1].as<float>(), n[2].as<float>() };
  }

  inline YAML::Node SerializeQuat(const glm::quat& q)
  {
    YAML::Node n;
    n.push_back(q.x);
    n.push_back(q.y);
    n.push_back(q.z);
    n.push_back(q.w);
    n.SetStyle(YAML::EmitterStyle::Flow);
    return n;
  }

  inline glm::quat DeserializeQuat(const YAML::Node& n)
  {
    return glm::quat(n[3].as<float>(), n[0].as<float>(), n[1].as<float>(), n[2].as<float>());
  }
}
