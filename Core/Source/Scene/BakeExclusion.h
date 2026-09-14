#pragma once

#include <entt/entt.hpp>

#include "Components.h"

#ifdef YA_EDITOR
namespace YAEngine
{
  enum class BakeInclusionReason : uint8_t
  {
    Default,
    IncludeOverride,
    ExcludeOverride,
    DynamicCollider
  };

  struct BakeInclusion
  {
    bool excluded = false;
    BakeInclusionReason reason = BakeInclusionReason::Default;
    // The entity carrying the deciding override or collider, null for Default.
    entt::entity source { entt::null };
  };

  // Whether bakes capture an entity. The nearest explicit BakeOverrideComponent on the
  // entity or an ancestor decides. Without one the entity is excluded when it or an
  // ancestor has a non-static collider: a bake records the scene as it stays, and a moving
  // object would be burned in wherever it happened to stand.
  //
  // Memoized per entity, so resolving a whole scene walks each hierarchy edge once. Built
  // per query batch; it must not outlive a hierarchy or component change.
  class BakeExclusionResolver
  {
  public:

    explicit BakeExclusionResolver(const entt::registry& registry);

    BakeInclusion Resolve(entt::entity entity);
    bool IsExcluded(entt::entity entity) { return Resolve(entity).excluded; }

  private:

    const entt::registry& m_Registry;
    std::unordered_map<entt::entity, BakeInclusion> m_Memo;
    std::vector<entt::entity> m_Chain;
  };

  // The same rule for a single entity, without the memo: one parent walk that allocates
  // nothing, for a caller that asks about one entity every frame.
  BakeInclusion ResolveBakeInclusion(const entt::registry& registry, entt::entity entity);
}
#endif
