#include "ReelFormat.h"

#include "Scene/YamlUtils.h"
#include "Utils/Log.h"

#include <yaml-cpp/yaml.h>
#include <fstream>

Reel LoadReel(const std::string& path)
{
  Reel reel;

  std::ifstream file(path);
  if (!file.is_open())
  {
    YA_LOG_INFO("Render", "Reel not found at %s (optional)", path.c_str());
    return reel;
  }

  try
  {
    YAML::Node root = YAML::Load(file);

    if (root["duration"])
      reel.duration = root["duration"].as<double>();
    if (root["resetTAA"])
      reel.resetTAA = root["resetTAA"].as<bool>();
    if (root["resetAutoExposure"])
      reel.resetAutoExposure = root["resetAutoExposure"].as<bool>();

    if (root["carStart"])
    {
      auto cs = root["carStart"];
      if (cs["position"])
        reel.carStartPos = YAEngine::DeserializeVec3(cs["position"]);
      if (cs["rotation"])
        reel.carStartRot = YAEngine::DeserializeQuat(cs["rotation"]);
    }

    if (root["input"])
    {
      for (auto n : root["input"])
      {
        CarInputKey k;
        if (n["time"]) k.time = n["time"].as<double>();
        if (n["up"])    k.up    = n["up"].as<bool>();
        if (n["down"])  k.down  = n["down"].as<bool>();
        if (n["left"])  k.left  = n["left"].as<bool>();
        if (n["right"]) k.right = n["right"].as<bool>();
        reel.inputTrack.push_back(k);
      }
    }

    // Keys must be sorted by time for correct step sampling.
    std::sort(reel.inputTrack.begin(), reel.inputTrack.end(),
      [](const CarInputKey& a, const CarInputKey& b) { return a.time < b.time; });

    reel.valid = true;
    YA_LOG_INFO("Render", "Reel loaded: %s (duration=%.2fs, %zu input keys)",
      path.c_str(), reel.duration, reel.inputTrack.size());
  }
  catch (const std::exception& e)
  {
    YA_LOG_ERROR("Render", "Failed to parse reel %s: %s", path.c_str(), e.what());
    reel = Reel{};
  }

  return reel;
}

CarInputKey SampleCarInput(const Reel& reel, double elapsed)
{
  CarInputKey active;
  for (const auto& k : reel.inputTrack)
  {
    if (k.time > elapsed) break;
    active = k;
  }
  return active;
}
