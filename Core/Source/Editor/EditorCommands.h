#pragma once

#include "Assets/Handle.h"
#include "Editor/Utils/ShotTemplates.h"
#include "Scene/Scene.h"
#include "Utils/PrimitiveMeshFactory.h"

namespace YAEngine
{
  struct EditorContext;
  class AssetManager;
  class SequencePlayer;
  class Render;

  // Editor operations shared by the panels and the agent bridge actions, so both go through the
  // same code. Selection, window focus and rename state stay with the caller.
  namespace EditorCommands
  {
    enum class NewEntityKind : uint8_t
    {
      Empty,
      PointLight,
      SpotLight,
      DirectionalLight,
      Terrain,
      Camera,
      ReflectionProbe,
      IrradianceVolume
    };

    // An entry of the Add Component list shared by the Details popup and the Outliner context menu.
    struct AddableComponent
    {
      // Menu label and bridge label
      const char* label = nullptr;
      const char* icon = nullptr;
      const char* tooltip = nullptr;
      // Starts a new block of entries
      bool separatorBefore = false;
      // Null when the component can be added to the entity, otherwise why it cannot
      const char* (*unavailableReason)(Scene& scene, Entity entity) = nullptr;
      void (*add)(Scene& scene, AssetManager& assets, Entity entity) = nullptr;
    };

    // Point, Spot and Directional Light, Camera, Motion Path, Reflection Probe, Irradiance Volume, Terrain, Road,
    // Scatter and Collider, in menu order.
    std::span<const AddableComponent> GetAddableComponents();
    // Null when the component can be added to the entity, otherwise why it cannot. Runtime scatter output
    // takes no component at all.
    const char* GetAddComponentUnavailableReason(const AddableComponent& component, Scene& scene, Entity entity);

    // An entry of the Outliner Create menu, named the way the menu names it.
    Entity CreateEntity(Scene& scene, AssetManager& assets, NewEntityKind kind);
    const char* GetPrimitiveName(PrimitiveType type);
    Entity CreatePrimitive(Scene& scene, AssetManager& assets, PrimitiveType type);

    // A Model asset points back at exactly one root entity, so a second instance can only come
    // from loading the file again. entt::null when the file cannot be loaded.
    Entity DuplicateModel(Scene& scene, AssetManager& assets, Entity modelRoot);
    // Deletes the entity with its subtree and drops the selection when it lies inside that subtree.
    void DeleteEntity(EditorContext& context, Entity entity);
    // An empty name leaves the entity as it was.
    bool RenameEntity(Scene& scene, Entity entity, std::string_view name);
    // What the editor UI calls an entity: its name, "Entity <id>" without one, "None" for null.
    std::string GetEntityDisplayName(const entt::registry& registry, Entity entity);
    // The same text without an allocation: a named entity's own string, otherwise written into buffer
    const char* GetEntityDisplayName(const entt::registry& registry, Entity entity, std::span<char> buffer);
    // Entities whose Material component uses the material.
    size_t CountMaterialUsers(Scene& scene, MaterialHandle material);

    // CountMaterialUsers for a count shown every frame. Recounts when the material or the number of
    // Material components changes, otherwise at most every REFRESH_SECONDS: a material reassigned
    // somewhere else changes no count this could watch.
    class MaterialUserCount
    {
    public:
      static constexpr double REFRESH_SECONDS = 0.5;

      // now: any clock in seconds, such as ImGui::GetTime
      size_t Get(Scene& scene, MaterialHandle material, double now);

    private:
      MaterialHandle m_Material = MaterialHandle::Invalid();
      size_t m_ComponentCount = SIZE_MAX;
      double m_CountedAt = 0.0;
      size_t m_Count = 0;
    };

    // FindEntityByName for a reference stored by name and shown every frame (Cluster Source, Aim Target).
    // Scans again only when the name or the scene's structure generation changes.
    class EntityNameLookup
    {
    public:
      // The entity FindEntityByName resolves the name to, or entt::null
      Entity Resolve(Scene& scene, std::string_view name);
      // Entities carrying the name as of the last Resolve; above 1 the reference is ambiguous
      uint32_t GetMatchCount() const { return m_MatchCount; }

    private:
      Scene* m_Scene = nullptr;
      uint64_t m_Generation = 0;
      std::string m_Name;
      Entity m_Entity = entt::null;
      uint32_t m_MatchCount = 0;
    };
    // Root entity of the imported model, or entt::null when the file cannot be imported.
    Entity ImportModel(AssetManager& assets, const std::string& path);

    // False when the image cannot be loaded; the skybox then stays as it was.
    bool LoadSkybox(Scene& scene, AssetManager& assets, const std::string& path);

    // Why the renderer cannot show a debug view right now, or nullptr when it can.
    const char* GetDebugViewUnavailableReason(Render& render, int view);

    // The Sequencer Play button: resumes a paused or scrubbed session of this shot, otherwise
    // plays the shot from its start. A null track plays the whole timeline through the active
    // camera. Returns true when it resumed.
    bool PlaySequence(SequencePlayer& player, Scene& scene, Entity cameraTrack);

    struct AddShotResult
    {
      // The camera passed in, or the one created for the shot
      Entity camera = entt::null;
      // The shot's keys are [firstKey, firstKey + keyCount) of the camera's track
      int32_t firstKey = -1;
      int32_t keyCount = 0;
      // Empty on success; on failure nothing was changed
      std::string error;
      // Set on success when a track-wide setting changed that the track's keys outside the shot use too
      std::string warning;
    };

    // Applies a shot template. A null camera gets a new root "Shot N" entity with a Camera and a Camera Track,
    // posed at the shot's start. The track's keys inside [start, start + duration] are replaced, so a template
    // can be applied again over the same range. target: the entity the shot follows or aims at; it must have a
    // unique name, since the track refers to it by name. Fly To ignores it.
    AddShotResult AddShot(Scene& scene, ShotTemplate shot, const ShotTemplateParams& params, Entity target,
      Entity camera, const ShotViewPoses& views);
  }
}
