#pragma once

#include "Editor/Bridge/BridgeMethods.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class ServiceRegistry;

  // scene.entities, scene.componentGet, scene.componentPatch, render.settingsGet and
  // render.settingsPatch. Components and settings travel as YAML in the shape the scene file
  // uses, so the scene serializers are the only reflection there is. Main thread only.
  class BridgeData
  {
  public:
    // isCaptureBusy refuses the patches while a shot renders the scene and restores the settings
    // after it.
    void Init(ServiceRegistry& registry, BridgeMethodRegistry& methods, std::function<bool()> isCaptureBusy);

  private:
    void HandleEntities(const Json& params, const BridgeReply& reply);
    void HandleComponentGet(const Json& params, const BridgeReply& reply);
    void HandleComponentPatch(const Json& params, const BridgeReply& reply);
    void HandleSettingsGet(const BridgeReply& reply);
    void HandleSettingsPatch(const Json& params, const BridgeReply& reply);
    // Fails the reply unless the params name a component the entity has.
    bool ResolveComponent(const Json& params, const BridgeReply& reply, Entity& outEntity,
      std::string& outComponent) const;
    // Fails the reply with busy while a capture shot runs.
    bool RefuseWhileCapturing(const char* method, const BridgeReply& reply) const;

    ServiceRegistry* m_Registry = nullptr;
    std::function<bool()> m_IsCaptureBusy;
  };
}
