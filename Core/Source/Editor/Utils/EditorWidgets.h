#pragma once

#include "Pch.h"
#include "Assets/Handle.h"
#include "Scene/Scene.h"

namespace YAEngine
{
  struct EditorContext;
  struct EditorPreferences;

  // Property UI every editor panel builds on: collapsible groups with a persisted open state, a
  // two-column grid with the label on the left, and row kinds that own ids, clamping, tooltips,
  // Reset to Default and disabled dependents, so panels only describe their rows.
  //
  // Ids: a row hashes its visible label inside the id scope of its group, so bridge paths read
  // "<Window>/<Group>/<Label>" ("<Window>/<Label>" in a BeginPropertyScope block). Rows made of
  // several widgets add the part: "<Window>/<Group>/<Label>/<Part>". Labels must be unique within
  // one group or scope.
  //
  // Specs are aggregates meant for designated initializers, so their fields have to be named in
  // declaration order. Every editable row takes a tooltip and a disabledReason: a non-null reason
  // disables the row and is shown in its tooltip.
  namespace EditorWidgets
  {
    // changed: the value was written this frame; a drag reports every step.
    // committed: an edit finished this frame - a drag or text field was released or confirmed with
    // Enter, or a discrete control (checkbox, combo entry, Reset to Default, Load) set the value. A
    // drag's release frame reports committed without changed. Cheap values react to changed;
    // expensive regenerations (meshes, colliders) wait for committed.
    struct PropertyEdit
    {
      bool changed = false;
      bool committed = false;

      explicit operator bool() const { return changed; }

      PropertyEdit& operator|=(const PropertyEdit& other)
      {
        changed |= other.changed;
        committed |= other.committed;
        return *this;
      }
    };

    // Icon-only button at the right end of a group header, such as a component's Remove
    struct GroupAction
    {
      // Id and bridge label inside the group's scope: "<Window>/<Group>/<Label>"
      const char* label = nullptr;
      const char* icon = nullptr;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
      // Set to true on the frame the enabled button is pressed
      bool* pressed = nullptr;
    };

    struct GroupSpec
    {
      // ICON_LC_* glyph drawn before the title
      const char* icon = nullptr;
      // Open state until the user toggles the group; toggles are saved in the editor preferences
      bool defaultOpen = true;
      const char* tooltip = nullptr;
      // Drawn with the group open or closed; none while its label is null
      GroupAction action;
    };

