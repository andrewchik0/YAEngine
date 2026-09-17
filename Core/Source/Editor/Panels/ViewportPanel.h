#pragma once

#include "Editor/IEditorPanel.h"

namespace YAEngine
{
  class EditorCameraLayer;
  class Render;
  struct EditorPreferences;

  // The scene image with a translucent toolbar over its top edge: gizmo mode, debug view, overlays
  // and camera speed. Show flags and camera speed are saved in the editor preferences; the debug
  // view is not.
  class ViewportPanel : public IEditorPanel
  {
  public:

    static constexpr EditorPanelDescriptor DESCRIPTOR { .name = "Viewport", .category = EditorPanelCategory::Scene };
    static constexpr size_t SHOW_FLAG_COUNT = 8;

    // camera may be null, which hides the speed control
    ViewportPanel(EditorPreferences& preferences, EditorCameraLayer* camera);

    const EditorPanelDescriptor& GetDescriptor() const override { return DESCRIPTOR; }
    void OnRender(EditorContext& context) override;
    // Applies the stored show flags and camera speed once Render exists
    void OnSceneReady(EditorContext& context) override;

    // Saves a camera speed change still waiting out its delay
    void FlushPreferences();

  private:
    // Returns the height of the strip
    float DrawToolbar(EditorContext& context, const ImVec2& origin, float width);
    void DrawGizmoModeButtons(Render& render);
    // Opens the popup on a press and places it under the button just submitted
    void OpenToolbarPopupBelow(const char* popupId, bool pressed, bool wasOpen);
    void DrawViewMenu(Render& render);
    void DrawShowMenu(Render& render);
    void DrawShowFlag(Render& render, size_t index, const char* disabledReason);
    void DrawNodeColorMenu(Render& render, const char* disabledReason);
    void DrawCameraSpeed(float rightEdge);
    static void DrawPreviewOverlay(EditorContext& context, const ImVec2& origin);
    // The 16:9 output frame of the piloted shot: the image outside it darkened, thirds inside
    static void DrawPilotFrame(EditorContext& context, const ImVec2& imageMin, const ImVec2& imageSize);
    static void DrawPilotOverlay(EditorContext& context, const ImVec2& origin);

    void StoreShowFlag(size_t index, bool value);
    // Wheel steps arrive every frame while scrolling, so a change is saved once it settles
    // unless the edit just finished
    void TrackCameraSpeed(bool saveNow);

    EditorPreferences& m_Preferences;
    EditorCameraLayer* m_Camera = nullptr;
    // Render's values before the preferences were applied; only differences are stored
    std::array<bool, SHOW_FLAG_COUNT> m_ShowFlagDefaults {};
    uint8_t m_NodeColorDefault = 0;
    bool b_PreferencesApplied = false;
    float m_TrackedCameraSpeed = 0.0f;
    // ImGui time the pending camera speed is saved at; negative when nothing is pending
    double m_CameraSpeedSaveTime = -1.0;
    // Screen y of the image's bottom edge; toolbar popups scroll rather than run past it
    float m_ImageBottom = 0.0f;
    // Widest View menu label, measured with m_ViewMenuWidthFont at m_ViewMenuWidthFontSize
    float m_ViewMenuWidestName = 0.0f;
    float m_ViewMenuWidthFontSize = 0.0f;
    ImFont* m_ViewMenuWidthFont = nullptr;
    uint32_t m_LastWidth = 0;
    uint32_t m_LastHeight = 0;
  };
}
