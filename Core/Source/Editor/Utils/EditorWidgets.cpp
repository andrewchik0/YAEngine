#include "Editor/Utils/EditorWidgets.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include "Assets/AssetManager.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorPreferences.h"
#include "Editor/Utils/EditorFonts.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorTextureCache.h"
#include "Editor/Utils/FileDialog.h"
#include "Utils/StringSearch.h"

namespace YAEngine::EditorWidgets
{
  namespace
  {
    // Unscaled pixels, multiplied by the window content scale
    constexpr float MIN_LABEL_WIDTH = 110.0f;
    constexpr float LABEL_INSET = 8.0f;
    constexpr float DEPENDENCY_INDENT = 14.0f;
    constexpr float CELL_PADDING_X = 4.0f;
    constexpr float CELL_PADDING_Y = 2.0f;
    constexpr float HEADER_EXTRA_HEIGHT = 4.0f;
    constexpr float HEADER_GAP = 6.0f;
    constexpr float GROUP_BOTTOM_GAP = 4.0f;
    constexpr float COMPONENT_GAP = 4.0f;
    constexpr float SUBHEADING_LINE = 1.0f;

    // A narrow grid still leaves its values this much of the width
    constexpr float MAX_LABEL_FRACTION = 0.6f;
    constexpr float HEADER_SURFACE_MIX = 0.5f;
    constexpr float ROW_HOVER_MIX = 0.3f;
    constexpr float TOOLTIP_WIDTH_EMS = 22.0f;
    constexpr size_t ENTITY_PICKER_VISIBLE_ROWS = 12;

    // Keeps grid table ids apart from the item ids hashed in the same scope
    constexpr ImGuiID GRID_ID_SEED = 0x47524944u;

    constexpr const char* ELLIPSIS = "\xE2\x80\xA6";

    constexpr nfdu8filteritem_t IMAGE_FILTERS[] = { { "Image Files", "png,jpg,jpeg,tga,bmp,hdr" } };

    struct ScopeFrame
    {
      ImGuiID id = 0;
      // Group title, part of the preferences key; null for a BeginPropertyScope block
      const char* label = nullptr;
      // Grids closed so far in this scope; each new one needs its own table id
      uint32_t segment = 0;
      bool gridOpen = false;
    };

    struct RowState
    {
      const char* label = nullptr;
      const char* labelEnd = nullptr;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
      ImGuiID id = 0;
      bool composite = false;
      bool labelTruncated = false;
      bool labelHovered = false;
      bool labelTooltipHovered = false;
    };

    EditorPreferences* s_Preferences = nullptr;
    std::vector<ScopeFrame> s_Scopes;
    // One entry per PushDependency: null while satisfied, the reason while not
    std::vector<const char*> s_Dependencies;
    RowState s_Row;

    // A text field edits a copy that reaches its target only on commit
    ImGuiID s_TextEditId = 0;
    const std::string* s_TextEditTarget = nullptr;
    std::string s_TextEditBuffer;

    char s_EntitySearch[128] = {};

    struct EntityCandidate
    {
      Entity entity = entt::null;
      // Another entity carries the same name
      bool sharedName = false;
    };

    // The open entity picker's list. One popup is open at a time, so one list serves every picker. It is
    // rebuilt when a picker opens and when its search, the scene structure or the named entity count
    // changes, not every frame.
    ImGuiID s_EntityListId = 0;
    std::string s_EntityListSearch;
    const Scene* s_EntityListScene = nullptr;
    uint64_t s_EntityListGeneration = 0;
    size_t s_EntityListNamedCount = 0;
    std::vector<EntityCandidate> s_EntityList;

    float Scale()
    {
      return EditorStyle::GetContentScale();
    }

    // Through GetColorU32, so the alpha of a disabled block applies
    ImU32 TokenColor(const glm::vec4& color)
    {
      return ImGui::GetColorU32(ToImGuiColor(color));
    }

    ImU32 SurfaceColor(float frameMix)
    {
      const EditorTheme& theme = EditorStyle::GetTheme();
      return TokenColor(glm::mix(theme.panel, theme.frame, frameMix));
    }

    const glm::vec4& StatusColor(StatusKind kind)
    {
      const EditorTheme& theme = EditorStyle::GetTheme();
      switch (kind)
      {
        case StatusKind::Success: return theme.success;
        case StatusKind::Warning: return theme.warning;
        case StatusKind::Error: return theme.error;
        case StatusKind::Info: return theme.info;
        default: return theme.textSecondary;
      }
    }

    const char* StatusIcon(StatusKind kind)
    {
      switch (kind)
      {
        case StatusKind::Success: return ICON_LC_CHECK;
        case StatusKind::Warning: return ICON_LC_TRIANGLE_ALERT;
        case StatusKind::Error: return ICON_LC_CIRCLE_X;
        case StatusKind::Info: return ICON_LC_INFO;
        default: return nullptr;
      }
    }

    void FormatWithUnit(char* buffer, size_t size, const char* format, const char* unit)
    {
      if (unit == nullptr || unit[0] == '\0')
        std::snprintf(buffer, size, "%s", format);
      else
        std::snprintf(buffer, size, "%s %s", format, unit);
    }

    // A value that would print as a negative zero ("-0.0 deg") is shown as a plain zero. Only the shown
    // copy changes; the caller writes it back only when the widget edits it.
    float WithoutNegativeZero(float value, const char* format)
    {
      if (!(value <= 0.0f))
        return value;

      char trimmed[32];
      char text[64];
      ImFormatString(text, sizeof(text), ImParseFormatTrimDecorations(format, trimmed, sizeof(trimmed)), value);
      return std::strtof(text, nullptr) == 0.0f ? 0.0f : value;
    }

    const char* ActiveDisabledReason(const char* own)
    {
      if (own != nullptr)
        return own;
      for (const char* reason : s_Dependencies)
      {
        if (reason != nullptr)
          return reason;
      }
      return nullptr;
    }

    // Straight into the draw list, which keeps the text out of ImGui's text log: the bridge reads a
    // widget's value from the "{ value } label" the widget logs itself, and a second copy of the
    // label logged just before it would be matched first.
    bool DrawTextEllipsized(ImDrawList* drawList, ImVec2 pos, float maxWidth, ImU32 color, const char* text, const char* textEnd)
    {
      ImFont* font = ImGui::GetFont();
      const float fontSize = ImGui::GetFontSize();
      if (ImGui::CalcTextSize(text, textEnd).x <= maxWidth)
      {
        drawList->AddText(font, fontSize, pos, color, text, textEnd);
        return false;
      }

      const float ellipsisWidth = ImGui::CalcTextSize(ELLIPSIS).x;
      const char* cut = textEnd;
      const float prefixWidth = font->CalcTextSizeA(fontSize, std::max(maxWidth - ellipsisWidth, 0.0f), 0.0f, text, textEnd, &cut).x;
      drawList->AddText(font, fontSize, pos, color, text, cut);
      drawList->AddText(font, fontSize, ImVec2(pos.x + prefixWidth, pos.y), color, ELLIPSIS);
      return true;
    }

