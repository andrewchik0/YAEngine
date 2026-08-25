#include "GameComponentSerializers.h"

#include <yaml-cpp/yaml.h>

#include "GameComponents.h"
#include "Scene/ComponentRegistry.h"

// YamlUtils only covers float quaternions; the wheel rest pose is a dquat
static YAML::Node SerializeQuatD(const glm::dquat& q)
{
  YAML::Node n;
  n.push_back(q.x);
  n.push_back(q.y);
  n.push_back(q.z);
  n.push_back(q.w);
  n.SetStyle(YAML::EmitterStyle::Flow);
  return n;
}

static glm::dquat DeserializeQuatD(const YAML::Node& n)
{
  return glm::dquat(n[3].as<double>(), n[0].as<double>(), n[1].as<double>(), n[2].as<double>());
}

void RegisterGameComponentSerializers(YAEngine::ComponentRegistry& registry)
{
  // Only tuning params are stored (sim state is rebuilt by ControlsLayer each frame); fields are written unconditionally since an all-default component would serialize as an empty map and get dropped as "nothing to store".
  registry.Register<VehicleComponent>("vehicle",
    [](const entt::registry& reg, entt::entity e) -> YAML::Node {
      auto& v = reg.get<VehicleComponent>(e);
      YAML::Node n;
      n["maxSteerAngle"] = v.maxSteerAngle;
      n["steerRate"] = v.steerRate;
      n["steerReturnRate"] = v.steerReturnRate;
      n["maxSpeed"] = v.maxSpeed;
      n["maxSpeedBack"] = v.maxSpeedBack;
      n["acceleration"] = v.acceleration;
      n["accelerationBack"] = v.accelerationBack;
      n["brake"] = v.brake;
      n["drag"] = v.drag;
      return n;
    },
    [](entt::registry& reg, entt::entity e, const YAML::Node& n) {
      VehicleComponent v;
      if (n["maxSteerAngle"]) v.maxSteerAngle = n["maxSteerAngle"].as<double>();
      if (n["steerRate"]) v.steerRate = n["steerRate"].as<double>();
      if (n["steerReturnRate"]) v.steerReturnRate = n["steerReturnRate"].as<double>();
      if (n["maxSpeed"]) v.maxSpeed = n["maxSpeed"].as<double>();
      if (n["maxSpeedBack"]) v.maxSpeedBack = n["maxSpeedBack"].as<double>();
      if (n["acceleration"]) v.acceleration = n["acceleration"].as<double>();
      if (n["accelerationBack"]) v.accelerationBack = n["accelerationBack"].as<double>();
      if (n["brake"]) v.brake = n["brake"].as<double>();
      if (n["drag"]) v.drag = n["drag"].as<double>();
      reg.emplace_or_replace<VehicleComponent>(e, v);
    }
  );

  // baseRot is stored rather than read back from the node transform: ControlsLayer spins that transform every frame, so a save mid-drive would otherwise bake the spin into the rest pose and drift further each time.
  registry.Register<WheelComponent>("wheel",
    [](const entt::registry& reg, entt::entity e) -> YAML::Node {
      auto& w = reg.get<WheelComponent>(e);
      YAML::Node n;
      n["isFront"] = w.isFront;
      n["radius"] = w.radius;
      n["baseRot"] = SerializeQuatD(w.baseRot);
      return n;
    },
    [](entt::registry& reg, entt::entity e, const YAML::Node& n) {
      WheelComponent w;
      if (n["isFront"]) w.isFront = n["isFront"].as<bool>();
      if (n["radius"]) w.radius = n["radius"].as<float>();
      if (n["baseRot"]) w.baseRot = DeserializeQuatD(n["baseRot"]);
      reg.emplace_or_replace<WheelComponent>(e, w);
    }
  );
}
