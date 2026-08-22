#include "PositionWelder.h"

namespace YAEngine
{
  namespace
  {
    struct PositionKey
    {
      uint32_t bits[3];

      bool operator==(const PositionKey& other) const
      {
        return bits[0] == other.bits[0] && bits[1] == other.bits[1] && bits[2] == other.bits[2];
      }
    };

    struct PositionKeyHash
    {
      size_t operator()(const PositionKey& key) const
      {
        size_t h = 1469598103934665603ull;
        for (uint32_t b : key.bits)
        {
          h ^= b;
          h *= 1099511628211ull;
        }
        return h;
      }
    };

    PositionKey MakeKey(const glm::vec3& p)
    {
      PositionKey key {};
      std::memcpy(key.bits, &p, sizeof(key.bits));
      return key;
    }
  }

  WeldedPositions PositionWelder::Weld(const glm::vec3* positions, size_t vertexCount,
    const std::vector<uint32_t>& indices, float worthwhileRatio)
  {
    WeldedPositions result;

    if (positions == nullptr || vertexCount == 0 || indices.empty())
      return result;

    std::unordered_map<PositionKey, uint32_t, PositionKeyHash> remap;
    remap.reserve(vertexCount);

    result.indices.resize(indices.size());
    result.positions.reserve(vertexCount);

    // Walking the index stream instead of the vertex array orders unique
    // positions by first use, so the welded buffer is fetched front to back.
    for (size_t i = 0; i < indices.size(); i++)
    {
      uint32_t source = indices[i];
      if (source >= vertexCount)
        return {};

      const glm::vec3& position = positions[source];
      auto [it, inserted] = remap.try_emplace(MakeKey(position), uint32_t(result.positions.size()));
      if (inserted)
        result.positions.push_back(position);

      result.indices[i] = it->second;
    }

    result.worthwhile = float(result.positions.size()) < float(vertexCount) * worthwhileRatio;

    if (!result.worthwhile)
    {
      result.positions.clear();
      result.positions.shrink_to_fit();
      result.indices.clear();
      result.indices.shrink_to_fit();
    }

    return result;
  }
}