    void DrawTooltip(const char* title, const char* titleEnd, const char* text, const char* disabledReason)
    {
      if (title == nullptr && text == nullptr && disabledReason == nullptr)
        return;
      if (!ImGui::BeginTooltip())
        return;

      ImGui::PushTextWrapPos(ImGui::GetFontSize() * TOOLTIP_WIDTH_EMS);
      if (title != nullptr)
      {
        EditorFonts::Push(EditorFontRole::Medium);
        ImGui::TextUnformatted(title, titleEnd);
        EditorFonts::Pop();
      }
      if (text != nullptr)
        ImGui::TextWrapped("%s", text);
      if (disabledReason != nullptr)
      {
        ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(EditorStyle::GetTheme().warning));
        ImGui::TextWrapped(ICON_LC_CIRCLE_ALERT " %s", disabledReason);
        ImGui::PopStyleColor();
      }
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }

    std::string BuildGroupKey(const char* label, const char* labelEnd)
    {
      std::string key = GImGui->CurrentWindow->RootWindow->Name;
      for (const ScopeFrame& scope : s_Scopes)
      {
        if (scope.label == nullptr)
          continue;
        key += '/';
        key.append(scope.label, ImGui::FindRenderedTextEnd(scope.label));
      }
      key += '/';
      key.append(label, labelEnd);
      return key;
    }

    bool LoadGroupOpen(const std::string& key, bool defaultOpen)
    {
      if (s_Preferences == nullptr)
        return defaultOpen;

      auto it = s_Preferences->groupOpenStates.find(key);
      return it != s_Preferences->groupOpenStates.end() ? it->second : defaultOpen;
    }

    void StoreGroupOpen(const std::string& key, bool open, bool defaultOpen)
    {
      if (s_Preferences == nullptr)
        return;

      // Only departures from the default are kept, so a group nobody touched follows its default
      if (open == defaultOpen)
        s_Preferences->groupOpenStates.erase(key);
      else
        s_Preferences->groupOpenStates[key] = open;
      s_Preferences->Save();
    }

    void CloseGrid(ScopeFrame& scope)
    {
      if (!scope.gridOpen)
        return;

      ImGui::PopID();
      ImGui::EndTable();
      ImGui::PopStyleVar();
      scope.gridOpen = false;
      scope.segment++;
    }

    void CloseCurrentGrid()
    {
      if (!s_Scopes.empty())
        CloseGrid(s_Scopes.back());
    }

    bool OpenGrid()
    {
      IM_ASSERT(!s_Scopes.empty() && "Property rows need BeginPropertyGroup or BeginPropertyScope");
      if (s_Scopes.empty())
        return false;

      ScopeFrame& scope = s_Scopes.back();
      if (scope.gridOpen)
        return true;

      const float scale = Scale();
      const float width = ImGui::GetContentRegionAvail().x;
      const float maxLabelWidth = width * MAX_LABEL_FRACTION;
      const float labelWidth = std::max(std::floor(std::clamp(width * EditorStyle::GetTheme().labelColumnFraction,
        std::min(MIN_LABEL_WIDTH * scale, maxLabelWidth), maxLabelWidth)), 1.0f);

      // Tables read the vertical cell padding at every row, so it stays pushed while the grid is open
      ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(CELL_PADDING_X * scale, CELL_PADDING_Y * scale));
      const ImGuiID tableId = ImHashData(&scope.segment, sizeof(scope.segment), scope.id ^ GRID_ID_SEED);
      if (!ImGui::BeginTableEx("##PropertyGrid", tableId, 2, ImGuiTableFlags_NoSavedSettings))
      {
        ImGui::PopStyleVar();
        return false;
      }

      ImGui::TableSetupColumn("##Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
      ImGui::TableSetupColumn("##Value", ImGuiTableColumnFlags_WidthStretch);
      // The table pushed its own id; rows hash their labels in the scope they were declared in
      ImGui::PushOverrideID(scope.id);
      scope.gridOpen = true;
      return true;
    }

    bool BeginRow(const char* label, const char* tooltip, const char* disabledReason, bool composite)
    {
      IM_ASSERT(s_Row.label == nullptr && "Property rows cannot be nested");
      if (!OpenGrid())
        return false;

      ImGuiContext& g = *GImGui;
      ImGuiWindow* window = g.CurrentWindow;
      const float scale = Scale();

      s_Row = RowState {
        .label = label,
        .labelEnd = ImGui::FindRenderedTextEnd(label),
        .tooltip = tooltip,
        .disabledReason = ActiveDisabledReason(disabledReason),
        .id = window->GetID(label),
        .composite = composite,
      };

      const float frameHeight = ImGui::GetFrameHeight();
      ImGui::TableNextRow(ImGuiTableRowFlags_None, frameHeight + g.Style.CellPadding.y * 2.0f);
      ImGui::TableSetColumnIndex(0);

      if (s_Row.disabledReason != nullptr)
        ImGui::BeginDisabled();

      const ImVec2 cellPos = window->DC.CursorPos;
      const float cellWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
      const float inset = (LABEL_INSET + DEPENDENCY_INDENT * float(s_Dependencies.size())) * scale;
      const ImVec2 textPos(cellPos.x + inset, cellPos.y + g.Style.FramePadding.y);
      s_Row.labelTruncated = DrawTextEllipsized(window->DrawList, textPos, std::max(cellWidth - inset, 0.0f),
        TokenColor(EditorStyle::GetTheme().textSecondary), label, s_Row.labelEnd);

      const ImRect labelRect(cellPos, ImVec2(cellPos.x + cellWidth, cellPos.y + frameHeight));
      ImGui::ItemSize(labelRect, g.Style.FramePadding.y);
      if (composite)
      {
        // The parts of a composite row hang off this item, which is what gives them exact bridge paths
        ImGui::ItemAdd(labelRect, s_Row.id, nullptr, ImGuiItemFlags_NoNav);
        IMGUI_TEST_ENGINE_ITEM_INFO(s_Row.id, label, g.LastItemData.StatusFlags);
        if (g.LogEnabled)
          ImGui::LogRenderedText(&textPos, label, s_Row.labelEnd);
      }
      else
      {
        ImGui::ItemAdd(labelRect, 0);
      }

      s_Row.labelHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
      s_Row.labelTooltipHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled);

