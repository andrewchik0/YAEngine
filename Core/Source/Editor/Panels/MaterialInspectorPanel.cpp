#include "Editor/Panels/MaterialInspectorPanel.h"

#include <imgui.h>

#include "Assets/AssetManager.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorContext.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorWidgets.h"
#include "Render/Render.h"
#include "Scene/Components.h"

namespace YAEngine
{
  namespace
  {
    using EditorWidgets::PropertyEdit;

    constexpr EditorWidgets::EnumOption SHADING_MODELS[] = {
      { .label = "Lit", .tooltip = "Lights, shadows and indirect lighting" },
      { .label = "Unlit", .tooltip = "Albedo and textures only, without lighting" },
    };

    constexpr EditorWidgets::EnumOption TRANSMISSION_MODES[] = {
      { .label = "None", .tooltip = "Absent from the path tracer, like any transparent material without transmission" },
      { .label = "Sheet", .tooltip = "Single-layer geometry, one surface a whole pane crossed without bending: windows, bottles "
                                     "modeled as one sheet. Cheap: one query covers any number of them" },
      { .label = "ThinWalled", .tooltip = "Closed thin walls, each surface one interface crossed without bending: glasses and cups, "
                                          "no lens effect. Cheap: one query covers any number of them" },
      { .label = "Solid", .tooltip = "A closed volume light refracts into and out of: liquids, ice, thick glass - real refraction, "
                                     "one traced ray per interface, as Render Settings > PT Glass allows" },
    };

    PropertyEdit DrawSurfaceGroup(Material& mat)
    {
      PropertyEdit edit;
      if (!EditorWidgets::BeginPropertyGroup("Surface", { .icon = ICON_LC_LAYERS }))
        return edit;

      edit |= EditorWidgets::PropertyColor("Albedo", mat.albedo, {
        .defaultValue = glm::vec3(1.0f),
        .tooltip = "Base color, multiplied with the Base Color texture." });
      edit |= EditorWidgets::PropertyFloat("Roughness", mat.roughness, {
        .min = 0.0f, .max = 1.0f, .slider = true, .defaultValue = 0.5f,
        .tooltip = "Normalized 0-1. Multiplies the red channel of the Roughness texture, or the green channel "
                   "of the Metallic texture with Combined Textures." });
      edit |= EditorWidgets::PropertyFloat("Roughness Factor", mat.roughnessFactor, {
        .min = 0.0f, .max = 1.0f, .slider = true, .defaultValue = 1.0f,
        .tooltip = "Roughness factor from the model file (normalized 0-1), saved with the material. "
                   "Shading uses Roughness; the renderer does not read this value." });
      edit |= EditorWidgets::PropertyFloat("Metallic", mat.metallic, {
        .min = 0.0f, .max = 1.0f, .slider = true, .defaultValue = 0.0f,
        .tooltip = "Normalized 0-1. Multiplies the blue channel of the Metallic texture." });
      edit |= EditorWidgets::PropertyFloat("Metallic Factor", mat.metallicFactor, {
        .min = 0.0f, .max = 1.0f, .slider = true, .defaultValue = 1.0f,
        .tooltip = "Metallic factor from the model file (normalized 0-1), saved with the material. "
                   "Shading uses Metallic; the renderer does not read this value." });
      edit |= EditorWidgets::PropertyFloat("Specular", mat.specular, {
        .min = 0.0f, .max = 1.0f, .slider = true, .defaultValue = 0.5f,
        .tooltip = "Specular level from the model file (normalized 0-1), saved with the material. "
                   "The renderer does not read it." });
      EditorWidgets::PropertyReadOnly("Workflow", mat.sg ? "Specular / Glossiness" : "Metallic / Roughness", {
        .tooltip = "Read from the model file on import. A specular/glossiness material without a roughness map "
                   "uses its specular map as the Roughness texture." });
      EditorWidgets::PropertyReadOnly("Texture Has Alpha", mat.hasAlpha ? "Yes" : "No", {
        .tooltip = "Whether the Base Color texture has an alpha channel. Detected when the texture loads; "
                   "Alpha Test defaults to it." });

      EditorWidgets::EndPropertyGroup();
      return edit;
    }

