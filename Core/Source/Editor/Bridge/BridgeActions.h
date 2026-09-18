#pragma once

#include "Editor/Bridge/BridgeMethods.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class ServiceRegistry;

  enum class BridgeActionParamType : uint8_t
  {
    Entity,
    String,
    Number,
    Integer,
    Bool,
    Vec3,
    Path
  };

  struct BridgeActionParam
  {
    std::string name;
    BridgeActionParamType type = BridgeActionParamType::String;
    bool required = false;
    std::string description;
  };

  // The params of one run, already checked against the action's descriptors: required params are
  // present, every present param has its declared type, and entity params name live entities
  // that are not editor-only. Getters for an optional param need Has first.
  class BridgeActionArgs
  {
  public:
    explicit BridgeActionArgs(Json params) : m_Params(std::move(params)) {}

    bool Has(const char* name) const;
    Entity GetEntity(const char* name) const;
    std::string GetString(const char* name) const;
    double GetNumber(const char* name) const;
    int64_t GetInteger(const char* name) const;
    bool GetBool(const char* name) const;
    glm::vec3 GetVec3(const char* name) const;

  private:
    Json m_Params;
  };

  // Runs on the main thread and completes the reply exactly once, now or through
  // BridgeActions::Defer.
  using BridgeActionHandler = std::function<void(const BridgeActionArgs& args, const BridgeReply& reply)>;

  struct BridgeAction
  {
    std::string name;
    std::string description;
    std::vector<BridgeActionParam> params;
    // A capture shot restores the view settings and the camera pose when it ends and renders
    // from the scene as it stands, so an action that changes those, replaces the scene or
    // renders on its own is answered with busy while a shot runs.
    bool refusedWhileCapturing = false;
    // For an action that completes in a later frame: answered with busy while the editor window is
    // minimized, since no frame advances then.
    bool refusedWhileMinimized = false;
    BridgeActionHandler handler;
  };

  // actions.list and actions.run over named editor operations. Main thread only.
  class BridgeActions
  {
  public:
    void Init(ServiceRegistry& registry, BridgeMethodRegistry& methods, std::function<bool()> isCaptureBusy,
      std::function<bool()> isMinimized);
    // Registering a name again replaces the action.
    void Register(BridgeAction action);
    // Whether the named action answers in a later frame (refusedWhileMinimized); false for an unknown name.
    bool CompletesLater(const std::string& name) const;
    // For a run that finishes in a later frame: poll is called once per frame until it returns
    // true, and has completed the reply by then.
    void Defer(BridgeReply reply, std::function<bool(const BridgeReply& reply)> poll);
    void LateUpdate();
    // Fails every deferred run that has not finished.
    void Cancel(std::string_view reason);

  private:
    struct PendingRun
    {
      BridgeReply reply;
      std::function<bool(const BridgeReply& reply)> poll;
    };

    void HandleList(const BridgeReply& reply) const;
    void HandleRun(const Json& params, const BridgeReply& reply);
    // Fails the reply unless the params match the action's descriptors.
    bool ValidateParams(const BridgeAction& action, const Json& params, const BridgeReply& reply) const;

    ServiceRegistry* m_Registry = nullptr;
    std::function<bool()> m_IsCaptureBusy;
    std::function<bool()> m_IsMinimized;
    // Ordered, so actions.list comes out sorted by name
    std::map<std::string, BridgeAction> m_Actions;
    std::vector<PendingRun> m_Pending;
  };
}
