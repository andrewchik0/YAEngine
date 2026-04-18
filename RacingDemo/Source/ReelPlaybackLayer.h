#pragma once

#include "Layer.h"
#include "ReelFormat.h"

#include <string>

class ReelPlaybackLayer : public YAEngine::Layer
{
public:
  void SetReelPath(const std::string& path) { m_ReelPath = path; }

  void Update(double deltaTime) override;

private:
  void Start();
  void Stop();

  Reel m_Reel;
  std::string m_ReelPath;
  bool m_Playing = false;
  double m_Elapsed = 0.0;
};
