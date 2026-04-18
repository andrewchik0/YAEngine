#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

struct CarInputKey
{
  double time = 0.0;
  bool up = false;
  bool down = false;
  bool left = false;
  bool right = false;
};

struct Reel
{
  double duration = 0.0;
  bool resetTAA = true;
  bool resetAutoExposure = true;
  glm::vec3 carStartPos { 0.0f };
  glm::quat carStartRot { 1.0f, 0.0f, 0.0f, 0.0f };
  std::vector<CarInputKey> inputTrack;

  bool valid = false;
};

// Loads a .reel yaml. On error logs YA_LOG_ERROR and returns Reel with valid=false.
Reel LoadReel(const std::string& path);

// Returns active input for given elapsed time (step interpolation -- each key holds until next).
CarInputKey SampleCarInput(const Reel& reel, double elapsed);