      ImGui::TableSetColumnIndex(1);
      if (composite)
        ImGui::PushOverrideID(s_Row.id);
      ImGui::BeginGroup();
      return true;
    }

    void HighlightHoveredRow()
    {
      ImGuiTable* table = ImGui::GetCurrentTable();
      if (table == nullptr || !ImGui::IsWindowHovered())
        return;

      ImGuiWindow* window = GImGui->CurrentWindow;
      const float top = table->RowPosY1;
      const float bottom = std::max(table->RowPosY2, window->DC.CursorMaxPos.y + table->RowCellPaddingY);
      if (ImGui::IsMouseHoveringRect(ImVec2(table->WorkRect.Min.x, top), ImVec2(table->WorkRect.Max.x, bottom), false))
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, SurfaceColor(ROW_HOVER_MIX));
    }

    // True when Reset to Default was chosen from the row's context menu
    bool EndRow(bool hasDefault)
    {
      ImGui::EndGroup();
      const bool valueHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
      const bool valueTooltipHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)
        && !ImGui::IsItemActive();

      if (s_Row.composite)
        ImGui::PopID();

      const bool disabled = s_Row.disabledReason != nullptr;
      if (disabled)
        ImGui::EndDisabled();

      bool reset = false;
      if (hasDefault && !disabled)
      {
        const ImGuiID popupId = ImHashStr("##ResetMenu", 0, s_Row.id);
        if ((s_Row.labelHovered || valueHovered) && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
          ImGui::OpenPopupEx(popupId);
        if (ImGui::BeginPopupEx(popupId, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
        {
          reset = ImGui::MenuItem("Reset to Default");
          ImGui::EndPopup();
        }
      }

      if (s_Row.labelTooltipHovered || valueTooltipHovered)
        DrawTooltip(s_Row.labelTruncated ? s_Row.label : nullptr, s_Row.labelEnd, s_Row.tooltip, s_Row.disabledReason);

      HighlightHoveredRow();
      s_Row = RowState {};
      return reset;
    }

    // One widget fills the value cell. It keeps its own label, which is its id and the text the
    // bridge reads, but draws that label past the cell edge, where this clip rect hides it.
    void BeginSingleValue(float clipWidth)
    {
      ImGuiWindow* window = GImGui->CurrentWindow;
      const ImVec2 pos = window->DC.CursorPos;
      ImGui::PushClipRect(ImVec2(pos.x, window->ClipRect.Min.y), ImVec2(pos.x + clipWidth, window->ClipRect.Max.y), true);
    }

    void EndSingleValue()
    {
      ImGui::PopClipRect();
    }

    // Right after the widget. A value set while nothing is held active came from a click, not
    // from a drag in progress.
    void FinishEdit(PropertyEdit& edit)
    {
      edit.committed |= ImGui::IsItemDeactivatedAfterEdit();
      if (edit.changed && !ImGui::IsAnyItemActive())
        edit.committed = true;
    }

    template<typename T>
    void ApplyReset(bool reset, T& value, const std::optional<T>& defaultValue, PropertyEdit& edit)
    {
      if (!reset || !defaultValue.has_value() || value == *defaultValue)
        return;

      value = *defaultValue;
      edit.changed = true;
      edit.committed = true;
    }

    // Button whose id and bridge label come from the label, whatever it shows. width: 0 fits the
    // content, negative fills the available width.
    bool IconTextButton(const char* label, const char* icon, bool showLabel, float width)
    {
      ImGuiContext& g = *GImGui;
      ImGuiWindow* window = g.CurrentWindow;
      if (window->SkipItems)
        return false;

      const ImGuiStyle& style = g.Style;
      const ImGuiID id = window->GetID(label);
      const char* labelEnd = ImGui::FindRenderedTextEnd(label);
      const float iconWidth = icon != nullptr ? ImGui::CalcTextSize(icon).x : 0.0f;
      const float textWidth = showLabel ? ImGui::CalcTextSize(label, labelEnd).x : 0.0f;
      const float gap = icon != nullptr && showLabel ? style.ItemInnerSpacing.x : 0.0f;
      const float contentWidth = iconWidth + gap + textWidth;
      const float frameHeight = ImGui::GetFrameHeight();

      float buttonWidth = showLabel ? contentWidth + style.FramePadding.x * 2.0f : frameHeight;
      if (width < 0.0f)
        buttonWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
      else if (width > 0.0f)
        buttonWidth = width;

      const ImVec2 pos = window->DC.CursorPos;
      const ImRect bb(pos, ImVec2(pos.x + buttonWidth, pos.y + frameHeight));
      ImGui::ItemSize(bb, style.FramePadding.y);
      if (!ImGui::ItemAdd(bb, id))
        return false;

      bool hovered = false;
      bool held = false;
      const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
      const ImGuiCol fill = held && hovered ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button;
      ImGui::RenderNavCursor(bb, id);
      ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(fill), true, style.FrameRounding);

      // The disabled alpha alone leaves a disabled button looking live on the dark theme
      const bool disabled = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
      const ImU32 textColor = ImGui::GetColorU32(disabled ? ImGuiCol_TextDisabled : ImGuiCol_Text);
      ImVec2 contentPos(bb.Min.x + (buttonWidth - contentWidth) * 0.5f, bb.Min.y + style.FramePadding.y);
      if (showLabel)
        contentPos.x = std::max(contentPos.x, bb.Min.x + style.FramePadding.x);
      if (icon != nullptr)
      {
        window->DrawList->AddText(contentPos, textColor, icon);
        contentPos.x += iconWidth + gap;
      }
      if (showLabel)
        window->DrawList->AddText(contentPos, textColor, label, labelEnd);

      if (g.LogEnabled)
      {
        ImGui::LogSetNextTextDecoration("[", "]");
        ImGui::LogRenderedText(&bb.Min, label, labelEnd);
      }

      IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
      return pressed;
    }

    // A slider drawn as a fill behind its value text; the regular grab would cover the number. The
    // fill is drawn before the widget from the value it had then, one frame behind a drag.
    bool FillSliderFloat(const char* label, float& value, float min, float max, const char* format, ImGuiSliderFlags flags, float width)
    {
      ImGuiContext& g = *GImGui;
      ImGuiWindow* window = g.CurrentWindow;
      const EditorTheme& theme = EditorStyle::GetTheme();
      const ImGuiID id = window->GetID(label);
      const ImVec2 pos = window->DC.CursorPos;
      const ImRect frame(pos, ImVec2(pos.x + width, pos.y + ImGui::GetFrameHeight()));

      const bool active = g.ActiveId == id;
      const bool hovered = !active && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(frame.Min, frame.Max);
      const float rounding = g.Style.FrameRounding;
      window->DrawList->AddRectFilled(frame.Min, frame.Max,
        TokenColor(active ? theme.frameActive : hovered ? theme.frameHovered : theme.frame), rounding);

      const float fraction = max > min ? std::clamp((value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
      if (fraction > 0.0f)
      {
        // A disabled slider keeps its proportion but not the accent, so it does not read as live
        const bool disabled = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
        const ImDrawFlags corners = fraction < 1.0f ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersAll;
        window->DrawList->AddRectFilled(frame.Min, ImVec2(frame.Min.x + frame.GetWidth() * fraction, frame.Max.y),
          TokenColor(disabled ? theme.frameActive : theme.accentMuted), rounding, corners);
      }

      constexpr ImGuiCol HIDDEN_COLORS[] = {
        ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive
      };
      for (ImGuiCol color : HIDDEN_COLORS)
        ImGui::PushStyleColor(color, IM_COL32(0, 0, 0, 0));
      ImGui::SetNextItemWidth(width);
      const bool changed = ImGui::SliderFloat(label, &value, min, max, format, flags);
      ImGui::PopStyleColor(int32_t(std::size(HIDDEN_COLORS)));
      return changed;
    }

    // Inside the right end of a group header. Added without ItemSize so the header keeps its line; the
    // header item allows the overlap.
    bool DrawGroupAction(ImGuiID groupId, const ImRect& header, const GroupAction& action)
    {
      ImGuiContext& g = *GImGui;
      ImGuiWindow* window = g.CurrentWindow;
      const EditorTheme& theme = EditorStyle::GetTheme();
      const float size = ImGui::GetFrameHeight();
      const float inset = std::max((header.GetHeight() - size) * 0.5f, 0.0f);
      const ImRect bb(ImVec2(header.Max.x - inset - size, header.Min.y + inset),
        ImVec2(header.Max.x - inset, header.Min.y + inset + size));
      const bool disabled = action.disabledReason != nullptr;
      const char* labelEnd = ImGui::FindRenderedTextEnd(action.label);

      // In the group's scope whether or not the group is open, so the bridge path never changes
      ImGui::PushOverrideID(groupId);
      const ImGuiID id = window->GetID(action.label);
      if (disabled)
        ImGui::BeginDisabled();

      bool pressed = false;
      if (ImGui::ItemAdd(bb, id))
      {
        bool hovered = false;
        bool held = false;
        pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (hovered)
          window->DrawList->AddRectFilled(bb.Min, bb.Max, TokenColor(held ? theme.frameActive : theme.frameHovered),
            theme.radiusSmall * Scale());

        const char* icon = action.icon != nullptr ? action.icon : ICON_LC_X;
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        window->DrawList->AddText(ImVec2(bb.Min.x + (size - iconSize.x) * 0.5f, bb.Min.y + (size - iconSize.y) * 0.5f),
          TokenColor(hovered ? theme.textPrimary : theme.textSecondary), icon);

        if (g.LogEnabled)
        {
          ImGui::LogSetNextTextDecoration("[", "]");
          ImGui::LogRenderedText(&bb.Min, action.label, labelEnd);
        }
        IMGUI_TEST_ENGINE_ITEM_INFO(id, action.label, g.LastItemData.StatusFlags);
      }
      const bool tooltipHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled);

      if (disabled)
        ImGui::EndDisabled();
      ImGui::PopID();

      if (tooltipHovered)
        DrawTooltip(action.label, labelEnd, action.tooltip, action.disabledReason);
      return pressed && !disabled;
    }

    PropertyEdit PropertyVector(const char* label, float* values, int32_t count, float speed, float min, float max,
      const char* format, const char* unit, const std::array<const char*, 3>& parts, const float* defaults,
      const char* tooltip, const char* disabledReason)
    {
      if (!BeginRow(label, tooltip, disabledReason, true))
        return {};

      static constexpr glm::vec4 EditorTheme::* AXIS_COLORS[] = { &EditorTheme::axisX, &EditorTheme::axisY, &EditorTheme::axisZ };

      ImGuiContext& g = *GImGui;
      const EditorTheme& theme = EditorStyle::GetTheme();

      char displayFormat[48];
      FormatWithUnit(displayFormat, sizeof(displayFormat), format, unit);
      const ImGuiSliderFlags flags = min < max ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;

      float partLabelWidth = 0.0f;
      for (int32_t i = 0; i < count; i++)
        partLabelWidth = std::max(partLabelWidth, ImGui::CalcTextSize(parts[size_t(i)]).x);

      const float gap = COMPONENT_GAP * Scale();
      const float labelGap = g.Style.ItemInnerSpacing.x;
      // A disabled row drops the axis colors, as a disabled slider drops its accent
      const bool disabled = (g.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
      const float slot = (ImGui::GetContentRegionAvail().x - gap * float(count - 1)) / float(count);
      const float fieldWidth = std::max(slot - partLabelWidth - labelGap, 1.0f);

      // Each part starts at its own slot: a drag's clipped label still counts in its item width, so
      // SameLine would push every later part right by that label
      const ImVec2 rowStart = ImGui::GetCursorScreenPos();
      PropertyEdit edit;
      for (int32_t i = 0; i < count; i++)
      {
        if (i > 0)
        {
          ImGui::SameLine();
          ImGui::SetCursorScreenPos(ImVec2(rowStart.x + float(i) * (slot + gap), rowStart.y));
        }

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        g.CurrentWindow->DrawList->AddText(ImVec2(pos.x, pos.y + g.Style.FramePadding.y),
          TokenColor(disabled ? theme.textSecondary : theme.*AXIS_COLORS[i]), parts[size_t(i)]);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + partLabelWidth + labelGap, pos.y));

        BeginSingleValue(fieldWidth);
        ImGui::SetNextItemWidth(fieldWidth);
        PropertyEdit part;
        float shown = WithoutNegativeZero(values[i], displayFormat);
        part.changed = ImGui::DragFloat(parts[size_t(i)], &shown, speed, min, max, displayFormat, flags);
        if (part.changed)
          values[i] = shown;
        FinishEdit(part);
        EndSingleValue();
        edit |= part;
      }

      const bool reset = EndRow(defaults != nullptr);
      if (reset && defaults != nullptr && !std::equal(values, values + count, defaults))
      {
        std::copy(defaults, defaults + count, values);
        edit.changed = true;
        edit.committed = true;
      }
      return edit;
    }
  }

  void SetPreferences(EditorPreferences* preferences)
  {
    s_Preferences = preferences;
  }

  bool BeginPropertyGroup(const char* label, const GroupSpec& spec)
  {
    IM_ASSERT(s_Row.label == nullptr && "A group cannot start inside a row");
    CloseCurrentGrid();

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    if (window->SkipItems)
      return false;

    const EditorTheme& theme = EditorStyle::GetTheme();
    const float scale = Scale();
    const ImGuiID id = window->GetID(label);
    const char* labelEnd = ImGui::FindRenderedTextEnd(label);

    // Seeded from the preferences the first time this window shows the group
    ImGuiStorage* storage = window->DC.StateStorage;
    int32_t state = storage->GetInt(id, -1);
    if (state < 0)
    {
      state = LoadGroupOpen(BuildGroupKey(label, labelEnd), spec.defaultOpen) ? 1 : 0;
      storage->SetInt(id, state);
    }
    bool open = state != 0;

    EditorFonts::Push(EditorFontRole::Medium);
    const float height = ImGui::GetFrameHeight() + HEADER_EXTRA_HEIGHT * scale;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + std::max(ImGui::GetContentRegionAvail().x, 1.0f), pos.y + height));
    ImGui::ItemSize(bb, (height - g.FontSize) * 0.5f);

    ImGuiItemStatusFlags status = ImGuiItemStatusFlags_Openable;
    const bool hasAction = spec.action.label != nullptr;
    // The action button is added later on top of the header
    const bool visible = ImGui::ItemAdd(bb, id, nullptr, hasAction ? ImGuiItemFlags_AllowOverlap : ImGuiItemFlags_None);
    if (visible)
    {
      bool hovered = false;
      bool held = false;
      if (ImGui::ButtonBehavior(bb, id, &hovered, &held))
      {
        open = !open;
        storage->SetInt(id, open ? 1 : 0);
        StoreGroupOpen(BuildGroupKey(label, labelEnd), open, spec.defaultOpen);
        status |= ImGuiItemStatusFlags_ToggledOpen;
      }

      const ImU32 fill = held && hovered ? TokenColor(theme.frameHovered)
        : hovered ? TokenColor(theme.frame)
        : SurfaceColor(HEADER_SURFACE_MIX);
      window->DrawList->AddRectFilled(bb.Min, bb.Max, fill, theme.radiusSmall * scale);
      ImGui::RenderNavCursor(bb, id);

      const float gap = HEADER_GAP * scale;
      ImVec2 textPos(bb.Min.x + g.Style.FramePadding.x, bb.Min.y + (height - g.FontSize) * 0.5f);
      window->DrawList->AddText(textPos, TokenColor(theme.textSecondary), open ? ICON_LC_CHEVRON_DOWN : ICON_LC_CHEVRON_RIGHT);
      textPos.x += ImGui::CalcTextSize(ICON_LC_CHEVRON_RIGHT).x + gap;
      if (spec.icon != nullptr)
      {
        window->DrawList->AddText(textPos, TokenColor(theme.textSecondary), spec.icon);
        textPos.x += ImGui::CalcTextSize(spec.icon).x + gap;
      }
      window->DrawList->AddText(textPos, TokenColor(theme.textPrimary), label, labelEnd);

      if (g.LogEnabled)
      {
        ImGui::LogSetNextTextDecoration("###", "###");
        ImGui::LogRenderedText(&textPos, label, labelEnd);
      }
    }

    if (open)
      status |= ImGuiItemStatusFlags_Opened;
    g.LastItemData.StatusFlags |= status;
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);

    const bool showTooltip = visible && spec.tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
    EditorFonts::Pop();
    if (showTooltip)
      DrawTooltip(nullptr, nullptr, spec.tooltip, nullptr);

    if (visible && hasAction && DrawGroupAction(id, bb, spec.action) && spec.action.pressed != nullptr)
      *spec.action.pressed = true;

    if (!open)
      return false;

    ImGui::PushOverrideID(id);
    s_Scopes.push_back(ScopeFrame { .id = id, .label = label });
    return true;
  }

  void EndPropertyGroup()
  {
    IM_ASSERT(!s_Scopes.empty() && s_Scopes.back().label != nullptr && "EndPropertyGroup without an open group");
    if (s_Scopes.empty())
      return;

    CloseGrid(s_Scopes.back());
    s_Scopes.pop_back();
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0.0f, GROUP_BOTTOM_GAP * Scale()));
  }

  void BeginPropertyScope()
  {
    IM_ASSERT(s_Row.label == nullptr && "A scope cannot start inside a row");
    CloseCurrentGrid();
    s_Scopes.push_back(ScopeFrame { .id = GImGui->CurrentWindow->IDStack.back() });
  }

  void EndPropertyScope()
  {
    IM_ASSERT(!s_Scopes.empty() && s_Scopes.back().label == nullptr && "EndPropertyScope without BeginPropertyScope");
    if (s_Scopes.empty())
      return;

    CloseGrid(s_Scopes.back());
    s_Scopes.pop_back();
  }

  void PushDependency(bool satisfied, const char* reason)
  {
    s_Dependencies.push_back(satisfied ? nullptr : (reason != nullptr ? reason : "Unavailable"));
  }

  void PopDependency()
  {
    IM_ASSERT(!s_Dependencies.empty() && "PopDependency without PushDependency");
    if (!s_Dependencies.empty())
      s_Dependencies.pop_back();
  }

  PropertyEdit PropertyFloat(const char* label, float& value, const FloatSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, false))
      return {};

    char format[48];
    FormatWithUnit(format, sizeof(format), spec.format, spec.unit);
    const bool bounded = spec.min < spec.max;
    const ImGuiSliderFlags flags = bounded ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;

    const float width = ImGui::GetContentRegionAvail().x;
    BeginSingleValue(width);
    PropertyEdit edit;
    float shown = WithoutNegativeZero(value, format);
    if (spec.slider && bounded)
    {
      edit.changed = FillSliderFloat(label, shown, spec.min, spec.max, format, flags, width);
    }
    else
    {
      ImGui::SetNextItemWidth(width);
      edit.changed = ImGui::DragFloat(label, &shown, spec.speed, spec.min, spec.max, format, flags);
    }
    if (edit.changed)
      value = shown;
    FinishEdit(edit);
    EndSingleValue();

    ApplyReset(EndRow(spec.defaultValue.has_value()), value, spec.defaultValue, edit);
    return edit;
  }

  PropertyEdit PropertyInt(const char* label, int32_t& value, const IntSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, false))
      return {};

    char format[48];
    FormatWithUnit(format, sizeof(format), "%d", spec.unit);
    const ImGuiSliderFlags flags = spec.min < spec.max ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;

    const float width = ImGui::GetContentRegionAvail().x;
    BeginSingleValue(width);
    ImGui::SetNextItemWidth(width);
    PropertyEdit edit;
    edit.changed = ImGui::DragInt(label, &value, spec.speed, spec.min, spec.max, format, flags);
    FinishEdit(edit);
    EndSingleValue();

    ApplyReset(EndRow(spec.defaultValue.has_value()), value, spec.defaultValue, edit);
    return edit;
  }

  PropertyEdit PropertyUInt(const char* label, uint32_t& value, const UIntSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, false))
      return {};

    char format[48];
    FormatWithUnit(format, sizeof(format), "%lld", spec.unit);
    const int64_t lower = int64_t(spec.min);
    const int64_t upper = std::max(int64_t(spec.max), lower);
    int64_t wide = int64_t(value);

    const float width = ImGui::GetContentRegionAvail().x;
    BeginSingleValue(width);
    ImGui::SetNextItemWidth(width);
    PropertyEdit edit;
    edit.changed = ImGui::DragScalar(label, ImGuiDataType_S64, &wide, spec.speed, &lower, &upper, format, ImGuiSliderFlags_AlwaysClamp);
    FinishEdit(edit);
    EndSingleValue();

    if (edit.changed)
    {
      const uint32_t clamped = uint32_t(std::clamp(wide, lower, upper));
      edit.changed = clamped != value;
      value = clamped;
    }

    ApplyReset(EndRow(spec.defaultValue.has_value()), value, spec.defaultValue, edit);
    return edit;
  }

  PropertyEdit PropertyBool(const char* label, bool& value, const BoolSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, false))
      return {};

    BeginSingleValue(ImGui::GetFrameHeight());
    PropertyEdit edit;
    // Disabling only fades a check mark, and a faded accent still reads as a live control
    const bool disabled = s_Row.disabledReason != nullptr;
    if (disabled)
      ImGui::PushStyleColor(ImGuiCol_CheckMark, ToImGuiColor(EditorStyle::GetTheme().textSecondary));
    edit.changed = ImGui::Checkbox(label, &value);
    if (disabled)
      ImGui::PopStyleColor();
    FinishEdit(edit);
    EndSingleValue();

    ApplyReset(EndRow(spec.defaultValue.has_value()), value, spec.defaultValue, edit);
    return edit;
  }

  PropertyEdit PropertyEnum(const char* label, int32_t& index, std::span<const EnumOption> options, const EnumSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, false))
      return {};

    const bool inRange = index >= 0 && size_t(index) < options.size();
    const float width = ImGui::GetContentRegionAvail().x;
    BeginSingleValue(width);
    ImGui::SetNextItemWidth(width);
    PropertyEdit edit;
    if (ImGui::BeginCombo(label, inRange ? options[size_t(index)].label : ""))
    {
      for (size_t i = 0; i < options.size(); i++)
      {
        const EnumOption& option = options[i];
        const bool selected = int32_t(i) == index;
        const bool unavailable = option.disabledReason != nullptr;

        if (unavailable)
          ImGui::BeginDisabled();
        if (ImGui::Selectable(option.label, selected) && !selected)
        {
          index = int32_t(i);
          edit.changed = true;
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled);
        if (unavailable)
          ImGui::EndDisabled();

        if (hovered)
          DrawTooltip(nullptr, nullptr, option.tooltip, option.disabledReason);
      }
      ImGui::EndCombo();
    }
    EndSingleValue();
    edit.committed = edit.changed;

    ApplyReset(EndRow(spec.defaultValue.has_value()), index, spec.defaultValue, edit);
    return edit;
  }

  PropertyEdit PropertyColor(const char* label, glm::vec3& color, const ColorSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, true))
      return {};

    ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoLabel;
    if (spec.hdr)
      flags |= ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float;

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    PropertyEdit edit;
    // The color edit pushes its label as an id; an empty one hashes to the row itself, so its
    // fields get exact paths under the row ("<Row>/##X") instead of wildcards
    edit.changed = ImGui::ColorEdit3("", &color.x, flags);
    FinishEdit(edit);
    // The swatch reports no label of its own, which would leave it without a bridge path
    ImGuiContext& g = *GImGui;
    IMGUI_TEST_ENGINE_ITEM_INFO(ImHashStr("##ColorButton", 0, s_Row.id), "##ColorButton", ImGuiItemStatusFlags_None);

    ApplyReset(EndRow(spec.defaultValue.has_value()), color, spec.defaultValue, edit);
    return edit;
  }

  PropertyEdit PropertyVec2(const char* label, glm::vec2& value, const Vec2Spec& spec)
  {
    return PropertyVector(label, &value.x, 2, spec.speed, spec.min, spec.max, spec.format, spec.unit,
      spec.componentLabels, spec.defaultValue ? &spec.defaultValue->x : nullptr, spec.tooltip, spec.disabledReason);
  }

  PropertyEdit PropertyVec3(const char* label, glm::vec3& value, const Vec3Spec& spec)
  {
    return PropertyVector(label, &value.x, 3, spec.speed, spec.min, spec.max, spec.format, spec.unit,
      spec.componentLabels, spec.defaultValue ? &spec.defaultValue->x : nullptr, spec.tooltip, spec.disabledReason);
  }

  PropertyEdit PropertyBitmask(const char* label, uint32_t& mask, const BitmaskSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, false))
      return {};

    ImGuiContext& g = *GImGui;
    const uint32_t before = mask;
    char preview[16];
    std::snprintf(preview, sizeof(preview), "0x%08X", mask);

    // The bridge reads a combo as "{ value } label" from the text log. BeginCombo logs only the bare label
    // when the preview is drawn by the caller, so the pair is logged up front, on the row's log line.
    if (g.LogEnabled)
    {
      const ImVec2 logPos(g.CurrentWindow->DC.CursorPos.x, g.CurrentWindow->DC.CursorPos.y + g.Style.FramePadding.y);
      ImGui::LogSetNextTextDecoration("{", "}");
      ImGui::LogRenderedText(&logPos, preview);
      ImGui::LogRenderedText(nullptr, label, s_Row.labelEnd);
    }

    const float width = ImGui::GetContentRegionAvail().x;
    BeginSingleValue(width);
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, nullptr, ImGuiComboFlags_HeightLarge))
    {
      uint32_t offered = spec.flags.empty() ? UINT32_MAX : 0u;
      for (const BitmaskFlag& flag : spec.flags)
      {
        if (flag.bit < 32)
          offered |= 1u << flag.bit;
      }

      if (ImGui::Button("All"))
        mask |= offered;
      ImGui::SameLine();
      if (ImGui::Button("None"))
        mask &= ~offered;
      ImGui::Separator();

      auto drawBit = [&mask](uint32_t bit, const char* bitLabel) {
        bool set = ((mask >> bit) & 1u) != 0;
        if (ImGui::Checkbox(bitLabel, &set))
          mask = set ? (mask | (1u << bit)) : (mask & ~(1u << bit));
      };

      if (spec.flags.empty())
      {
        if (ImGui::BeginTable("##Bits", 4))
        {
          for (uint32_t bit = 0; bit < 32; bit++)
          {
            ImGui::TableNextColumn();
            char bitLabel[16];
            std::snprintf(bitLabel, sizeof(bitLabel), "Bit %u", bit);
            drawBit(bit, bitLabel);
          }
          ImGui::EndTable();
        }
      }
      else
      {
        for (const BitmaskFlag& flag : spec.flags)
        {
          if (flag.bit < 32)
            drawBit(flag.bit, flag.label);
        }
      }
      ImGui::EndCombo();
    }

    if (ImGui::BeginComboPreview())
    {
      // After the popup, which may have changed the mask this frame
      std::snprintf(preview, sizeof(preview), "0x%08X", mask);
      EditorFonts::Push(EditorFontRole::Mono);
      ImGui::TextUnformatted(preview);
      EditorFonts::Pop();
      ImGui::EndComboPreview();
    }
    EndSingleValue();

    PropertyEdit edit;
    edit.changed = mask != before;
    edit.committed = edit.changed;
    ApplyReset(EndRow(spec.defaultValue.has_value()), mask, spec.defaultValue, edit);
    return edit;
  }

  PropertyEdit PropertyTexture(const char* label, TextureHandle& handle, EditorContext& context, const TextureSlotSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, true))
      return {};

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    const EditorTheme& theme = EditorStyle::GetTheme();
    const float rounding = theme.radiusSmall * Scale();
    // As tall as the file name line plus the button line beside it
    const float thumbnail = std::floor(ImGui::GetTextLineHeight() + g.Style.ItemSpacing.y + ImGui::GetFrameHeight());

    TextureManager* textures = context.assetManager != nullptr ? &context.assetManager->Textures() : nullptr;
    const bool hasTexture = textures != nullptr && textures->Has(handle);

    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 thumbnailMax(pos.x + thumbnail, pos.y + thumbnail);
    const VkDescriptorSet descriptor = hasTexture && context.textureCache != nullptr
      ? context.textureCache->GetOrRegister(handle)
      : VK_NULL_HANDLE;
    if (descriptor != VK_NULL_HANDLE)
    {
      window->DrawList->AddImageRounded((void*)descriptor, pos, thumbnailMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
        ImGui::GetColorU32(IM_COL32_WHITE), rounding);
    }
    else
    {
      window->DrawList->AddRectFilled(pos, thumbnailMax, TokenColor(theme.frame), rounding);
      const ImVec2 iconSize = ImGui::CalcTextSize(ICON_LC_IMAGE);
      window->DrawList->AddText(ImVec2(pos.x + (thumbnail - iconSize.x) * 0.5f, pos.y + (thumbnail - iconSize.y) * 0.5f),
        TokenColor(theme.textDisabled), ICON_LC_IMAGE);
    }
    ImGui::Dummy(ImVec2(thumbnail, thumbnail));

    ImGui::SameLine(0.0f, g.Style.ItemSpacing.x);
    ImGui::BeginGroup();

    std::string_view fileName = "None";
    if (hasTexture)
    {
      const std::string_view path = textures->FindPath(handle);
      const size_t slash = path.find_last_of("/\\");
      fileName = path.empty() ? std::string_view("Unnamed texture")
        : slash == std::string_view::npos ? path : path.substr(slash + 1);
    }
    const ImVec2 namePos = window->DC.CursorPos;
    const float nameWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    DrawTextEllipsized(window->DrawList, namePos, nameWidth, TokenColor(hasTexture ? theme.textPrimary : theme.textDisabled),
      fileName.data(), fileName.data() + fileName.size());
    ImGui::Dummy(ImVec2(nameWidth, ImGui::GetTextLineHeight()));

    PropertyEdit edit;
    if (IconTextButton("Load...", ICON_LC_FOLDER_OPEN, true, 0.0f) && textures != nullptr)
    {
      const std::string path = FileDialog::OpenFile(IMAGE_FILTERS, 1);
      if (!path.empty())
      {
        handle = textures->Load(path, spec.outHasAlpha, spec.linear);
        edit.changed = true;
        edit.committed = true;
      }
    }

    ImGui::SameLine(0.0f, g.Style.ItemInnerSpacing.x);
    ImGui::BeginDisabled(!hasTexture);
    if (IconTextButton("Clear", ICON_LC_X, false, 0.0f))
    {
      handle = TextureHandle::Invalid();
      if (spec.outHasAlpha != nullptr)
        *spec.outHasAlpha = false;
      edit.changed = true;
      edit.committed = true;
    }
    ImGui::EndDisabled();

    ImGui::EndGroup();
    EndRow(false);
    return edit;
  }

  namespace
  {
    void RebuildEntityList(Scene& scene, const EntityPickerSpec& spec, ImGuiID id)
    {
      entt::registry& registry = scene.GetRegistry();
      s_EntityListId = id;
      s_EntityListScene = &scene;
      s_EntityListSearch = s_EntitySearch;
      s_EntityListGeneration = scene.GetStructureGeneration();
      s_EntityListNamedCount = registry.storage<Name>().size();
      s_EntityList.clear();

      // Over every named entity, offered or not: a stored name resolves among all of them
      std::unordered_map<std::string_view, uint32_t> nameCounts;
      if (spec.uniqueNames)
      {
        for (auto [candidate, name] : registry.view<Name>().each())
          nameCounts[name]++;
      }

      for (auto [candidate, name] : registry.view<Name>().each())
      {
        if (registry.all_of<EditorOnlyTag>(candidate))
          continue;
        if (spec.filter && !spec.filter(candidate))
          continue;
        if (!ContainsCaseInsensitive(name, s_EntitySearch))
          continue;

        auto count = nameCounts.find(name);
        s_EntityList.push_back(EntityCandidate {
          .entity = candidate,
          .sharedName = count != nameCounts.end() && count->second > 1 });
      }

      std::sort(s_EntityList.begin(), s_EntityList.end(), [&registry](const EntityCandidate& a, const EntityCandidate& b) {
        return registry.get<Name>(a.entity) < registry.get<Name>(b.entity);
      });
    }
  }

  PropertyEdit PropertyEntity(const char* label, Entity& entity, Scene& scene, const EntityPickerSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, false))
      return {};

    entt::registry& registry = scene.GetRegistry();
    char unnamed[32];
    const char* preview = EditorCommands::GetEntityDisplayName(registry, entity, unnamed);

    const float width = ImGui::GetContentRegionAvail().x;
    BeginSingleValue(width);
    ImGui::SetNextItemWidth(width);
    PropertyEdit edit;
    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLargest))
    {
      const bool appearing = ImGui::IsWindowAppearing();
      if (appearing)
      {
        s_EntitySearch[0] = '\0';
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputTextWithHint("##Search", ICON_LC_SEARCH " Search", s_EntitySearch, sizeof(s_EntitySearch));

      if (appearing || s_EntityListId != s_Row.id || s_EntityListScene != &scene || s_EntityListSearch != s_EntitySearch
        || s_EntityListGeneration != scene.GetStructureGeneration() || s_EntityListNamedCount != registry.storage<Name>().size())
      {
        RebuildEntityList(scene, spec, s_Row.id);
      }

      if (spec.allowNone && ImGui::Selectable("None", entity == entt::null) && entity != entt::null)
      {
        entity = entt::null;
        edit.changed = true;
      }

      if (s_EntityList.empty())
      {
        ImGui::TextDisabled("No matching entities");
      }
      else
      {
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const float listHeight = rowHeight * float(std::min(s_EntityList.size(), ENTITY_PICKER_VISIBLE_ROWS));
        if (ImGui::BeginChild("##Entities", ImVec2(0.0f, listHeight)))
        {
          ImGuiListClipper clipper;
          clipper.Begin(int32_t(s_EntityList.size()), rowHeight);
          while (clipper.Step())
          {
            for (int32_t i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
            {
              const EntityCandidate& candidate = s_EntityList[size_t(i)];
              // Destroyed behind the scene's back since the list was built
              if (!registry.valid(candidate.entity))
              {
                ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
                continue;
              }

              const bool selected = candidate.entity == entity;
              char candidateUnnamed[32];
              const char* candidateLabel = EditorCommands::GetEntityDisplayName(registry, candidate.entity, candidateUnnamed);
              ImGui::PushID(int32_t(entt::to_integral(candidate.entity)));
              ImGui::BeginDisabled(candidate.sharedName);
              if (ImGui::Selectable(candidateLabel, selected) && !selected && !candidate.sharedName)
              {
                entity = candidate.entity;
                edit.changed = true;
                ImGui::CloseCurrentPopup();
              }
              const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled);
              ImGui::EndDisabled();
              if (candidate.sharedName && hovered)
              {
                DrawTooltip(nullptr, nullptr, nullptr, "Another entity has the same name. The reference is stored by name "
                  "and would resolve to whichever of them the scene lists first; rename one of them to pick it.");
              }
              if (selected)
                ImGui::SetItemDefaultFocus();
              ImGui::PopID();
            }
          }
        }
        ImGui::EndChild();
      }
      ImGui::EndCombo();
    }
    EndSingleValue();
    edit.committed = edit.changed;

    EndRow(false);
    return edit;
  }

  PropertyEdit PropertyString(const char* label, std::string& value, const StringSpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, spec.disabledReason, false))
      return {};

    const ImGuiID id = s_Row.id;
    const bool editing = s_TextEditId == id && s_TextEditTarget == &value;
    std::string display;
    std::string* buffer = &s_TextEditBuffer;
    if (!editing)
    {
      display = value;
      buffer = &display;
    }

    const float width = ImGui::GetContentRegionAvail().x;
    BeginSingleValue(width);
    ImGui::SetNextItemWidth(width);
    if (spec.hint != nullptr)
      ImGui::InputTextWithHint(label, spec.hint, buffer);
    else
      ImGui::InputText(label, buffer);

    PropertyEdit edit;
    if (ImGui::IsItemActivated())
    {
      s_TextEditId = id;
      s_TextEditTarget = &value;
      if (buffer != &s_TextEditBuffer)
        s_TextEditBuffer = *buffer;
    }
    else if (editing && ImGui::IsItemDeactivated())
    {
      s_TextEditId = 0;
      s_TextEditTarget = nullptr;
      if ((spec.allowEmpty || !s_TextEditBuffer.empty()) && s_TextEditBuffer != value)
      {
        value = s_TextEditBuffer;
        edit.changed = true;
        edit.committed = true;
      }
    }
    EndSingleValue();

    EndRow(false);
    return edit;
  }

  void PropertyReadOnly(const char* label, const char* text, const ReadOnlySpec& spec)
  {
    if (!BeginRow(label, spec.tooltip, nullptr, false))
      return;

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    if (spec.mono)
      EditorFonts::Push(EditorFontRole::Mono);

    const ImVec2 pos = window->DC.CursorPos;
    const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const ImVec2 textPos(pos.x, pos.y + g.Style.FramePadding.y);
    const char* textEnd = text + std::strlen(text);
    DrawTextEllipsized(window->DrawList, textPos, width, ImGui::GetColorU32(ImGuiCol_Text), text, textEnd);
    ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
    if (g.LogEnabled)
      ImGui::LogRenderedText(&textPos, text, textEnd);

    if (spec.mono)
      EditorFonts::Pop();
    EndRow(false);
  }

  void PropertyStatus(const char* label, const char* text, StatusKind kind, const char* tooltip)
  {
    const auto drawStatus = [kind, text]() {
      ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(StatusColor(kind)));
      if (const char* icon = StatusIcon(kind))
      {
        ImGui::TextUnformatted(icon);
        ImGui::SameLine(0.0f, GImGui->Style.ItemInnerSpacing.x);
      }
      ImGui::TextWrapped("%s", text);
      ImGui::PopStyleColor();
    };

    if (label == nullptr)
    {
      CloseCurrentGrid();
      const char* reason = ActiveDisabledReason(nullptr);
      // Starts where the labels of the grid rows around it start, not at the window edge
      const float indent = (LABEL_INSET + DEPENDENCY_INDENT * float(s_Dependencies.size())) * Scale();
      ImGui::Indent(indent);
      if (reason != nullptr)
        ImGui::BeginDisabled();
      ImGui::BeginGroup();
      drawStatus();
      ImGui::EndGroup();
      const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled);
      if (reason != nullptr)
        ImGui::EndDisabled();
      ImGui::Unindent(indent);
      if (hovered)
        DrawTooltip(nullptr, nullptr, tooltip, reason);
      return;
    }

    if (!BeginRow(label, tooltip, nullptr, false))
      return;

    ImGui::AlignTextToFramePadding();
    drawStatus();
    EndRow(false);
  }

  bool PropertyButton(const char* label, const ButtonSpec& spec)
  {
    IM_ASSERT(s_Row.label == nullptr && "A full-width button cannot be placed inside a row");
    CloseCurrentGrid();

    const char* reason = ActiveDisabledReason(spec.disabledReason);
    if (reason != nullptr)
      ImGui::BeginDisabled();
    const bool pressed = IconTextButton(label, spec.icon, true, -1.0f);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled);
    if (reason != nullptr)
      ImGui::EndDisabled();

    if (hovered)
      DrawTooltip(nullptr, nullptr, spec.tooltip, reason);
    return pressed && reason == nullptr;
  }

  bool InlineButton(const char* label, const InlineButtonSpec& spec)
  {
    const char* reason = ActiveDisabledReason(spec.disabledReason);
    const bool iconOnly = spec.iconOnly && spec.icon != nullptr;
    if (reason != nullptr)
      ImGui::BeginDisabled();
    const bool pressed = IconTextButton(label, spec.icon, !iconOnly, spec.width);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled);
    if (reason != nullptr)
      ImGui::EndDisabled();

    // An icon alone does not say what the button is, so its tooltip is titled with the label
    if (hovered && (iconOnly || spec.tooltip != nullptr || reason != nullptr))
      DrawTooltip(iconOnly ? label : nullptr, iconOnly ? ImGui::FindRenderedTextEnd(label) : nullptr, spec.tooltip, reason);
    return pressed && reason == nullptr;
  }

  void PropertySubHeading(const char* text)
  {
    IM_ASSERT(s_Row.label == nullptr && "A sub-heading cannot be placed inside a row");
    CloseCurrentGrid();

    const EditorTheme& theme = EditorStyle::GetTheme();
    const float scale = Scale();
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextBorderSize, SUBHEADING_LINE * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(LABEL_INSET * scale, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ToImGuiColor(theme.textSecondary));
    EditorFonts::Push(EditorFontRole::Medium, theme.fontSizeSmall);
    ImGui::SeparatorText(text);
    EditorFonts::Pop();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
  }

  bool BeginPropertyRow(const char* label, const RowSpec& spec)
  {
    return BeginRow(label, spec.tooltip, spec.disabledReason, true);
  }

  void EndPropertyRow()
  {
    EndRow(false);
  }

  void SuspendPropertyGrid()
  {
    IM_ASSERT(s_Row.label == nullptr && "The grid cannot be suspended inside a row");
    CloseCurrentGrid();
  }

  bool AddComponentMenuItems(EditorContext& context, Entity entity)
  {
    if (context.scene == nullptr || context.assetManager == nullptr)
      return false;

    bool added = false;
    for (const EditorCommands::AddableComponent& component : EditorCommands::GetAddableComponents())
    {
      if (component.separatorBefore)
        ImGui::Separator();

      const char* reason = EditorCommands::GetAddComponentUnavailableReason(component, *context.scene, entity);
      if (ImGui::MenuItemEx(component.label, component.icon, nullptr, false, reason == nullptr))
      {
        component.add(*context.scene, *context.assetManager, entity);
        added = true;
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        DrawTooltip(nullptr, nullptr, component.tooltip, reason);
    }
    return added;
  }
}
