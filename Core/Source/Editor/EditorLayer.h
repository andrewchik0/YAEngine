#pragma once

#include "Layer.h"
#include "Editor/EditorContext.h"
#include "Editor/IEditorPanel.h"
#include "Editor/Utils/EditorTextureCache.h"
#include "Assets/IrradianceVolumeFile.h"
#include "Scene/ComponentRegistry.h"
#include "Utils/Ray.h"

namespace YAEngine
{
  enum class GizmoAxis : uint8_t;
  enum class GizmoMode : uint8_t;

  class EditorLayer : public Layer
  {
  public:

    void OnAttach() override;
    void OnSceneReady() override;
    void Update(double deltaTime) override;
    void RenderUI() override;
    void DebugDrawGizmos() override;
    void OnDetach() override;

  private:

    void BuildDefaultLayout(uint32_t dockspaceId);
    void NewScene();
    void SaveScene();
    void SaveSceneAs();
    void OpenScene();
    void LoadSceneDeferred(const std::string& path);
    void EnsureBasePath(const std::string& scenePath);
    void SyncEditorCameraState();
    void DebugDrawIrradianceVolumeNodes();

    EditorContext m_Context;
    std::string m_CurrentScenePath;
    std::string m_PendingScenePath;
    bool b_PendingNewScene = false;
    EditorTextureCache m_TextureCache;
    std::vector<std::unique_ptr<IEditorPanel>> m_Panels;
    bool b_LayoutBuilt = false;
    bool b_ResetLayout = false;
    uint32_t m_LastViewportWidth = 0;
    uint32_t m_LastViewportHeight = 0;

    // Parsed .yaiv of the selected volume, so the node gizmos never re-read the
    // file per frame
    std::string m_VolumeNodeCachePath;
    IrradianceVolumeFileData m_VolumeNodeCache;
    bool b_VolumeNodeCacheValid = false;
    // Brightest L0 channel over all valid nodes of the cached volume. Scanned
    // once when the cache is filled - the node draw runs every frame.
    float m_VolumeNodePeakL0 = 0.0f;

    // Gizmo drag state
    bool b_DragActive = false;
    GizmoAxis m_DragAxis {};
    GizmoMode m_DragMode {};
    LocalTransform m_DragStartLocalTransform;
    glm::vec3 m_DragStartWorldPos { 0.0f };
    glm::vec3 m_DragPlaneNormal { 0.0f };
    glm::vec3 m_DragStartHitPoint { 0.0f };
    glm::vec3 m_DragAxisDir { 0.0f };
    float m_DragGizmoScale = 1.0f;
  };
}