    PropertyEdit DrawClearCoatGroup(Material& mat)
    {
      PropertyEdit edit;
      if (!EditorWidgets::BeginPropertyGroup("Clear Coat", { .icon = ICON_LC_SPARKLES }))
        return edit;

      EditorWidgets::PushDependency(IsClearCoatShaded(mat), mat.transparent
        ? "A transparent material ignores the coat"
        : "The Unlit shading model ignores the coat");
      edit |= EditorWidgets::PropertyFloat("Weight", mat.clearCoat, {
        .min = 0.0f, .max = 1.0f, .slider = true, .defaultValue = 0.0f,
        .tooltip = "Normalized 0-1. A smooth varnish layer over the surface, like car paint, in raster and the path "
                   "tracer; 0 is no coat. The coat follows the mesh normal while the surface under it keeps its normal "
                   "map and every other texture. Stored in 256 steps: a weight below 0.002 is no coat." });

      EditorWidgets::PushDependency(mat.clearCoat > 0.0f, "Requires a Weight");
      edit |= EditorWidgets::PropertyFloat("Roughness", mat.clearCoatRoughness, {
        .min = 0.0f, .max = 1.0f, .slider = true, .defaultValue = 0.0f,
        .tooltip = "Normalized 0-1, the coat's own roughness. Surface > Roughness stays the roughness of the surface "
                   "under the coat." });
      EditorWidgets::PopDependency();
      EditorWidgets::PopDependency();

      EditorWidgets::EndPropertyGroup();
      return edit;
    }

    PropertyEdit DrawEmissionGroup(Material& mat)
    {
      PropertyEdit edit;
      if (!EditorWidgets::BeginPropertyGroup("Emission", { .icon = ICON_LC_SUN }))
        return edit;

      edit |= EditorWidgets::PropertyBool("Emissive", mat.emissive, {
        .defaultValue = false,
        .tooltip = "Texels whose emission is brighter than a small cutoff are shaded as pure emission "
                   "instead of a PBR surface." });

      EditorWidgets::PushDependency(mat.emissive, "Requires Emissive");
      edit |= EditorWidgets::PropertyColor("Color", mat.emissivity, {
        .hdr = true, .defaultValue = glm::vec3(0.0f),
        .tooltip = "Emitted color (HDR), multiplied with the Emissive texture." });
      edit |= EditorWidgets::PropertyFloat("Intensity", mat.emissiveIntensity, {
        .min = 0.0f, .max = 1000.0f, .speed = 0.1f, .format = "%.2f", .defaultValue = 1.0f,
        .tooltip = "Multiplier on Color (glTF emissive strength)." });
      EditorWidgets::PopDependency();

      EditorWidgets::EndPropertyGroup();
      return edit;
    }

    PropertyEdit DrawOpacityGroup(Material& mat)
    {
      PropertyEdit edit;
      if (!EditorWidgets::BeginPropertyGroup("Opacity", { .icon = ICON_LC_BLEND }))
        return edit;

      edit |= EditorWidgets::PropertyBool("Alpha Test", mat.alphaTest, {
        .defaultValue = mat.hasAlpha,
        .tooltip = "Cuts out texels whose Base Color texture alpha is below 0.5. Defaults to Texture Has Alpha." });
      edit |= EditorWidgets::PropertyBool("Transparent", mat.transparent, {
        .defaultValue = false,
        .tooltip = "Draws in the forward transparent pass, blended by Opacity." });

      EditorWidgets::PushDependency(mat.transparent, "Requires Transparent");
      edit |= EditorWidgets::PropertyFloat("Opacity", mat.opacity, {
        .min = 0.0f, .max = 1.0f, .slider = true, .defaultValue = 1.0f,
        .tooltip = "Normalized 0-1. Multiplies the Base Color texture alpha." });
      edit |= EditorWidgets::PropertyFloat("Fresnel Opacity", mat.fresnelOpacity, {
        .min = 0.0f, .max = 1.0f, .slider = true, .defaultValue = 0.0f,
        .tooltip = "Normalized 0-1. Raises opacity toward 1 at grazing angles." });
      EditorWidgets::PopDependency();

      EditorWidgets::EndPropertyGroup();
      return edit;
    }

