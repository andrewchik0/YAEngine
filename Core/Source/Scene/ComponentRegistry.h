#pragma once

#include <yaml-cpp/yaml.h>
#include <entt/entt.hpp>

namespace YAEngine
{
  class ComponentRegistry
  {
  public:

    using SerializeFn = std::function<YAML::Node(const entt::registry&, entt::entity)>;
    using DeserializeFn = std::function<void(entt::registry&, entt::entity, const YAML::Node&)>;
    using HasFn = std::function<bool(const entt::registry&, entt::entity)>;

    struct Entry
    {
      std::string name;
      SerializeFn serialize;
      DeserializeFn deserialize;
      HasFn has;
    };

    template<typename T>
    void Register(const std::string& name, SerializeFn serialize, DeserializeFn deserialize)
    {
      Entry entry;
      entry.name = name;
      entry.serialize = std::move(serialize);
      entry.deserialize = std::move(deserialize);
      entry.has = [](const entt::registry& reg, entt::entity e) {
        return reg.all_of<T>(e);
      };
      m_Entries[name] = std::move(entry);
    }

    YAML::Node Serialize(const std::string& name, const entt::registry& reg, entt::entity e) const
    {
      auto it = m_Entries.find(name);
      if (it == m_Entries.end())
        return {};
      return it->second.serialize(reg, e);
    }

    bool Deserialize(const std::string& name, entt::registry& reg, entt::entity e, const YAML::Node& node) const
    {
      auto it = m_Entries.find(name);
      if (it == m_Entries.end())
        return false;
      it->second.deserialize(reg, e, node);
      return true;
    }

    YAML::Node SerializeAll(const entt::registry& reg, entt::entity e) const
    {
      YAML::Node result;
      for (auto& [name, entry] : m_Entries)
      {
        if (entry.has(reg, e))
          result[name] = entry.serialize(reg, e);
      }
      return result;
    }

    void DeserializeAll(entt::registry& reg, entt::entity e, const YAML::Node& node) const
    {
      for (auto it = node.begin(); it != node.end(); ++it)
      {
        auto name = it->first.as<std::string>();
        Deserialize(name, reg, e, it->second);
      }
    }

    bool Has(const std::string& name) const
    {
      return m_Entries.contains(name);
    }

    const std::unordered_map<std::string, Entry>& GetEntries() const { return m_Entries; }

  private:
    std::unordered_map<std::string, Entry> m_Entries;
  };
}
