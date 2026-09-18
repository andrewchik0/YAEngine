#include "Editor/Bridge/BridgeActions.h"

#include "Editor/Bridge/BridgeTypes.h"
#include "Scene/Components.h"
#include "Utils/Log.h"
#include "Utils/ServiceRegistry.h"

namespace YAEngine
{
  namespace
  {
    using EntityId = std::underlying_type_t<Entity>;

    const char* GetParamTypeName(BridgeActionParamType type)
    {
      switch (type)
      {
        case BridgeActionParamType::Entity: return "entity";
        case BridgeActionParamType::String: return "string";
        case BridgeActionParamType::Number: return "number";
        case BridgeActionParamType::Integer: return "integer";
        case BridgeActionParamType::Bool: return "bool";
        case BridgeActionParamType::Vec3: return "vec3";
        case BridgeActionParamType::Path: return "path";
      }
      return "string";
    }

    bool IsInt64(const Json& value)
    {
      return value.is_number_integer()
        && !(value.is_number_unsigned() && value.get<uint64_t>() > uint64_t(std::numeric_limits<int64_t>::max()));
    }

    // Empty when the value has the declared type.
    std::string CheckParamType(const BridgeActionParam& param, const Json& value)
    {
      const std::string expected = "'" + param.name + "' must be ";
      switch (param.type)
      {
        case BridgeActionParamType::Entity:
        {
          bool valid = IsInt64(value) && value.get<int64_t>() >= 0
            && value.get<int64_t>() <= int64_t(std::numeric_limits<EntityId>::max());
          return valid ? std::string() : expected + "an entity id as listed by scene.entities";
        }
        case BridgeActionParamType::String:
          return value.is_string() ? std::string() : expected + "a string";
        case BridgeActionParamType::Path:
          return value.is_string() && !value.get_ref<const std::string&>().empty()
            ? std::string() : expected + "a non-empty path";
        case BridgeActionParamType::Number:
          return value.is_number() ? std::string() : expected + "a number";
        case BridgeActionParamType::Integer:
          return IsInt64(value) ? std::string() : expected + "an integer";
        case BridgeActionParamType::Bool:
          return value.is_boolean() ? std::string() : expected + "a boolean";
        case BridgeActionParamType::Vec3:
        {
          bool valid = value.is_array() && value.size() == 3
            && std::all_of(value.begin(), value.end(), [](const Json& component) { return component.is_number(); });
          return valid ? std::string() : expected + "an array of three numbers";
        }
      }
      return {};
    }

    std::string ListParamNames(const BridgeAction& action)
    {
      if (action.params.empty())
        return "none";

      std::string list;
      for (const BridgeActionParam& param : action.params)
        list += (list.empty() ? "" : ", ") + param.name;
      return list;
    }
  }

  bool BridgeActionArgs::Has(const char* name) const
  {
    auto it = m_Params.find(name);
    return it != m_Params.end() && !it->is_null();
  }

  Entity BridgeActionArgs::GetEntity(const char* name) const
  {
    return static_cast<Entity>(static_cast<EntityId>(m_Params.at(name).get<int64_t>()));
  }

  std::string BridgeActionArgs::GetString(const char* name) const
  {
    return m_Params.at(name).get<std::string>();
  }

  double BridgeActionArgs::GetNumber(const char* name) const
  {
    return m_Params.at(name).get<double>();
  }

  int64_t BridgeActionArgs::GetInteger(const char* name) const
  {
    return m_Params.at(name).get<int64_t>();
  }

  bool BridgeActionArgs::GetBool(const char* name) const
  {
    return m_Params.at(name).get<bool>();
  }

  glm::vec3 BridgeActionArgs::GetVec3(const char* name) const
  {
    const Json& value = m_Params.at(name);
    return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
  }

  void BridgeActions::Init(ServiceRegistry& registry, BridgeMethodRegistry& methods, std::function<bool()> isCaptureBusy,
    std::function<bool()> isMinimized)
  {
    m_Registry = &registry;
    m_IsCaptureBusy = std::move(isCaptureBusy);
    m_IsMinimized = std::move(isMinimized);

    methods.Register("actions.list", [this](const Json&, const BridgeReply& reply) {
      HandleList(reply);
    });

    methods.Register("actions.run", [this](const Json& params, const BridgeReply& reply) {
      HandleRun(params, reply);
    });
  }

  void BridgeActions::Register(BridgeAction action)
  {
    if (m_Actions.contains(action.name))
      YA_LOG_WARN("Bridge", "Action '%s' registered twice; the later one replaces the earlier one", action.name.c_str());

    std::string name = action.name;
    m_Actions[name] = std::move(action);
  }

  bool BridgeActions::CompletesLater(const std::string& name) const
  {
    auto it = m_Actions.find(name);
    return it != m_Actions.end() && it->second.refusedWhileMinimized;
  }

  void BridgeActions::Defer(BridgeReply reply, std::function<bool(const BridgeReply& reply)> poll)
  {
    m_Pending.push_back(PendingRun { .reply = std::move(reply), .poll = std::move(poll) });
  }

