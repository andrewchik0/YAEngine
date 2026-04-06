#pragma once

#include "Pch.h"

namespace YAEngine
{
  class DebugMarker
  {
  public:

    static inline void Init(VkInstance instance)
    {
#ifndef NDEBUG
      s_CmdBeginLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
      s_CmdEndLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
      s_CmdInsertLabel = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
        vkGetInstanceProcAddr(instance, "vkCmdInsertDebugUtilsLabelEXT"));
      s_SetObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
#else
      (void)instance;
#endif
    }

    static inline void BeginLabel(VkCommandBuffer cmd, const char* name,
                                  float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f)
    {
#ifndef NDEBUG
      if (!s_CmdBeginLabel) return;
      VkDebugUtilsLabelEXT label{};
      label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
      label.pLabelName = name;
      label.color[0] = r;
      label.color[1] = g;
      label.color[2] = b;
      label.color[3] = a;
      s_CmdBeginLabel(cmd, &label);
#else
      (void)cmd; (void)name; (void)r; (void)g; (void)b; (void)a;
#endif
    }

    static inline void EndLabel(VkCommandBuffer cmd)
    {
#ifndef NDEBUG
      if (!s_CmdEndLabel) return;
      s_CmdEndLabel(cmd);
#else
      (void)cmd;
#endif
    }

    static inline void InsertLabel(VkCommandBuffer cmd, const char* name,
                                   float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f)
    {
#ifndef NDEBUG
      if (!s_CmdInsertLabel) return;
      VkDebugUtilsLabelEXT label{};
      label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
      label.pLabelName = name;
      label.color[0] = r;
      label.color[1] = g;
      label.color[2] = b;
      label.color[3] = a;
      s_CmdInsertLabel(cmd, &label);
#else
      (void)cmd; (void)name; (void)r; (void)g; (void)b; (void)a;
#endif
    }

    static inline void SetObjectName(VkDevice device, VkObjectType type, uint64_t handle, const char* name)
    {
#ifndef NDEBUG
      if (!s_SetObjectName) return;
      VkDebugUtilsObjectNameInfoEXT info{};
      info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
      info.objectType = type;
      info.objectHandle = handle;
      info.pObjectName = name;
      s_SetObjectName(device, &info);
#else
      (void)device; (void)type; (void)handle; (void)name;
#endif
    }

  private:

#ifndef NDEBUG
    static inline PFN_vkCmdBeginDebugUtilsLabelEXT s_CmdBeginLabel = nullptr;
    static inline PFN_vkCmdEndDebugUtilsLabelEXT s_CmdEndLabel = nullptr;
    static inline PFN_vkCmdInsertDebugUtilsLabelEXT s_CmdInsertLabel = nullptr;
    static inline PFN_vkSetDebugUtilsObjectNameEXT s_SetObjectName = nullptr;
#endif
  };

  class DebugScope
  {
  public:

#ifndef NDEBUG
    DebugScope(VkCommandBuffer cmd, const char* name,
               float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f)
      : m_Cmd(cmd)
    {
      DebugMarker::BeginLabel(cmd, name, r, g, b, a);
    }

    ~DebugScope()
    {
      DebugMarker::EndLabel(m_Cmd);
    }
#else
    DebugScope(VkCommandBuffer, const char*,
               float = 0.0f, float = 0.0f, float = 0.0f, float = 0.0f) {}
    ~DebugScope() = default;
#endif

    DebugScope(const DebugScope&) = delete;
    DebugScope& operator=(const DebugScope&) = delete;

  private:

#ifndef NDEBUG
    VkCommandBuffer m_Cmd;
#endif
  };
}

#ifndef NDEBUG
  #define YA_DEBUG_NAME(device, type, handle, name) \
      YAEngine::DebugMarker::SetObjectName(device, type, reinterpret_cast<uint64_t>(handle), name)
  #define YA_DEBUG_NAMEF(device, type, handle, fmt, ...) \
      do { char _dn[64]; snprintf(_dn, sizeof(_dn), fmt, __VA_ARGS__); \
           YAEngine::DebugMarker::SetObjectName(device, type, reinterpret_cast<uint64_t>(handle), _dn); } while(0)
#else
  #define YA_DEBUG_NAME(device, type, handle, name) ((void)0)
  #define YA_DEBUG_NAMEF(device, type, handle, fmt, ...) ((void)0)
#endif
