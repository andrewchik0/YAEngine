#pragma once

#include <array>
#include <random>
#include <vector>
#include <glm/glm.hpp>
#include "ParticleInstance.h"

class SparkPool
{
public:
  static constexpr uint32_t MAX_SPARKS = 512;

  struct Spark
  {
    glm::vec3 position { 0.0f };
    glm::vec3 trailPos { 0.0f };
    glm::vec3 velocity { 0.0f };
    glm::vec4 color { 1.0f };
    float life = 0.0f;
    float maxLife = 0.0f;
    float width = 0.08f;
  };

  void Emit(const glm::vec3& origin, const glm::vec3& normal, uint32_t count)
  {
    glm::vec3 nrm = glm::length(normal) > 1e-5f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    glm::vec3 tangentA = glm::abs(nrm.y) < 0.9f
      ? glm::normalize(glm::cross(nrm, glm::vec3(0, 1, 0)))
      : glm::normalize(glm::cross(nrm, glm::vec3(1, 0, 0)));
    glm::vec3 tangentB = glm::cross(nrm, tangentA);

    for (uint32_t i = 0; i < count; i++)
    {
      auto& s = m_Sparks[m_WriteIdx];
      m_WriteIdx = (m_WriteIdx + 1) % MAX_SPARKS;

      float speed = Rand(5.0f, 12.0f);
      float cone = 0.7f;
      glm::vec3 dir = glm::normalize(
        nrm +
        tangentA * Rand(-cone, cone) +
        tangentB * Rand(-cone, cone));

      s.position = origin;
      s.trailPos = origin;
      s.velocity = dir * speed + glm::vec3(0.0f, Rand(1.0f, 3.0f), 0.0f);
      s.maxLife = Rand(0.4f, 0.8f);
      s.life = s.maxLife;
      s.width = Rand(0.22f, 0.38f);
      s.color = glm::vec4(Rand(5.0f, 7.0f), Rand(3.0f, 4.5f), Rand(0.6f, 1.2f), 1.0f);
    }
  }

  void Update(double dt)
  {
    const glm::vec3 gravity(0.0f, -9.81f, 0.0f);
    const float drag = 2.5f;
    const float fdt = static_cast<float>(dt);
    const float trailHalflife = 0.06f;
    const float trailAlpha = 1.0f - std::exp(-fdt / trailHalflife);

    for (auto& s : m_Sparks)
    {
      if (s.life <= 0.0f) continue;
      s.velocity += gravity * fdt;
      s.velocity *= std::max(0.0f, 1.0f - drag * fdt);
      s.position += s.velocity * fdt;
      s.trailPos = glm::mix(s.trailPos, s.position, trailAlpha);
      s.life -= fdt;
    }
  }

  void FillInstances(std::vector<YAEngine::ParticleInstance>& out) const
  {
    for (const auto& s : m_Sparks)
    {
      if (s.life <= 0.0f) continue;

      float t = glm::clamp(s.life / std::max(s.maxLife, 1e-5f), 0.0f, 1.0f);

      YAEngine::ParticleInstance inst;
      inst.headAndWidth = glm::vec4(s.position, s.width);
      inst.tail = glm::vec4(s.trailPos, 0.0f);
      inst.color = glm::vec4(glm::vec3(s.color), s.color.a * t);
      out.push_back(inst);
    }
  }

  bool HasAliveSparks() const
  {
    for (const auto& s : m_Sparks)
      if (s.life > 0.0f) return true;
    return false;
  }

private:
  static float Rand(float lo, float hi)
  {
    static std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
  }

  std::array<Spark, MAX_SPARKS> m_Sparks {};
  uint32_t m_WriteIdx = 0;
};