    PropertyEdit DrawTransmissionGroup(Material& mat, Render* render)
    {
      PropertyEdit edit;
      if (!EditorWidgets::BeginPropertyGroup("Transmission (Path Tracing)", { .icon = ICON_LC_GLASS_WATER }))
        return edit;

      // Still editable: the mode is authored for when the switch is on.
      if (render != nullptr && !render->GetPathTraceGlass())
      {
        EditorWidgets::PropertyStatus(nullptr, "No effect while PT Glass is off", EditorWidgets::StatusKind::Info,
          "Render Settings > Render Path > PT Glass > Glass. Off, the path tracer ignores the Mode and the raster "
          "forward pass draws this transparent material over the traced image.");
      }

      EditorWidgets::PushDependency(mat.transparent, "Requires Transparent");
      edit |= EditorWidgets::PropertyEnum("Mode", mat.transmissionMode, TRANSMISSION_MODES, {
        .defaultValue = int32_t(TransmissionMode::None),
        .tooltip = "How the path tracer sees this surface: a perfectly smooth dielectric that ignores Albedo, Metallic, "
                   "Roughness and the Opacity group. Raster ignores it." });

      EditorWidgets::PushDependency(mat.transmissionMode != TransmissionMode::None, "Requires a Mode");
      edit |= EditorWidgets::PropertyFloat("IOR", mat.ior, {
        .min = MIN_TRANSMISSION_IOR, .max = MAX_TRANSMISSION_IOR, .slider = true, .defaultValue = 1.5f,
        .tooltip = "Index of refraction, 1.33 for water and 1.5 for glass. Sets how much the surface reflects and, for "
                   "Solid, how much it bends light." });
      edit |= EditorWidgets::PropertyColor("Transmittance Color", mat.transmittanceColor, {
        .defaultValue = glm::vec3(1.0f),
        .tooltip = "Solid and ThinWalled: the color white light keeps after Transmittance Distance inside the volume or the "
                   "wall, so thicker parts get deeper. Sheet: the tint of one pass through the sheet. White absorbs nothing." });
      edit |= EditorWidgets::PropertyFloat("Transmittance Distance", mat.transmittanceDistance, {
        .min = MIN_TRANSMITTANCE_DISTANCE, .max = 1000.0f, .speed = 0.001f, .unit = "m", .defaultValue = 1.0f,
        .tooltip = "Solid and ThinWalled: world units inside after which Transmittance Color is left. A Sheet does not use "
                   "it: its color is the tint of one crossing." });

      EditorWidgets::PushDependency(mat.transmissionMode == TransmissionMode::Solid, "Requires Mode Solid");
      edit |= EditorWidgets::PropertyInt("Medium Priority", mat.mediumPriority, {
        .min = 0, .max = MAX_MEDIUM_PRIORITY, .speed = 0.05f, .defaultValue = 0,
        .tooltip = "Where Solid volumes overlap, the higher priority owns the overlap and the other's surfaces inside it "
                   "are ignored. Give a liquid a higher priority than the glass wall it is modeled into." });
      EditorWidgets::PopDependency();
      EditorWidgets::PopDependency();
      EditorWidgets::PopDependency();

      EditorWidgets::EndPropertyGroup();
      return edit;
    }

    PropertyEdit DrawShadingGroup(Material& mat)
    {
      PropertyEdit edit;
      if (!EditorWidgets::BeginPropertyGroup("Shading", { .icon = ICON_LC_CONTRAST }))
        return edit;

      edit |= EditorWidgets::PropertyEnum("Shading Model", mat.shadingModel, SHADING_MODELS, {
        .defaultValue = int32_t(ShadingModel::Lit),
        .tooltip = "Lit shades the surface with lights and GI. Unlit shows albedo and textures without lighting." });
      edit |= EditorWidgets::PropertyBool("Double Sided", mat.doubleSided, {
        .defaultValue = false,
        .tooltip = "Renders back faces instead of culling them." });

      EditorWidgets::EndPropertyGroup();
      return edit;
    }

