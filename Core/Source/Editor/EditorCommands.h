#pragma once

#include "Scene/Scene.h"
#include "Utils/PrimitiveMeshFactory.h"

namespace YAEngine
{
  struct EditorContext;
  class AssetManager;
  class CameraTrackPlayer;
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
      Terrain
    };

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
    // Root entity of the imported model, or entt::null when the file cannot be imported.
    Entity ImportModel(AssetManager& assets, const std::string& path);

    // False when the image cannot be loaded; the skybox then stays as it was.
    bool LoadSkybox(Scene& scene, AssetManager& assets, const std::string& path);

    // Why the renderer cannot show a debug view right now, or nullptr when it can.
    const char* GetDebugViewUnavailableReason(Render& render, int view);

    // The Sequencer Play button: resumes a paused session of this track, otherwise plays the
    // track from its start. Returns true when it resumed.
    bool PlayCameraTrack(CameraTrackPlayer& player, Scene& scene, Entity track);
  }
}
