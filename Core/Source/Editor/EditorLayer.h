#pragma once

#include "Layer.h"
#include "Editor/EditorContext.h"
#include "Editor/IEditorPanel.h"
#include "Editor/Utils/EditorTextureCache.h"
#include "Editor/Bridge/EditorBridge.h"
#include "Editor/EditorPreferences.h"
#include "Assets/IrradianceVolumeFile.h"
#include "Scene/ComponentRegistry.h"
#include "Utils/Ray.h"
#include "Render/Render.h"

namespace YAEngine
{
  enum class GizmoAxis : uint8_t;
  enum class GizmoMode : uint8_t;
  struct CameraTrackKey;
  class BridgeActions;
  class ViewportPanel;
  class OutlinerPanel;
  class DetailsPanel;
  class AgentPanel;

  class EditorLayer : public Layer
  {
  public:

    void OnAttach() override;
    void OnSceneReady() override;
    void Update(double deltaTime) override;
    void LateUpdate(double deltaTime) override;
    void RenderUI() override;
    void DebugDrawGizmos() override;
    void OnDetach() override;

  private:

    void BuildDefaultLayout(uint32_t dockspaceId);
    // Appends a panel with the visibility stored in the preferences
    template<typename TPanel, typename... TArgs>
    TPanel& AddPanel(TArgs&&... args);
    void DrawViewMenu();
    // View > Theme: picks a built-in palette and mode and saves the choice
    void DrawThemeMenu();
    // Shows or hides a panel and saves the choice in the preferences
    void SetPanelVisible(IEditorPanel& panel, bool visible);
    // Records the panel's current visibility in the preferences without saving them
    void StorePanelVisibility(const IEditorPanel& panel);
    void NewScene();
    void SaveScene();
    void SaveSceneAs();
    // Save Scene As past its dialog: the editor continues on the new path
    bool SaveSceneTo(const std::string& path);
    void OpenScene();
    void LoadSceneDeferred(const std::string& path);
    void EnsureBasePath(const std::string& scenePath);
    void SyncEditorCameraState();
    void DebugDrawIrradianceVolumeNodes();
    void ReleaseVolumeNodeCache();
    void DebugDrawIrradianceVolumeBricks();
    void DebugDrawSceneCameras();
    void DebugDrawCameraTrack();

    // EditorLayer.Actions.cpp: the editor operations agents run through actions.run
    void RegisterBridgeActions();
    void RegisterSelectionActions(BridgeActions& actions);
    void RegisterEntityActions(BridgeActions& actions);
    void RegisterViewActions(BridgeActions& actions);
    void RegisterSceneActions(BridgeActions& actions);
    void RegisterRenderActions(BridgeActions& actions);
    void RegisterPlaybackActions(BridgeActions& actions);

    // Picking, most specific first: overlay icons, then the entity id the renderer
    // rasterized into the clicked pixel, then the ray test for what has no geometry.
    Entity PickIconEntity(const Ray& ray, const glm::mat4& view, const glm::mat4& proj);
    Entity PickByRay(const Ray& ray);
    Entity FindSelectionRoot(Entity entity);
    void ApplyPickResult(const PickResult& result);

    // Sequencer key manipulation: while the track entity itself is selected and the
    // sequencer has a selected key, the viewport gizmo grabs that key instead
    CameraTrackKey* ActiveSequencerKey();
    void DragSequencerKey(const glm::vec3& delta, const glm::vec3& currentHit);
    bool PickTrackKey(const Ray& ray, Entity& outTrack, int& outKey);

    EditorContext m_Context;
    std::string m_CurrentScenePath;
    std::string m_PendingScenePath;
    bool b_PendingNewScene = false;
    EditorTextureCache m_TextureCache;
    // Declared before the panels so AgentPanel, which refers to both, is destroyed first
    EditorPreferences m_Preferences;
    EditorBridge m_Bridge;
    std::vector<std::unique_ptr<IEditorPanel>> m_Panels;
    // Typed access to the panels the layer drives directly; owned by m_Panels
    ViewportPanel* m_ViewportPanel = nullptr;
    OutlinerPanel* m_OutlinerPanel = nullptr;
    DetailsPanel* m_DetailsPanel = nullptr;
    AgentPanel* m_AgentPanel = nullptr;
    bool b_LayoutBuilt = false;
    bool b_ResetLayout = false;
    // Window content scale the style was last applied at
    float m_ContentScale = 1.0f;
    uint32_t m_LastViewportWidth = 0;
    uint32_t m_LastViewportHeight = 0;

    // Parsed .yaiv of the selected volume, so the node gizmos never re-read the
    // file per frame
    std::string m_VolumeNodeCachePath;
    // Render::GetIrradianceVolumeGeneration when the cache was filled
    uint32_t m_VolumeNodeCacheGeneration = 0;
    IrradianceVolumeFileData m_VolumeNodeCache;
    // Per node of the cached volume: xyz world position, w gizmo half size. Found from the bricks
    // once when the cache is filled.
    std::vector<glm::vec4> m_VolumeNodeGizmos;
    bool b_VolumeNodeCacheValid = false;
    // Brightest L0 channel over all valid nodes of the cached volume. Scanned
    // once when the cache is filled - the node draw runs every frame.
    float m_VolumeNodePeakL0 = 0.0f;

    // An id pick is answered a few frames after the click, so what the click meant has
    // to be remembered until then.
    bool b_PickRequestActive = false;
    bool b_PickRequestExact = false; // Ctrl was held: select the exact mesh, not its root
    Ray m_PickFallbackRay {};

    bool b_DragActive = false;
    bool b_DragTargetKey = false;
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
