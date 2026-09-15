#pragma once

#include "Editor/IEditorPanel.h"
#include "Editor/EditorCommands.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  class MaterialInspectorPanel;
  class SequencerPanel;
  struct RoadComponent;

  class DetailsPanel : public IEditorPanel
  {
  public:

    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "Details", .category = EditorPanelCategory::Scene };

    // Opens a closed panel, stores that in the preferences and brings the panel to the front
    using ShowPanelFunction = std::function<void(IEditorPanel& panel)>;

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;

    // The panels section buttons open: Material > Edit and Camera Track > Open in Sequencer
    void LinkPanels(MaterialInspectorPanel& materialInspector, SequencerPanel& sequencer, ShowPanelFunction showPanel);

  private:

    struct Section
    {
      bool (*present)(Scene& scene, Entity entity) = nullptr;
      void (DetailsPanel::*draw)(EditorContext& context, Entity entity) = nullptr;
    };

    // Component sections in display order
    static const Section SECTIONS[];

    void DrawHeader(EditorContext& context, Entity entity);
    void DrawTransformSection(EditorContext& context, Entity entity);
    void DrawModelSection(EditorContext& context, Entity entity);
    void DrawMaterialSection(EditorContext& context, Entity entity);
    void DrawLightSection(EditorContext& context, Entity entity);
    void DrawCameraSection(EditorContext& context, Entity entity);
    void DrawCameraTrackSection(EditorContext& context, Entity entity);
    void DrawReflectionProbeSection(EditorContext& context, Entity entity);
    void DrawIrradianceVolumeSection(EditorContext& context, Entity entity);
    void DrawBakeInclusionSection(EditorContext& context, Entity entity);
    void DrawTerrainSection(EditorContext& context, Entity entity);
    void DrawTerrainMaterialSection(EditorContext& context, Entity entity);
    void DrawRoadSection(EditorContext& context, Entity entity);
    // Returns whether the road has to be regenerated
    bool DrawRoadPoints(Entity entity, RoadComponent& road);
    void DrawScatterSection(EditorContext& context, Entity entity);
    void DrawColliderSection(EditorContext& context, Entity entity);
    void DrawAddComponent(EditorContext& context, Entity entity);

    MaterialInspectorPanel* m_MaterialInspector = nullptr;
    SequencerPanel* m_Sequencer = nullptr;
    ShowPanelFunction m_ShowPanel;

    // The name is typed into this buffer and applied on commit, only to the entity the edit started on
    Entity m_NameEditEntity { entt::null };
    std::string m_NameBuffer;
    bool b_NameEditing = false;

    // Rotation angles as last shown for m_EulerEntity. glm::eulerAngles folds angles past 90 deg back
    // into range, so deriving them from the quaternion every frame would make a typed 91 jump.
    Entity m_EulerEntity { entt::null };
    glm::quat m_EulerRotation { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 m_EulerDegrees { 0.0f };

    char m_MaterialSearch[128] = {};
    EditorCommands::MaterialUserCount m_MaterialUsers;
    EditorCommands::EntityNameLookup m_ClusterSourceLookup;

    // The Bake Inclusion tooltip for m_BakeTooltipEntity, rebuilt when the entity, its mode, its hidden state
    // or the scene structure changes and otherwise periodically (see DrawBakeInclusionSection)
    Entity m_BakeTooltipEntity { entt::null };
    int32_t m_BakeTooltipMode = -1;
    bool b_BakeTooltipHidden = false;
    uint64_t m_BakeTooltipGeneration = 0;
    double m_BakeTooltipTime = 0.0;
    std::string m_BakeTooltip;

    // World XZ to road canvas mapping for m_RoadViewEntity. Refitted to the points every frame except
    // while a point is held, so the road does not rescale under the cursor.
    Entity m_RoadViewEntity { entt::null };
    glm::vec2 m_RoadViewOrigin { 0.0f };
    float m_RoadViewRange = 1.0f;
    bool b_RoadViewLocked = false;
    // Canvas copies of the road points, kept to reuse their storage
    std::vector<glm::vec2> m_RoadCanvasPoints;
    std::vector<glm::vec2> m_RoadUneditedPoints;
  };
}
