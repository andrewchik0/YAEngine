#pragma once

#include "Layer.h"
#include "Editor/EditorContext.h"
#include "Editor/IEditorPanel.h"
#include "Editor/Utils/EditorTextureCache.h"
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
    void OnDetach() override;

  private:

    void BuildDefaultLayout(uint32_t dockspaceId);

    EditorContext m_Context;
    EditorTextureCache m_TextureCache;
    std::vector<std::unique_ptr<IEditorPanel>> m_Panels;
    bool b_LayoutBuilt = false;
    bool b_ResetLayout = false;
    uint32_t m_LastViewportWidth = 0;
    uint32_t m_LastViewportHeight = 0;

    // Gizmo drag state
    bool m_DragActive = false;
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