    PropertyEdit DrawUvGroup(Material& mat)
    {
      PropertyEdit edit;
      if (!EditorWidgets::BeginPropertyGroup("UV", { .icon = ICON_LC_GRID_3X3 }))
        return edit;

      // Floor above zero: a zero factor collapses the whole texture into one texel
      edit |= EditorWidgets::PropertyVec2("UV Scale", mat.uvScale, {
        .speed = 0.01f, .min = 0.001f, .max = 1000.0f, .componentLabels = { "U", "V", "" },
        .defaultValue = glm::vec2(1.0f),
        .tooltip = "Texture tiling: every texture fetch of this material multiplies the mesh UVs by it." });
      edit |= EditorWidgets::PropertyBool("Combined Textures", mat.combinedTextures, {
        .defaultValue = false,
        .tooltip = "The Metallic texture carries roughness in its green channel (glTF packed "
                   "metallic-roughness); the Roughness texture is ignored." });

      EditorWidgets::EndPropertyGroup();
      return edit;
    }

    PropertyEdit DrawTexturesGroup(Material& mat, EditorContext& context)
    {
      PropertyEdit edit;
      if (!EditorWidgets::BeginPropertyGroup("Textures", { .icon = ICON_LC_IMAGE }))
        return edit;

      edit |= EditorWidgets::PropertyTexture("Base Color", mat.baseColorTexture, context, {
        .linear = false, .outHasAlpha = &mat.hasAlpha,
        .tooltip = "sRGB. RGB multiplies Albedo; alpha drives Alpha Test and Opacity." });
      edit |= EditorWidgets::PropertyTexture("Metallic", mat.metallicTexture, context, {
        .linear = true,
        .tooltip = "Linear. Blue multiplies Metallic; green is roughness with Combined Textures." });
      edit |= EditorWidgets::PropertyTexture("Roughness", mat.roughnessTexture, context, {
        .linear = true,
        .tooltip = "Linear. Red multiplies Roughness. Ignored with Combined Textures." });
      edit |= EditorWidgets::PropertyTexture("Specular", mat.specularTexture, context, {
        .linear = true,
        .tooltip = "Linear. Saved and bound with the material; no shader samples it." });
      edit |= EditorWidgets::PropertyTexture("Emissive", mat.emissiveTexture, context, {
        .linear = false,
        .tooltip = "sRGB. Multiplies the emission Color while Emissive is on." });
      edit |= EditorWidgets::PropertyTexture("Normal", mat.normalTexture, context, {
        .linear = true,
        .tooltip = "Linear tangent-space normal map." });
      edit |= EditorWidgets::PropertyTexture("Height", mat.heightTexture, context, {
        .linear = true,
        .tooltip = "Linear. Saved and bound with the material; no shader samples it." });

      EditorWidgets::EndPropertyGroup();
      return edit;
    }
  }

  void MaterialInspectorPanel::OnRender(EditorContext& context)
  {
    if (!BeginPanel())
    {
      ImGui::End();
      return;
    }

    Material* matPtr = context.selectedMaterial && context.assetManager
      ? context.assetManager->Materials().TryGet(context.selectedMaterial)
      : nullptr;
    if (!matPtr)
    {
      ImGui::TextDisabled("No material selected");
      ImGui::End();
      return;
    }

    Material& mat = *matPtr;

    EditorWidgets::BeginPropertyScope();
    EditorWidgets::PropertyString("Name", mat.name, {
      .hint = "Material name",
      .tooltip = "Applied on Enter or when the field loses focus. An empty name is rejected." });

    const size_t users = context.scene != nullptr ? m_Users.Get(*context.scene, context.selectedMaterial, ImGui::GetTime()) : 0;
    char usage[32];
    std::snprintf(usage, sizeof(usage), "%zu %s", users, users == 1 ? "entity" : "entities");
    EditorWidgets::PropertyReadOnly("Used By", usage, {
      .tooltip = "Entities whose Material component uses this material. Edits here change all of them." });
    EditorWidgets::EndPropertyScope();

    PropertyEdit edit;
    edit |= DrawSurfaceGroup(mat);
    edit |= DrawClearCoatGroup(mat);
    edit |= DrawEmissionGroup(mat);
    edit |= DrawOpacityGroup(mat);
    edit |= DrawTransmissionGroup(mat, context.render);
    edit |= DrawShadingGroup(mat);
    edit |= DrawUvGroup(mat);
    edit |= DrawTexturesGroup(mat, context);

    if (edit.changed)
      mat.MarkChanged();

    ImGui::End();
  }
}