  void BridgeActions::LateUpdate()
  {
    if (m_Pending.empty())
      return;

    std::vector<PendingRun> pending = std::move(m_Pending);
    m_Pending.clear();

    for (PendingRun& run : pending)
    {
      bool finished = true;
      try
      {
        finished = run.poll(run.reply);
      }
      catch (const std::exception& e)
      {
        YA_LOG_ERROR("Bridge", "A deferred action failed: %s", e.what());
        run.reply.Fail(BridgeErrorCode::INTERNAL, e.what());
      }
      catch (...)
      {
        YA_LOG_ERROR("Bridge", "A deferred action threw a non-standard exception");
        run.reply.Fail(BridgeErrorCode::INTERNAL, "The deferred action threw a non-standard exception");
      }

      if (!finished)
        m_Pending.push_back(std::move(run));
    }
  }

  void BridgeActions::Cancel(std::string_view reason)
  {
    std::vector<PendingRun> pending = std::move(m_Pending);
    m_Pending.clear();

    for (PendingRun& run : pending)
      run.reply.Fail(BridgeErrorCode::FAILED, reason);
  }

  void BridgeActions::HandleList(const BridgeReply& reply) const
  {
    Json actions = Json::array();
    for (const auto& [name, action] : m_Actions)
    {
      Json params = Json::array();
      for (const BridgeActionParam& param : action.params)
      {
        Json entry = Json::object();
        entry["name"] = param.name;
        entry["type"] = GetParamTypeName(param.type);
        entry["required"] = param.required;
        entry["description"] = param.description;
        params.push_back(std::move(entry));
      }

      Json entry = Json::object();
      entry["name"] = name;
      entry["description"] = action.description;
      entry["params"] = std::move(params);
      actions.push_back(std::move(entry));
    }

    Json result = Json::object();
    result["actions"] = std::move(actions);
    reply.Ok(std::move(result));
  }

  void BridgeActions::HandleRun(const Json& params, const BridgeReply& reply)
  {
    std::string name;
    std::string error;
    if (!ReadOptionalParam(params, "name", name, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }

    if (name.empty())
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'name' must name an action from actions.list");
      return;
    }

    Json runParams = Json::object();
    auto paramsIt = params.find("params");
    if (paramsIt != params.end() && !paramsIt->is_null())
    {
      if (!paramsIt->is_object())
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'params' must be an object of the action's params");
        return;
      }
      runParams = *paramsIt;
    }

    auto it = m_Actions.find(name);
    if (it == m_Actions.end())
    {
      reply.Fail(BridgeErrorCode::NOT_FOUND, "no action '" + name + "'; actions.list names every action");
      return;
    }

    const BridgeAction& action = it->second;
    if (!ValidateParams(action, runParams, reply))
      return;

    if (action.refusedWhileCapturing && m_IsCaptureBusy && m_IsCaptureBusy())
    {
      reply.Fail(BridgeErrorCode::BUSY, "'" + name + "' has to wait for the running capture, "
        "which restores the view and the camera when it ends");
      return;
    }

    if (action.refusedWhileMinimized && m_IsMinimized && m_IsMinimized())
    {
      reply.Fail(BridgeErrorCode::BUSY, "'" + name + "': " + BRIDGE_MINIMIZED_MESSAGE);
      return;
    }

    action.handler(BridgeActionArgs(std::move(runParams)), reply);
  }

  bool BridgeActions::ValidateParams(const BridgeAction& action, const Json& params, const BridgeReply& reply) const
  {
    for (auto it = params.begin(); it != params.end(); ++it)
    {
      bool known = std::any_of(action.params.begin(), action.params.end(),
        [&it](const BridgeActionParam& param) { return param.name == it.key(); });
      if (!known)
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'" + action.name + "' has no param '" + it.key()
          + "'; params: " + ListParamNames(action));
        return false;
      }
    }

    Scene& scene = m_Registry->Get<Scene>();
    for (const BridgeActionParam& param : action.params)
    {
      auto it = params.find(param.name);
      if (it == params.end() || it->is_null())
      {
        if (param.required)
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'" + action.name + "' needs the param '" + param.name + "'");
          return false;
        }
        continue;
      }

      std::string error = CheckParamType(param, *it);
      if (!error.empty())
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
        return false;
      }

      if (param.type != BridgeActionParamType::Entity)
        continue;

      int64_t id = it->get<int64_t>();
      Entity entity = static_cast<Entity>(static_cast<EntityId>(id));
      if (!scene.GetRegistry().valid(entity))
      {
        reply.Fail(BridgeErrorCode::NOT_FOUND, "no entity with id " + std::to_string(id));
        return false;
      }

      // The editor camera and its kin exist for the editor itself; the Outliner never offers them
      if (scene.HasComponent<EditorOnlyTag>(entity))
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "entity " + std::to_string(id) + " ('" + scene.GetName(entity)
          + "') belongs to the editor itself; the editor camera is driven with camera.set");
        return false;
      }
    }

    return true;
  }
}
