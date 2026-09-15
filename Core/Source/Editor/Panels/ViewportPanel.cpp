#include "Editor/Panels/ViewportPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "Editor/EditorCameraLayer.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorPreferences.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Render/Render.h"
#include "Utils/DebugViews.h"
#include "Utils/Log.h"

namespace YAEngine
{
  namespace
  {
    enum ShowFlagIndex : size_t
    {
      SHOW_GIZMOS,
      SHOW_CAMERA_FRUSTUMS,
      SHOW_PROBE_VOLUMES,
      SHOW_IRRADIANCE_BOUNDS,
      SHOW_VOLUME_NODES,
      SHOW_REJECTED_NODES,
      SHOW_VOLUME_BRICKS,
      SHOW_COLLIDERS
    };

    struct ShowFlag
    {
      // editor.yaml key
      const char* key;
      // Show menu entry and bridge path segment
      const char* label;
      bool& (Render::*flag)();
      const char* tooltip;
    };

    constexpr ShowFlag SHOW_FLAGS[] = {
      { "gizmos", "Gizmos", &Render::GetGizmosEnabled,
        "Every editor overlay: light, probe and camera icons, the transform gizmo and\nthe entries below." },
      { "camera_frustums", "Camera Frustums", &Render::GetCameraFrustumsVisible,
        "Near end of the frustum of every scene camera." },
      { "reflection_probe_volumes", "Reflection Probe Volumes", &Render::GetProbeVolumesVisible,
        "Influence volumes of the baked reflection probes." },
      { "irradiance_volume_bounds", "Irradiance Volume Bounds", &Render::GetIrradianceVolumesVisible,
        "Bounds of every irradiance volume." },
      { "volume_nodes", "Volume Nodes", &Render::GetVolumeNodesVisible,
        "Baked SH nodes of the selected irradiance volume.\nAbove 20000 nodes only every k-th node is drawn." },
      { "rejected_nodes", "Rejected Nodes", &Render::GetVolumeInvalidNodesVisible,
        "Also draws the nodes the bake rejected (inside or too close to geometry), in magenta." },
      { "volume_bricks", "Volume Bricks", &Render::GetVolumeBricksVisible,
        "Wireframes of the previewed placement bricks of the selected irradiance volume,\n"
        "colored by spacing level. Preview Placement in Details fills them." },
      { "colliders", "Colliders", &Render::GetCollidersVisible,
        "Collider boxes, including the instanced colliders of scatter and model nodes." },
    };

    static_assert(std::size(SHOW_FLAGS) == ViewportPanel::SHOW_FLAG_COUNT);

    // Indices follow VolumeNodeColorMode
    constexpr const char* NODE_COLOR_KEYS[] = { "irradiance", "ringing" };
    constexpr const char* NODE_COLOR_LABELS[] = { "Irradiance", "Ringing" };
    constexpr const char* NODE_COLOR_TOOLTIPS[] = {
      "L0 divided by the brightest node of the volume, gamma encoded.\n"
        "Black = unlit, white = the brightest node, hue is the color of the bounce.",
      "Worst channel of |L1| / L0.\n"
        "Green to yellow = the L1 fit stays positive, yellow means it is close.\n"
        "Opaque red to white = above 1, those nodes reconstruct negative irradiance\n"
        "for some normals and the shader clamps them to black."
    };

    constexpr const char* VIEW_POPUP_ID = "View Menu";
    constexpr const char* SHOW_POPUP_ID = "Show Menu";
    constexpr const char* SPEED_POPUP_ID = "Camera Speed Menu";

    constexpr float TOOLBAR_PADDING = 4.0f;
    constexpr float PREVIEW_BANNER_MARGIN = 10.0f;
    constexpr float SPEED_SLIDER_WIDTH = 180.0f;
    // Popups over the image stay readable against a busy frame
    constexpr float OVERLAY_POPUP_MIN_ALPHA = 0.85f;
    constexpr double CAMERA_SPEED_SAVE_DELAY = 1.0;

    constexpr const char* GIZMOS_OFF_REASON = "Hidden while Gizmos is off.";
    constexpr const char* VOLUME_NODES_OFF_REASON = "Needs Volume Nodes.";