    struct RowSpec
    {
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct FloatSpec
    {
      // Equal bounds leave the value unbounded; otherwise every edit, typed values included, is clamped
      float min = 0.0f;
      float max = 0.0f;
      // Per pixel of mouse drag; sliders ignore it
      float speed = 0.01f;
      const char* format = "%.3f";
      // Appended to the displayed value: "m", "deg", "s"
      const char* unit = nullptr;
      // Only with bounds
      bool slider = false;
      std::optional<float> defaultValue;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct IntSpec
    {
      int32_t min = 0;
      int32_t max = 0;
      float speed = 0.1f;
      const char* unit = nullptr;
      std::optional<int32_t> defaultValue;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    // Edited as a signed 64-bit value and clamped before it is stored, so a typed negative number
    // lands on min instead of wrapping around
    struct UIntSpec
    {
      uint32_t min = 0;
      uint32_t max = UINT32_MAX;
      float speed = 0.1f;
      const char* unit = nullptr;
      std::optional<uint32_t> defaultValue;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct BoolSpec
    {
      std::optional<bool> defaultValue;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct EnumOption
    {
      const char* label = nullptr;
      // Greys the entry out; shown when it is hovered
      const char* disabledReason = nullptr;
      const char* tooltip = nullptr;
    };

    struct EnumSpec
    {
      // Index into the options
      std::optional<int32_t> defaultValue;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct ColorSpec
    {
      // Unbounded float components for radiance; LDR colors stay within 0-1
      bool hdr = false;
      std::optional<glm::vec3> defaultValue;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    template<typename TVector>
    struct VectorSpec
    {
      float speed = 0.01f;
      float min = 0.0f;
      float max = 0.0f;
      const char* format = "%.3f";
      const char* unit = nullptr;
      // Drawn in the theme's axis colors and used as the part ids; must not be empty
      std::array<const char*, 3> componentLabels = { "X", "Y", "Z" };
      std::optional<TVector> defaultValue;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    using Vec2Spec = VectorSpec<glm::vec2>;
    using Vec3Spec = VectorSpec<glm::vec3>;

    struct BitmaskFlag
    {
      // 0-31
      uint32_t bit = 0;
      const char* label = nullptr;
    };

    struct BitmaskSpec
    {
      // Named bits; empty offers all 32 as "Bit N"
      std::span<const BitmaskFlag> flags;
      std::optional<uint32_t> defaultValue;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct TextureSlotSpec
    {
      // Data maps (normals, roughness) load linear, colors as sRGB
      bool linear = false;
      // Receives whether a loaded texture has alpha; cleared together with the slot
      bool* outHasAlpha = nullptr;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct EntityPickerSpec
    {
      bool allowNone = true;
      // For a reference stored by name: an entity whose name another entity shares is offered disabled,
      // since the name would resolve to whichever of them comes first
      bool uniqueNames = false;
      // Entities it rejects are not offered; editor-only entities never are
      std::function<bool(Entity)> filter;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct StringSpec
    {
      const char* hint = nullptr;
      bool allowEmpty = false;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct ReadOnlySpec
    {
      bool mono = false;
      const char* tooltip = nullptr;
    };

    struct ButtonSpec
    {
      const char* icon = nullptr;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    struct InlineButtonSpec
    {
      const char* icon = nullptr;
      // Shows the icon alone in a square button; the label stays its id, bridge label and tooltip title
      bool iconOnly = false;
      // 0 fits the content, negative fills the available width
      float width = 0.0f;
      const char* tooltip = nullptr;
      const char* disabledReason = nullptr;
    };

    enum class StatusKind : uint8_t
    {
      Neutral,
      Success,
      Warning,
      Error,
      Info
    };

    // Group open states are read from and saved to these preferences; null keeps them for the session.
    void SetPreferences(EditorPreferences* preferences);

    // Header plus id scope. Returns whether the group is open; call EndPropertyGroup only then.
    bool BeginPropertyGroup(const char* label, const GroupSpec& spec = {});
    void EndPropertyGroup();

    // Rows outside any group, hashed in the enclosing id scope.
    void BeginPropertyScope();
    void EndPropertyScope();

    // Rows up to the matching Pop are indented under the row they depend on and disabled while
    // the dependency is not satisfied, with the reason in their tooltip. Nests; the outermost
    // unsatisfied reason wins.
    void PushDependency(bool satisfied, const char* reason);
    void PopDependency();

    PropertyEdit PropertyFloat(const char* label, float& value, const FloatSpec& spec = {});
    PropertyEdit PropertyInt(const char* label, int32_t& value, const IntSpec& spec = {});
    PropertyEdit PropertyUInt(const char* label, uint32_t& value, const UIntSpec& spec = {});
    PropertyEdit PropertyBool(const char* label, bool& value, const BoolSpec& spec = {});
    PropertyEdit PropertyEnum(const char* label, int32_t& index, std::span<const EnumOption> options, const EnumSpec& spec = {});
    PropertyEdit PropertyColor(const char* label, glm::vec3& color, const ColorSpec& spec = {});
    PropertyEdit PropertyVec2(const char* label, glm::vec2& value, const Vec2Spec& spec = {});
    PropertyEdit PropertyVec3(const char* label, glm::vec3& value, const Vec3Spec& spec = {});
    // 32 flags edited in a popup; the row shows the mask in hex
    PropertyEdit PropertyBitmask(const char* label, uint32_t& mask, const BitmaskSpec& spec = {});
    // Thumbnail (not clickable), file name, Load... (opens a file dialog) and Clear
    PropertyEdit PropertyTexture(const char* label, TextureHandle& handle, EditorContext& context, const TextureSlotSpec& spec = {});
    // Searchable list of the scene's entities
    PropertyEdit PropertyEntity(const char* label, Entity& entity, Scene& scene, const EntityPickerSpec& spec = {});
    // Writes the target only on commit (Enter or focus loss); an empty text is rejected unless allowed
    PropertyEdit PropertyString(const char* label, std::string& value, const StringSpec& spec = {});

    void PropertyReadOnly(const char* label, const char* text, const ReadOnlySpec& spec = {});
    // Text in the semantic color of its kind; a null label spans the full width, starting where row labels start
    void PropertyStatus(const char* label, const char* text, StatusKind kind = StatusKind::Neutral, const char* tooltip = nullptr);
    // Full-width button; a disabled one never reports a press
    bool PropertyButton(const char* label, const ButtonSpec& spec = {});
    // Button the caller places, on its own line or among the widgets of a row. Its id and bridge label
    // are the label whatever it shows; a disabled one never reports a press.
    bool InlineButton(const char* label, const InlineButtonSpec& spec = {});
    // Separator with text between blocks of rows
    void PropertySubHeading(const char* text);

    // A row whose value cell holds the caller's widgets, scoped under the label: their ids read
    // "<Window>/<Group>/<Label>/<Widget>". Returns false when the row is not drawn; call
    // EndPropertyRow only after true.
    bool BeginPropertyRow(const char* label, const RowSpec& spec = {});
    void EndPropertyRow();

    // Ends the current grid so full-width ImGui content can follow; the next row starts a new one.
    void SuspendPropertyGrid();

    // Menu items for EditorCommands::GetAddableComponents, for a popup or menu the caller opened.
    // Unavailable entries are disabled with their reason in the tooltip; ids are the plain labels.
    // Returns true when a component was added.
    bool AddComponentMenuItems(EditorContext& context, Entity entity);

    template<typename TEnum> requires std::is_enum_v<TEnum>
    PropertyEdit PropertyEnum(const char* label, TEnum& value, std::span<const EnumOption> options, const EnumSpec& spec = {})
    {
      int32_t index = static_cast<int32_t>(value);
      PropertyEdit edit = PropertyEnum(label, index, options, spec);
      if (edit.changed)
        value = static_cast<TEnum>(index);
      return edit;
    }
  }
}