    void PushOverlayPopupColor()
    {
      glm::vec4 color = EditorStyle::GetTheme().overlay;
      color.a = std::max(color.a, OVERLAY_POPUP_MIN_ALPHA);
      ImGui::PushStyleColor(ImGuiCol_PopupBg, ToImGuiColor(color));
    }

    // Frame fill of a hand-drawn toolbar button for the item just submitted
    ImU32 ToolbarButtonColor(bool selected, bool popupOpen)
    {
      const EditorTheme& theme = EditorStyle::GetTheme();
      if (selected)
        return ImGui::GetColorU32(ToImGuiColor(ImGui::IsItemHovered() ? theme.accentHovered : theme.accent));
      if (popupOpen || ImGui::IsItemActive())
        return ImGui::GetColorU32(ImGuiCol_FrameBgActive);
      return ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    }

    void DrawCenteredText(const char* text)
    {
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      const ImVec2 size = ImGui::CalcTextSize(text);
      const ImVec2 position(std::floor(min.x + (max.x - min.x - size.x) * 0.5f),
        std::floor(min.y + (max.y - min.y - size.y) * 0.5f));
      ImGui::GetWindowDrawList()->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), text);
    }

    // The toolbar buttons are drawn by hand over an invisible button: a combo reports no label to
    // the agent bridge, so it could not be addressed by a path. Pressed on click like a combo, so a
    // click on the button of an open popup closes it instead of reopening it.
    bool ToolbarButton(const char* id, float width, bool selected, bool popupOpen)
    {
      const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, ImGui::GetFrameHeight()),
        ImGuiButtonFlags_PressedOnClick);
      ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
        ToolbarButtonColor(selected, popupOpen), ImGui::GetStyle().FrameRounding);
      return pressed;
    }

    float DropdownWidth(float textWidth)
    {
      const ImGuiStyle& style = ImGui::GetStyle();
      return style.FramePadding.x * 2.0f + textWidth + style.ItemInnerSpacing.x
        + ImGui::CalcTextSize(ICON_LC_CHEVRON_DOWN).x;
    }

    // Text on the left, chevron on the right of the button just submitted
    void DrawDropdownContent(const char* text)
    {
      const ImGuiStyle& style = ImGui::GetStyle();
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      const float textY = min.y + style.FramePadding.y;
      const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      drawList->AddText(ImVec2(min.x + style.FramePadding.x, textY), color, text);
      drawList->AddText(ImVec2(max.x - style.FramePadding.x - ImGui::CalcTextSize(ICON_LC_CHEVRON_DOWN).x, textY),
        color, ICON_LC_CHEVRON_DOWN);
    }

    // The regular image is listed and shown as its group, "Lit", rather than by its registry name "Off"
    const char* GetViewMenuLabel(const DebugViewInfo& view)
    {
      return view.group == DebugViewGroup::Lit ? GetDebugViewGroupName(view.group) : view.name;
    }
  }

  ViewportPanel::ViewportPanel(EditorPreferences& preferences, EditorCameraLayer* camera)
    : m_Preferences(preferences), m_Camera(camera)
  {
  }

  void ViewportPanel::OnSceneReady(EditorContext& context)
  {
    if (b_PreferencesApplied || context.render == nullptr)
      return;
    b_PreferencesApplied = true;

    Render& render = *context.render;
    for (size_t i = 0; i < SHOW_FLAG_COUNT; i++)
    {
      bool& flag = (render.*SHOW_FLAGS[i].flag)();
      m_ShowFlagDefaults[i] = flag;

      auto stored = m_Preferences.viewportShowFlags.find(SHOW_FLAGS[i].key);
      if (stored != m_Preferences.viewportShowFlags.end())
        flag = stored->second;
    }

    m_NodeColorDefault = uint8_t(render.GetVolumeNodeColorMode());
    if (!m_Preferences.viewportNodeColor.empty())
    {
      auto key = std::find(std::begin(NODE_COLOR_KEYS), std::end(NODE_COLOR_KEYS), std::string_view(m_Preferences.viewportNodeColor));
      if (key != std::end(NODE_COLOR_KEYS))
        render.GetVolumeNodeColorMode() = VolumeNodeColorMode(key - std::begin(NODE_COLOR_KEYS));
      else
        YA_LOG_WARN("Editor", "Preferences: unknown viewport node color '%s', using the default",
          m_Preferences.viewportNodeColor.c_str());
    }

    if (m_Camera != nullptr)
    {
      if (m_Preferences.cameraSpeed)
        m_Camera->SetSpeed(*m_Preferences.cameraSpeed);
      m_TrackedCameraSpeed = m_Camera->GetSpeed();
    }
  }

  void ViewportPanel::FlushPreferences()
  {
    if (m_CameraSpeedSaveTime < 0.0)
      return;

    m_CameraSpeedSaveTime = -1.0;
    m_Preferences.Save();
  }

  void ViewportPanel::OnRender(EditorContext& context)
  {
    // Popped right after Begin, which is where the window reads it: the toolbar popups need the
    // regular padding
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = BeginPanel(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (open)
    {
      context.viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
      context.mouseInViewportValid = false;
      auto size = ImGui::GetContentRegionAvail();
      uint32_t w = static_cast<uint32_t>(size.x);
      uint32_t h = static_cast<uint32_t>(size.y);

      if (w > 0 && h > 0 && context.render)
      {
        if (w != m_LastWidth || h != m_LastHeight)
        {
          m_LastWidth = w;
          m_LastHeight = h;
          context.viewportWidth = w;
          context.viewportHeight = h;
          context.render->RequestViewportResize(w, h);
        }

        auto vpMin = ImGui::GetCursorScreenPos();
        auto mouse = ImGui::GetMousePos();
        glm::vec2 rel((mouse.x - vpMin.x) / size.x, (mouse.y - vpMin.y) / size.y);
        context.mouseInViewportValid = context.viewportHovered
          && rel.x >= 0.0f && rel.x <= 1.0f && rel.y >= 0.0f && rel.y <= 1.0f;
        context.mouseInViewport = rel;

        ImGui::Image(context.render->GetSceneTextureID(), size);
        m_ImageBottom = vpMin.y + size.y;

        const float stripHeight = DrawToolbar(context, vpMin, size.x);
        DrawPreviewOverlay(context, ImVec2(vpMin.x, vpMin.y + stripHeight));
      }
    }
    else
    {
      context.viewportHovered = false;
      context.mouseInViewportValid = false;
    }
    ImGui::End();
  }

  float ViewportPanel::DrawToolbar(EditorContext& context, const ImVec2& origin, float width)
  {
    Render& render = *context.render;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float padding = TOOLBAR_PADDING * EditorStyle::GetContentScale();
    const ImVec2 stripMin = origin;
    const ImVec2 stripMax(origin.x + width, origin.y + ImGui::GetFrameHeight() + 2.0f * padding);

    // Popups need no test of their own: while one is open the window underneath reports no hover
    if (ImGui::IsMouseHoveringRect(stripMin, stripMax))
    {
      context.viewportHovered = false;
      context.mouseInViewportValid = false;
    }

    ImGui::GetWindowDrawList()->AddRectFilled(stripMin, stripMax,
      ImGui::GetColorU32(ToImGuiColor(EditorStyle::GetTheme().overlay)));

    // Ids live straight in the window scope so bridge paths read "Viewport/<button>"
    ImGui::SetCursorScreenPos(ImVec2(stripMin.x + padding, stripMin.y + padding));

    DrawGizmoModeButtons(render);
    ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
    DrawViewMenu(render);
    ImGui::SameLine();
    DrawShowMenu(render);
    DrawCameraSpeed(stripMax.x - padding);

    return stripMax.y - stripMin.y;
  }

  void ViewportPanel::DrawGizmoModeButtons(Render& render)
  {
    struct ModeButton
    {
      GizmoMode mode;
      const char* id;
      const char* icon;
      const char* tooltip;
    };

    static constexpr ModeButton BUTTONS[] = {
      { GizmoMode::Translate, "Move", ICON_LC_MOVE_3D, "Move (1)" },
      { GizmoMode::Rotate, "Rotate", ICON_LC_ROTATE_3D, "Rotate (2)" },
      { GizmoMode::Scale, "Scale", ICON_LC_SCALE_3D, "Scale (3)" },
    };

    GizmoMode& current = render.GetGizmoMode();
    const float size = ImGui::GetFrameHeight();
    const float rounding = ImGui::GetStyle().FrameRounding;

    for (size_t i = 0; i < std::size(BUTTONS); i++)
    {
      const ModeButton& button = BUTTONS[i];
      if (i > 0)
        ImGui::SameLine(0.0f, 0.0f);

      if (ImGui::InvisibleButton(button.id, ImVec2(size, size)))
        current = button.mode;

      const ImDrawFlags corners = i == 0 ? ImDrawFlags_RoundCornersLeft
        : i + 1 == std::size(BUTTONS) ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
      ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
        ToolbarButtonColor(current == button.mode, false), rounding, corners);
      DrawCenteredText(button.icon);

      ImGui::SetItemTooltip("%s", button.tooltip);
    }
  }

  void ViewportPanel::OpenToolbarPopupBelow(const char* popupId, bool pressed, bool wasOpen)
  {
    if (pressed && !wasOpen)
      ImGui::OpenPopup(popupId);

    const ImVec2 buttonMin = ImGui::GetItemRectMin();
    const ImVec2 buttonMax = ImGui::GetItemRectMax();
    const float top = buttonMax.y + ImGui::GetStyle().ItemSpacing.y * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(buttonMin.x, top));
    // Long lists scroll instead of running past the bottom of the viewport
    ImGui::SetNextWindowSizeConstraints(ImVec2(buttonMax.x - buttonMin.x, 0.0f),
      ImVec2(FLT_MAX, std::max(m_ImageBottom - top, ImGui::GetFrameHeight() * 4.0f)));
  }

  void ViewportPanel::DrawViewMenu(Render& render)
  {
    const int32_t currentId = render.GetDebugView();
    const DebugViewInfo* current = FindDebugView(currentId);
    const char* currentName = current == nullptr ? "Unknown" : GetViewMenuLabel(*current);

    char text[96];
    std::snprintf(text, sizeof(text), ICON_LC_EYE " %s", currentName);

    // Sized for the longest name, so the strip does not shift when the view changes; measured again only
    // when the font or its size changes
    if (ImGui::GetFont() != m_ViewMenuWidthFont || ImGui::GetFontSize() != m_ViewMenuWidthFontSize)
    {
      m_ViewMenuWidthFont = ImGui::GetFont();
      m_ViewMenuWidthFontSize = ImGui::GetFontSize();
      m_ViewMenuWidestName = 0.0f;
      for (const DebugViewInfo& view : GetDebugViews())
        m_ViewMenuWidestName = std::max(m_ViewMenuWidestName, ImGui::CalcTextSize(GetViewMenuLabel(view)).x);
    }

    const bool wasOpen = ImGui::IsPopupOpen(VIEW_POPUP_ID);
    const bool pressed = ToolbarButton("View", DropdownWidth(ImGui::CalcTextSize(ICON_LC_EYE " ").x + m_ViewMenuWidestName),
      false, wasOpen);
    DrawDropdownContent(text);
    if (!wasOpen)
      ImGui::SetItemTooltip("Debug view. Not saved with the scene.");
    OpenToolbarPopupBelow(VIEW_POPUP_ID, pressed, wasOpen);

    PushOverlayPopupColor();
    const bool open = ImGui::BeginPopup(VIEW_POPUP_ID);
    ImGui::PopStyleColor();
    if (!open)
      return;

    // Lit, the first group, is a single entry at the top with no heading of its own
    DebugViewGroup group = DebugViewGroup::Lit;
    for (const DebugViewInfo& view : GetDebugViews())
    {
      if (view.group != group)
      {
        group = view.group;
        EditorWidgets::PropertySubHeading(GetDebugViewGroupName(group));
      }

      const char* reason = EditorCommands::GetDebugViewUnavailableReason(render, view.id);

      ImGui::BeginDisabled(reason != nullptr);
      if (ImGui::Selectable(GetViewMenuLabel(view), view.id == currentId) && reason == nullptr)
        render.SetDebugView(view.id);
      ImGui::EndDisabled();

      if (reason != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", reason);
    }

    ImGui::EndPopup();
  }

  void ViewportPanel::DrawShowMenu(Render& render)
  {
    constexpr const char* TEXT = ICON_LC_LAYERS " Show";

    const bool wasOpen = ImGui::IsPopupOpen(SHOW_POPUP_ID);
    const bool pressed = ToolbarButton("Show", DropdownWidth(ImGui::CalcTextSize(TEXT).x), false, wasOpen);
    DrawDropdownContent(TEXT);
    if (!wasOpen)
      ImGui::SetItemTooltip("Editor overlays. Saved in the editor preferences.");
    OpenToolbarPopupBelow(SHOW_POPUP_ID, pressed, wasOpen);

    PushOverlayPopupColor();
    const bool open = ImGui::BeginPopup(SHOW_POPUP_ID);
    ImGui::PopStyleColor();
    if (!open)
      return;

    // Several overlays are usually toggled in a row, so the entries keep the menu open
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

    DrawShowFlag(render, SHOW_GIZMOS, nullptr);
    const char* gizmosOff = render.GetGizmosEnabled() ? nullptr : GIZMOS_OFF_REASON;

    EditorWidgets::PropertySubHeading("Cameras");
    DrawShowFlag(render, SHOW_CAMERA_FRUSTUMS, gizmosOff);

    EditorWidgets::PropertySubHeading("Probes & GI");
    DrawShowFlag(render, SHOW_PROBE_VOLUMES, gizmosOff);
    DrawShowFlag(render, SHOW_IRRADIANCE_BOUNDS, gizmosOff);
    DrawShowFlag(render, SHOW_VOLUME_NODES, gizmosOff);
    {
      const char* nodesOff = gizmosOff != nullptr ? gizmosOff
        : render.GetVolumeNodesVisible() ? nullptr : VOLUME_NODES_OFF_REASON;
      ImGui::Indent();
      DrawShowFlag(render, SHOW_REJECTED_NODES, nodesOff);
      DrawNodeColorMenu(render, nodesOff);
      ImGui::Unindent();
    }
    DrawShowFlag(render, SHOW_VOLUME_BRICKS, gizmosOff);

    EditorWidgets::PropertySubHeading("Physics");
    DrawShowFlag(render, SHOW_COLLIDERS, gizmosOff);

    ImGui::PopItemFlag();
    ImGui::EndPopup();
  }

  void ViewportPanel::DrawShowFlag(Render& render, size_t index, const char* disabledReason)
  {
    const ShowFlag& entry = SHOW_FLAGS[index];
    bool& value = (render.*entry.flag)();

    ImGui::BeginDisabled(disabledReason != nullptr);
    const bool toggled = ImGui::MenuItem(entry.label, nullptr, &value);
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
    {
      if (disabledReason != nullptr)
        ImGui::SetTooltip("%s\n\n%s", entry.tooltip, disabledReason);
      else
        ImGui::SetTooltip("%s", entry.tooltip);
    }

    if (toggled)
      StoreShowFlag(index, value);
  }

  void ViewportPanel::DrawNodeColorMenu(Render& render, const char* disabledReason)
  {
    PushOverlayPopupColor();
    const bool open = ImGui::BeginMenu("Node Color", disabledReason == nullptr);
    ImGui::PopStyleColor();

    if (!open)
    {
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
      {
        if (disabledReason != nullptr)
          ImGui::SetTooltip("What the volume node gizmos encode in their color.\n\n%s", disabledReason);
        else
          ImGui::SetTooltip("What the volume node gizmos encode in their color.");
      }
      return;
    }

    VolumeNodeColorMode& mode = render.GetVolumeNodeColorMode();
    for (size_t i = 0; i < std::size(NODE_COLOR_LABELS); i++)
    {
      if (ImGui::MenuItem(NODE_COLOR_LABELS[i], nullptr, size_t(mode) == i) && size_t(mode) != i)
      {
        mode = VolumeNodeColorMode(i);
        m_Preferences.viewportNodeColor = i == m_NodeColorDefault ? std::string() : std::string(NODE_COLOR_KEYS[i]);
        m_Preferences.Save();
      }
      ImGui::SetItemTooltip("%s", NODE_COLOR_TOOLTIPS[i]);
    }

    ImGui::EndMenu();
  }

  void ViewportPanel::DrawCameraSpeed(float rightEdge)
  {
    if (m_Camera == nullptr)
      return;

    char text[48];
    std::snprintf(text, sizeof(text), ICON_LC_GAUGE " %.2f m/s", m_Camera->GetSpeed());
    const float width = ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 2.0f;

    ImGui::SameLine();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(std::max(cursor.x, rightEdge - width), cursor.y));

    const bool wasOpen = ImGui::IsPopupOpen(SPEED_POPUP_ID);
    const bool pressed = ToolbarButton("Camera Speed", width, false, wasOpen);
    DrawCenteredText(text);
    if (!wasOpen)
      ImGui::SetItemTooltip("Editor camera speed. The mouse wheel over the viewport changes it too.");

    if (pressed && !wasOpen)
      ImGui::OpenPopup(SPEED_POPUP_ID);
    // Right-aligned under the button, so it stays inside the viewport
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMax().x,
      ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y * 0.5f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    PushOverlayPopupColor();
    const bool open = ImGui::BeginPopup(SPEED_POPUP_ID);
    ImGui::PopStyleColor();

    bool saveNow = false;
    if (open)
    {
      float speed = m_Camera->GetSpeed();
      ImGui::SetNextItemWidth(SPEED_SLIDER_WIDTH * EditorStyle::GetContentScale());
      if (ImGui::SliderFloat("Speed", &speed, EditorCameraLayer::MIN_SPEED, EditorCameraLayer::MAX_SPEED,
        "%.2f m/s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
      {
        m_Camera->SetSpeed(speed);
      }
      saveNow = ImGui::IsItemDeactivatedAfterEdit();
      ImGui::EndPopup();
    }

    TrackCameraSpeed(saveNow);
  }

  void ViewportPanel::TrackCameraSpeed(bool saveNow)
  {
    const float speed = m_Camera->GetSpeed();
    if (speed != m_TrackedCameraSpeed)
    {
      m_TrackedCameraSpeed = speed;
      m_Preferences.cameraSpeed = speed;
      m_CameraSpeedSaveTime = ImGui::GetTime() + CAMERA_SPEED_SAVE_DELAY;
    }

    if (m_CameraSpeedSaveTime >= 0.0 && (saveNow || ImGui::GetTime() >= m_CameraSpeedSaveTime))
      FlushPreferences();
  }

  void ViewportPanel::StoreShowFlag(size_t index, bool value)
  {
    const char* key = SHOW_FLAGS[index].key;
    if (value == m_ShowFlagDefaults[index])
      m_Preferences.viewportShowFlags.erase(key);
    else
      m_Preferences.viewportShowFlags[key] = value;
    m_Preferences.Save();
  }

  // The viewport shows the scene camera exactly as the game would, with no editor camera cues left
  // on screen, so the only sign that it is not the editor camera has to be drawn here.
  void ViewportPanel::DrawPreviewOverlay(EditorContext& context, const ImVec2& origin)
  {
    if (!context.IsPreviewingCamera() || context.scene == nullptr)
      return;
    if (!context.scene->GetRegistry().valid(context.previewCamera))
      return;

    const EditorTheme& theme = EditorStyle::GetTheme();
    const float margin = PREVIEW_BANNER_MARGIN * EditorStyle::GetContentScale();
    ImGui::SetCursorScreenPos(ImVec2(origin.x + margin, origin.y + margin));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImGuiColor(theme.overlay));
    ImGui::BeginChild("PreviewOverlay", ImVec2(0, 0),
      ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

    ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(theme.warning));
    ImGui::TextUnformatted(ICON_LC_VIDEO " Previewing:");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(context.scene->GetName(context.previewCamera).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Exit Preview"))
      context.StopCameraPreview();

    // The click that leaves the preview must not also fire a viewport pick
    if (ImGui::IsItemHovered())
      context.mouseInViewportValid = false;

    ImGui::EndChild();
    ImGui::PopStyleColor();
  }
}
