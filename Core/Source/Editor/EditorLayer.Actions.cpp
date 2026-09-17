#include "Editor/EditorLayer.h"

#include "Assets/AssetManager.h"
#include "Editor/Bridge/BridgeActions.h"
#include "Editor/Bridge/BridgeTypes.h"
#include "Editor/EditorCameraLayer.h"
#include "Editor/EditorCommands.h"
#include "Editor/SequencerEditing.h"
#include "LayerManager.h"
#include "Render/Render.h"
#include "Scene/SequencePlayer.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Utils/CameraOrientation.h"
#include "Utils/DebugViews.h"
#include "Utils/FrameCaptureSpec.h"
#include "Utils/Log.h"
#include "Utils/ServiceRegistry.h"

namespace YAEngine
{
  namespace
  {
    using ParamType = BridgeActionParamType;

    constexpr const char* PATH_RULE = "absolute, or relative to the asset base path (the directory holding the "
      "Assets folder of the open scene)";

    BridgeActionParam RequiredParam(std::string name, ParamType type, std::string description)
    {
      return BridgeActionParam { .name = std::move(name), .type = type, .required = true,
        .description = std::move(description) };
    }

    BridgeActionParam OptionalParam(std::string name, ParamType type, std::string description)
    {
      return BridgeActionParam { .name = std::move(name), .type = type, .required = false,
        .description = std::move(description) };
    }

    Json EntityResult(Scene& scene, Entity entity)
    {
      Json result = Json::object();
      result["entity"] = entt::to_integral(entity);
      result["name"] = scene.GetName(entity);
      return result;
    }

    unsigned EntityIdForLog(Entity entity)
    {
      return static_cast<unsigned>(entt::to_integral(entity));
    }

    // Absolute and normalized, so a result names the file that was actually used.
    std::string ResolveActionPath(AssetManager& assets, const std::string& path)
    {
      std::string resolved = assets.ResolvePath(path);
      std::error_code ec;
      std::filesystem::path absolute = std::filesystem::absolute(resolved, ec);
      return ec ? resolved : absolute.lexically_normal().string();
    }

    // The reasons are worded for tooltips, with line breaks.
    std::string SingleLine(std::string text)
    {
      std::replace(text.begin(), text.end(), '\n', ' ');
      return text;
    }

    // Fails the reply when the optional 'name' param is given but empty.
    bool ReadOptionalName(const BridgeActionArgs& args, const BridgeReply& reply, std::string& outName)
    {
      if (!args.Has("name"))
        return true;

      outName = args.GetString("name");
      if (outName.empty())
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'name' must not be empty");
        return false;
      }
      return true;
    }

    // In creation order, the order scene.entities lists roots in.
    template<typename T>
    std::vector<Entity> CollectEntitiesWith(Scene& scene)
    {
      std::vector<Entity> entities;
      for (Entity entity : scene.GetView<T>())
        entities.push_back(entity);
      std::sort(entities.begin(), entities.end(),
        [](Entity a, Entity b) { return entt::to_entity(a) < entt::to_entity(b); });
      return entities;
    }
  }

  void EditorLayer::RegisterBridgeActions()
  {
    BridgeActions& actions = m_Bridge.GetActions();
    RegisterSelectionActions(actions);
    RegisterEntityActions(actions);
    RegisterViewActions(actions);
    RegisterSceneActions(actions);
    RegisterRenderActions(actions);
    RegisterPlaybackActions(actions);
  }

  void EditorLayer::RegisterSelectionActions(BridgeActions& actions)
  {
    actions.Register({
      .name = "selection.get",
      .description = "Return the entity selected in the editor as {entity, name}; both are null when nothing is selected.",
      .handler = [this](const BridgeActionArgs&, const BridgeReply& reply) {
        Entity selected = m_Context.selectedEntity;
        if (selected == entt::null || !GetScene().GetRegistry().valid(selected))
        {
          Json result = Json::object();
          result["entity"] = nullptr;
          result["name"] = nullptr;
          reply.Ok(std::move(result));
          return;
        }

        reply.Ok(EntityResult(GetScene(), selected));
      }
    });

    actions.Register({
      .name = "selection.set",
      .description = "Select an entity, as clicking it in the Outliner does: the Details panel shows it and the "
        "transform gizmo moves to it. Returns {entity, name}.",
      .params = { RequiredParam("entity", ParamType::Entity, "Entity id from scene.entities.") },
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        Entity entity = args.GetEntity("entity");
        m_Context.SelectEntity(entity);
        reply.Ok(EntityResult(GetScene(), entity));
      }
    });

    actions.Register({
      .name = "selection.clear",
      .description = "Clear the selection, as clicking empty space in the Outliner does.",
      .handler = [this](const BridgeActionArgs&, const BridgeReply& reply) {
        m_Context.ClearSelection();
        reply.Ok();
      }
    });
  }

  void EditorLayer::RegisterEntityActions(BridgeActions& actions)
  {
    actions.Register({
      .name = "entity.create",
      .description = "Create a root entity, as the Outliner Create menu does, and select it. Returns {entity, name}.",
      .params = {
        RequiredParam("type", ParamType::String, "empty, pointLight, spotLight, directionalLight or terrain."),
        OptionalParam("name", ParamType::String, "Name of the new entity. Default: the menu's name (Entity, "
          "PointLight, SpotLight, DirectionalLight, Terrain)."),
      },
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        using Kind = EditorCommands::NewEntityKind;
        const std::pair<std::string_view, Kind> kinds[] = {
          { "empty", Kind::Empty },
          { "pointLight", Kind::PointLight },
          { "spotLight", Kind::SpotLight },
          { "directionalLight", Kind::DirectionalLight },
          { "terrain", Kind::Terrain },
        };

        std::string type = args.GetString("type");
        auto kind = std::find_if(std::begin(kinds), std::end(kinds),
          [&type](const auto& entry) { return entry.first == type; });
        if (kind == std::end(kinds))
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS,
            "'type' must be empty, pointLight, spotLight, directionalLight or terrain");
          return;
        }

        std::string name;
        if (!ReadOptionalName(args, reply, name))
          return;

        Scene& scene = GetScene();
        Entity entity = EditorCommands::CreateEntity(scene, GetAssets(), kind->second);
        EditorCommands::RenameEntity(scene, entity, name);
        m_Context.SelectEntity(entity);

        YA_LOG_INFO("Bridge", "Action entity.create: '%s' (%u)", scene.GetName(entity).c_str(), EntityIdForLog(entity));
        reply.Ok(EntityResult(scene, entity));
      }
    });

    actions.Register({
      .name = "entity.createPrimitive",
      .description = "Create a root entity with a primitive mesh and the default material, as the Outliner Create > "
        "Primitives menu does, and select it. Returns {entity, name}.",
      .params = {
        RequiredParam("shape", ParamType::String, "cube, sphere or plane."),
        OptionalParam("name", ParamType::String, "Name of the new entity. Default: Cube, Sphere or Plane."),
      },
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        const std::pair<std::string_view, PrimitiveType> shapes[] = {
          { "cube", PrimitiveType::Box },
          { "sphere", PrimitiveType::Sphere },
          { "plane", PrimitiveType::Plane },
        };

        std::string shapeName = args.GetString("shape");
        auto shape = std::find_if(std::begin(shapes), std::end(shapes),
          [&shapeName](const auto& entry) { return entry.first == shapeName; });
        if (shape == std::end(shapes))
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'shape' must be cube, sphere or plane");
          return;
        }

        std::string name;
        if (!ReadOptionalName(args, reply, name))
          return;

        Scene& scene = GetScene();
        Entity entity = EditorCommands::CreatePrimitive(scene, GetAssets(), shape->second);
        EditorCommands::RenameEntity(scene, entity, name);
        m_Context.SelectEntity(entity);

        YA_LOG_INFO("Bridge", "Action entity.createPrimitive: '%s' (%u)", scene.GetName(entity).c_str(),
          EntityIdForLog(entity));
        reply.Ok(EntityResult(scene, entity));
      }
    });

    actions.Register({
      .name = "entity.delete",
      .description = "Delete an entity with its whole subtree, as the Outliner Delete item does. A deleted node of an "
        "imported model is recorded as a model override when the scene is saved. Returns {entity, name} of the "
        "deleted entity.",
      .params = { RequiredParam("entity", ParamType::Entity, "Entity id from scene.entities.") },
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        Entity entity = args.GetEntity("entity");
        Json result = EntityResult(scene, entity);

        YA_LOG_INFO("Bridge", "Action entity.delete: '%s' (%u)", scene.GetName(entity).c_str(), EntityIdForLog(entity));
        EditorCommands::DeleteEntity(m_Context, entity);
        reply.Ok(std::move(result));
      }
    });

    actions.Register({
      .name = "entity.duplicate",
      .description = "Duplicate an entity with its subtree next to the original, as the Outliner Duplicate item does, "
        "and select the copy. A model root is loaded from its file again; anything else is copied component by "
        "component. Returns {entity, name} of the copy.",
      .params = { RequiredParam("entity", ParamType::Entity, "Entity id from scene.entities.") },
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        Entity entity = args.GetEntity("entity");

        Entity copy = scene.HasComponent<ModelSourceComponent>(entity)
          ? EditorCommands::DuplicateModel(scene, GetAssets(), entity)
          : scene.DuplicateEntity(entity, m_Registry->Get<ComponentRegistry>());
        if (copy == entt::null)
        {
          reply.Fail(BridgeErrorCode::FAILED, "cannot load the model file of '" + scene.GetName(entity)
            + "' again; see log.tail");
          return;
        }

        m_Context.SelectEntity(copy);
        YA_LOG_INFO("Bridge", "Action entity.duplicate: '%s' (%u) -> '%s' (%u)", scene.GetName(entity).c_str(),
          EntityIdForLog(entity), scene.GetName(copy).c_str(), EntityIdForLog(copy));
        reply.Ok(EntityResult(scene, copy));
      }
    });

    actions.Register({
      .name = "entity.rename",
      .description = "Rename an entity, as renaming it in the Outliner does. Returns {entity, name}.",
      .params = {
        RequiredParam("entity", ParamType::Entity, "Entity id from scene.entities."),
        RequiredParam("name", ParamType::String, "New name, not empty. The scene file finds parents by name, so a "
          "name another entity already has can reparent entities when the scene is loaded again."),
      },
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        Entity entity = args.GetEntity("entity");
        std::string previous = scene.GetName(entity);

        if (!EditorCommands::RenameEntity(scene, entity, args.GetString("name")))
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'name' must not be empty");
          return;
        }

        YA_LOG_INFO("Bridge", "Action entity.rename: '%s' -> '%s' (%u)", previous.c_str(),
          scene.GetName(entity).c_str(), EntityIdForLog(entity));
        reply.Ok(EntityResult(scene, entity));
      }
    });

    actions.Register({
      .name = "model.import",
      .description = "Import a model file, as the Outliner Import Model item does, and select its root. The import "
        "runs on the editor's main thread, so the editor stalls until a large model is in. Returns {entity, name, "
        "path}.",
      .params = {
        RequiredParam("path", ParamType::Path, std::string("Model file (gltf, glb, obj or fbx): ") + PATH_RULE + "."),
        OptionalParam("parent", ParamType::Entity, "Entity to parent the model root under. Default: none, a root."),
      },
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        AssetManager& assets = GetAssets();
        std::string path = ResolveActionPath(assets, args.GetString("path"));
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec))
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "no file '" + path + "'");
          return;
        }

        Entity root = EditorCommands::ImportModel(assets, path);
        if (root == entt::null)
        {
          reply.Fail(BridgeErrorCode::FAILED, "cannot import '" + path + "'; see log.tail");
          return;
        }

        Scene& scene = GetScene();
        if (args.Has("parent"))
        {
          scene.SetParent(root, args.GetEntity("parent"));
          scene.MarkDirty(root);
        }
        m_Context.SelectEntity(root);

        YA_LOG_INFO("Bridge", "Action model.import: '%s' as '%s' (%u)", path.c_str(), scene.GetName(root).c_str(),
          EntityIdForLog(root));
        Json result = EntityResult(scene, root);
        result["path"] = path;
        reply.Ok(std::move(result));
      }
    });
  }

  void EditorLayer::RegisterViewActions(BridgeActions& actions)
  {
    auto cameraPose = [this](EditorCameraLayer& camera) {
      glm::vec3 position = camera.GetPosition();
      Json result = Json::object();
      result["position"] = Json::array({ position.x, position.y, position.z });
      result["yaw"] = glm::degrees(camera.GetYaw());
      result["pitch"] = glm::degrees(camera.GetPitch());
      result["active"] = GetScene().GetActiveCamera() == camera.GetCameraEntity();
      return result;
    };

    actions.Register({
      .name = "camera.get",
      .description = "Return the editor camera pose: position [x, y, z], yaw and pitch in degrees (yaw 0 looks down "
        "-Z, 90 down -X; a positive pitch looks up), and active: whether the viewport and capture shots currently "
        "look through it rather than through a previewed scene camera or a playing camera track.",
      .handler = [this, cameraPose](const BridgeActionArgs&, const BridgeReply& reply) {
        EditorCameraLayer* camera = GetLayerManager().GetLayer<EditorCameraLayer>();
        if (camera == nullptr)
        {
          reply.Fail(BridgeErrorCode::FAILED, "the editor camera layer is not running");
          return;
        }

        reply.Ok(cameraPose(*camera));
      }
    });

    actions.Register({
      .name = "camera.set",
      .description = "Move the editor camera, as flying it there would. Give a position, a yaw and/or pitch, or a "
        "lookAt point; what is not given stays as it is. While piloting (sequencer.pilot) the move counts as flying. "
        "Returns the pose afterwards, as camera.get does.",
      .params = {
        OptionalParam("position", ParamType::Vec3, "World position [x, y, z]."),
        OptionalParam("yaw", ParamType::Number, "Degrees about world up; 0 looks down -Z, 90 down -X."),
        OptionalParam("pitch", ParamType::Number, "Degrees; positive looks up. Clamped to [-90, 90]."),
        OptionalParam("lookAt", ParamType::Vec3, "World point [x, y, z] to face from the new position, instead of "
          "yaw and pitch."),
      },
      .refusedWhileCapturing = true,
      .handler = [this, cameraPose](const BridgeActionArgs& args, const BridgeReply& reply) {
        EditorCameraLayer* camera = GetLayerManager().GetLayer<EditorCameraLayer>();
        if (camera == nullptr)
        {
          reply.Fail(BridgeErrorCode::FAILED, "the editor camera layer is not running");
          return;
        }

        bool hasAngles = args.Has("yaw") || args.Has("pitch");
        if (!args.Has("position") && !hasAngles && !args.Has("lookAt"))
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS, "give a position, a yaw, a pitch or a lookAt point");
          return;
        }
        if (hasAngles && args.Has("lookAt"))
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS, "lookAt replaces yaw and pitch; give one or the other");
          return;
        }

        glm::vec3 position = args.Has("position") ? args.GetVec3("position") : camera->GetPosition();
        float yaw = camera->GetYaw();
        float pitch = camera->GetPitch();

        if (args.Has("lookAt"))
        {
          glm::vec3 forward = args.GetVec3("lookAt") - position;
          if (glm::length(forward) <= 1e-6f)
          {
            reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'lookAt' must differ from the camera position");
            return;
          }
          YawPitchFromForward(glm::normalize(forward), yaw, pitch);
        }
        else
        {
          if (args.Has("yaw"))
            yaw = glm::radians(static_cast<float>(args.GetNumber("yaw")));
          if (args.Has("pitch"))
            pitch = glm::radians(static_cast<float>(args.GetNumber("pitch")));
        }

        camera->SetPose(position, yaw, pitch);
        SequencerEditing::NotifyEditorCameraFlown(m_Context);
        reply.Ok(cameraPose(*camera));
      }
    });

    actions.Register({
      .name = "view.setDebugView",
      .description = "Switch the viewport debug view, as the View menu of the viewport toolbar does. A view the "
        "device or the current render path cannot produce is refused with the reason the menu gives. Not saved "
        "with the scene or the editor preferences. Returns {id, slug, name}.",
      .params = { RequiredParam("view", ParamType::String, "Slug such as off, albedo, normals or pt-noisy, or the "
        "numeric id as text; capture.targets lists them all.") },
      .refusedWhileCapturing = true,
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        std::string text = args.GetString("view");
        int view = ParseDebugView(text);
        if (view < 0)
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS, "unknown debug view '" + text
            + "'; capture.targets lists the ids and slugs");
          return;
        }

        Render& render = GetRender();
        if (const char* reason = EditorCommands::GetDebugViewUnavailableReason(render, view))
        {
          reply.Fail(BridgeErrorCode::FAILED, SingleLine(reason));
          return;
        }

        render.SetDebugView(view);

        Json result = Json::object();
        result["id"] = view;
        result["slug"] = GetDebugViewSlug(view);
        result["name"] = GetDebugViewName(view);
        reply.Ok(std::move(result));
      }
    });

    actions.Register({
      .name = "view.setGizmos",
      .description = "Show or hide the editor gizmos drawn over the viewport (light, probe and camera icons, the "
        "transform gizmo, volume bounds, camera tracks), as the Gizmos entry of the viewport toolbar's Show menu does. "
        "Unlike that entry it is not saved in the editor preferences. Capture shots hide them by themselves while "
        "they run. Returns {enabled}.",
      .params = { RequiredParam("enabled", ParamType::Bool, "True shows the gizmos, false hides them.") },
      .refusedWhileCapturing = true,
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        bool enabled = args.GetBool("enabled");
        GetRender().GetGizmosEnabled() = enabled;

        Json result = Json::object();
        result["enabled"] = enabled;
        reply.Ok(std::move(result));
      }
    });
  }

  void EditorLayer::RegisterSceneActions(BridgeActions& actions)
  {
    actions.Register({
      .name = "scene.new",
      .description = "Close the open scene without saving it and start an empty one, as File > New Scene does. "
        "Completes once the empty scene is in place; entity ids from before are gone. Returns {scenePath}, empty.",
      .refusedWhileCapturing = true,
      .refusedWhileMinimized = true,
      .handler = [this](const BridgeActionArgs&, const BridgeReply& reply) {
        // Applied by Update, like the menu item, rather than in the middle of the frame
        b_PendingNewScene = true;
        YA_LOG_INFO("Bridge", "Action scene.new");

        m_Bridge.GetActions().Defer(reply, [this](const BridgeReply& pending) {
          if (b_PendingNewScene)
            return false;

          Json result = Json::object();
          result["scenePath"] = m_CurrentScenePath;
          pending.Ok(std::move(result));
          return true;
        });
      }
    });

    actions.Register({
      .name = "scene.open",
      .description = "Open a scene file, as File > Open Scene does; unsaved changes to the open scene are lost. "
        "Completes once the scene has loaded, which takes a while for a large scene; entity ids from before are gone. "
        "Returns {scenePath}.",
      .params = { RequiredParam("path", ParamType::Path, std::string("Scene file (.scene): ") + PATH_RULE + ".") },
      .refusedWhileCapturing = true,
      .refusedWhileMinimized = true,
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        std::string path = ResolveActionPath(GetAssets(), args.GetString("path"));
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec))
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "no file '" + path + "'");
          return;
        }

        m_PendingScenePath = path;
        YA_LOG_INFO("Bridge", "Action scene.open: '%s'", path.c_str());

        m_Bridge.GetActions().Defer(reply, [this, path](const BridgeReply& pending) {
          if (!m_PendingScenePath.empty())
            return false;

          // The serializer records the scene path only once the file has parsed
          if (GetScene().GetScenePath() != path)
          {
            pending.Fail(BridgeErrorCode::FAILED, "cannot load '" + path + "'; see log.tail");
            return true;
          }

          Json result = Json::object();
          result["scenePath"] = m_CurrentScenePath;
          pending.Ok(std::move(result));
          return true;
        });
      }
    });

    actions.Register({
      .name = "scene.save",
      .description = "Save the open scene. Without a path this is File > Save Scene; with one it is Save Scene As, "
        "and the editor continues on the new path. An existing file is overwritten. Returns {scenePath}.",
      .params = { OptionalParam("path", ParamType::Path, std::string("Scene file to write, in an existing directory: ")
        + PATH_RULE + ". Default: the path the scene was opened from or last saved to.") },
      .refusedWhileCapturing = true,
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        std::string path = m_CurrentScenePath;
        if (args.Has("path"))
        {
          path = ResolveActionPath(GetAssets(), args.GetString("path"));
          std::filesystem::path directory = std::filesystem::path(path).parent_path();
          std::error_code ec;
          if (!std::filesystem::is_directory(directory, ec))
          {
            reply.Fail(BridgeErrorCode::NOT_FOUND, "the directory '" + directory.string() + "' does not exist");
            return;
          }
          if (std::filesystem::is_directory(path, ec))
          {
            reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'" + path + "' is a directory; give the scene file to write");
            return;
          }
        }
        else if (path.empty())
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS, "the scene has never been saved; give a 'path'");
          return;
        }

        if (!SaveSceneTo(path))
        {
          reply.Fail(BridgeErrorCode::FAILED, "cannot write '" + path + "'; see log.tail");
          return;
        }

        Json result = Json::object();
        result["scenePath"] = m_CurrentScenePath;
        reply.Ok(std::move(result));
      }
    });

    actions.Register({
      .name = "skybox.set",
      .description = "Load an HDR image as the scene skybox, as Load Skybox in Render Settings does. Returns {skybox}, "
        "the path the way the scene file stores it.",
      .params = { RequiredParam("path", ParamType::Path, std::string("Equirectangular .hdr image: ") + PATH_RULE + ".") },
      .handler = [this](const BridgeActionArgs& args, const BridgeReply& reply) {
        AssetManager& assets = GetAssets();
        std::string path = ResolveActionPath(assets, args.GetString("path"));
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec))
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "no file '" + path + "'");
          return;
        }

        if (!EditorCommands::LoadSkybox(GetScene(), assets, path))
        {
          reply.Fail(BridgeErrorCode::FAILED, "cannot load the skybox '" + path + "'; see log.tail");
          return;
        }

        YA_LOG_INFO("Bridge", "Action skybox.set: '%s'", path.c_str());
        Json result = Json::object();
        result["skybox"] = assets.MakeRelative(path);
        reply.Ok(std::move(result));
      }
    });

    actions.Register({
      .name = "skybox.clear",
      .description = "Remove the scene skybox, as Clear Skybox in Render Settings does.",
      .handler = [this](const BridgeActionArgs&, const BridgeReply& reply) {
        GetScene().SetSkybox({});
        reply.Ok();
      }
    });
  }

  void EditorLayer::RegisterRenderActions(BridgeActions& actions)
  {
    auto probeResult = [](Scene& scene, Entity entity) {
      const auto& probe = scene.GetComponent<ReflectionProbeComponent>(entity);
      Json result = EntityResult(scene, entity);
      result["baked"] = probe.baked;
      result["atlasSlot"] = probe.atlasSlot;
      result["bakedPrefilter"] = probe.bakedPrefilterPath;
      return result;
    };

    auto volumeResult = [](Scene& scene, Entity entity) {
      const auto& volume = scene.GetComponent<IrradianceVolumeComponent>(entity);
      Json result = EntityResult(scene, entity);
      result["baked"] = volume.baked;
      result["bakedVolume"] = volume.bakedVolumePath;
      return result;
    };

    const std::string bakeNote = " The editor does nothing else until the bake is done, which can take minutes. "
      "Baked files go to <asset base path>/Assets/Probes, named after the entity, and replace earlier ones.";

    const std::string volumeBakeNote = " Volumes are baked by ray tracing, with the volumeSamples, volumeBounces and "
      "volumeFireflyClamp render settings; fails on a device without the ray traced baker.";

    const std::string volumeBakeUnavailable = "irradiance volumes are baked by ray tracing, and the ray traced baker "
      "is unavailable: no hardware ray tracing pipeline or no bindless texture table";

    actions.Register({
      .name = "bake.allProbes",
      .description = "Bake every reflection probe of the scene, as Bake All Reflection Probes in Render Settings "
        "does, with the configured bounce count. Probe captures are lit by the irradiance volumes, so bake those first "
        "(bake.allVolumes)." + bakeNote + " Returns {probes: [{entity, name, baked, atlasSlot, "
        "bakedPrefilter}]}.",
      .refusedWhileCapturing = true,
      .handler = [this, probeResult](const BridgeActionArgs&, const BridgeReply& reply) {
        Scene& scene = GetScene();
        std::vector<Entity> probes = CollectEntitiesWith<ReflectionProbeComponent>(scene);
        if (probes.empty())
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "the scene has no reflection probes");
          return;
        }

        GetRender().BakeAllProbes(scene, GetAssets());

        Json list = Json::array();
        for (Entity probe : probes)
          list.push_back(probeResult(scene, probe));

        Json result = Json::object();
        result["probes"] = std::move(list);
        reply.Ok(std::move(result));
      }
    });

    actions.Register({
      .name = "bake.allVolumes",
      .description = "Bake every irradiance volume of the scene, as Bake All Volumes in Render Settings does."
        + volumeBakeNote + bakeNote + " Fails, with nothing baked, when the ray traced bake scene cannot be built. "
        "Returns {volumes: [{entity, name, baked, bakedVolume, rebaked}], allRebaked}. rebaked is whether this call "
        "baked, saved and uploaded the volume; one that failed before saving (invalid volume description, InvalidDesc: "
        "e.g. min spacing above max spacing; brick layout limit or validation; integration; every integrated node buried "
        "with no virtual offset accepted, too close to geometry or enclosed by it, nothing saved; a baked volume the GPU "
        "volume storage refuses on its own - device 3D texture limit, brick pool or indirection atlas - nothing saved; "
        "or file write, see log.tail) keeps its earlier file, which baked and bakedVolume then describe. One saved but "
        "left out of the reload for lack of room next to the other volumes has rebaked and baked false.",
      .refusedWhileCapturing = true,
      .handler = [this, volumeResult, volumeBakeUnavailable](const BridgeActionArgs&, const BridgeReply& reply) {
        Scene& scene = GetScene();
        std::vector<Entity> volumes = CollectEntitiesWith<IrradianceVolumeComponent>(scene);
        if (volumes.empty())
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "the scene has no irradiance volumes");
          return;
        }

        if (!GetRender().IsRayTracedBakeAvailable())
        {
          reply.Fail(BridgeErrorCode::FAILED, volumeBakeUnavailable);
          return;
        }

        const IrradianceVolumeBakeAllResult bake = GetRender().BakeAllIrradianceVolumes(scene, GetAssets());
        if (!bake.bakeSceneBuilt)
        {
          reply.Fail(BridgeErrorCode::FAILED, "no volume was baked: the ray traced bake scene could not be built, "
            "nothing in the scene is traceable (no objects, or every object excluded from bakes); see log.tail");
          return;
        }

        Json list = Json::array();
        bool allRebaked = true;
        for (Entity volume : volumes)
        {
          auto outcome = std::find_if(bake.volumes.begin(), bake.volumes.end(),
            [volume](const IrradianceVolumeBakeAllResult::Volume& v) { return v.entity == volume; });
          const bool rebaked = outcome != bake.volumes.end() && outcome->rebaked;
          allRebaked = allRebaked && rebaked;

          Json entry = volumeResult(scene, volume);
          entry["rebaked"] = rebaked;
          list.push_back(std::move(entry));
        }

        Json result = Json::object();
        result["volumes"] = std::move(list);
        result["allRebaked"] = allRebaked;
        reply.Ok(std::move(result));
      }
    });

    actions.Register({
      .name = "bake.probe",
      .description = "Bake one reflection probe, as Bake Probe in its Details panel section does." + bakeNote
        + " Returns {entity, name, baked, atlasSlot, bakedPrefilter}.",
      .params = { RequiredParam("entity", ParamType::Entity, "Entity with a reflectionProbe component.") },
      .refusedWhileCapturing = true,
      .handler = [this, probeResult](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        Entity entity = args.GetEntity("entity");
        if (!scene.HasComponent<ReflectionProbeComponent>(entity))
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "entity " + std::to_string(entt::to_integral(entity))
            + " has no 'reflectionProbe' component");
          return;
        }

        GetRender().BakeProbe(entity, scene, GetAssets());

        // A probe the atlas has no slot for is skipped with nothing but a log line
        if (scene.GetComponent<ReflectionProbeComponent>(entity).atlasSlot == 0)
        {
          reply.Fail(BridgeErrorCode::FAILED, "no free reflection probe atlas slot for '" + scene.GetName(entity)
            + "'; see log.tail");
          return;
        }

        reply.Ok(probeResult(scene, entity));
      }
    });

    actions.Register({
      .name = "bake.volume",
      .description = "Bake one irradiance volume, as Bake Volume in its Details panel section does." + volumeBakeNote
        + bakeNote + " Returns {entity, name, baked, bakedVolume}.",
      .params = { RequiredParam("entity", ParamType::Entity, "Entity with an irradianceVolume component.") },
      .refusedWhileCapturing = true,
      .handler = [this, volumeResult, volumeBakeUnavailable](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        Entity entity = args.GetEntity("entity");
        if (!scene.HasComponent<IrradianceVolumeComponent>(entity))
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "entity " + std::to_string(entt::to_integral(entity))
            + " has no 'irradianceVolume' component");
          return;
        }

        if (!GetRender().IsRayTracedBakeAvailable())
        {
          reply.Fail(BridgeErrorCode::FAILED, volumeBakeUnavailable);
          return;
        }

        if (!GetRender().BakeIrradianceVolume(entity, scene, GetAssets()))
        {
          reply.Fail(BridgeErrorCode::FAILED, "the bake of '" + scene.GetName(entity) + "' failed: the ray traced bake "
            "scene could not be built (nothing traceable in the scene), the volume description is invalid (InvalidDesc: "
            "e.g. min spacing above max spacing), the brick layout exceeded a limit or failed validation, the "
            "integration failed, every integrated node was buried with no virtual offset accepted, too close to geometry or "
            "enclosed by it (nothing saved), the GPU volume storage would refuse the baked volume on its own - device 3D "
            "texture limit, brick pool or indirection atlas (nothing saved), the "
            "file could not be written (a complete bake that could not replace the old file is kept as <file>.tmp), or the "
            "saved volume was left out of the reload for lack of room next to the other volumes; see log.tail");
          return;
        }

        reply.Ok(volumeResult(scene, entity));
      }
    });

    actions.Register({
      .name = "bake.previewVolumePlacement",
      .description = "Lay out the sparse adaptive bricks of one irradiance volume without baking, as Preview Placement "
        "in its Details panel does: bricks refine from maxSpacing down to minSpacing where ray traced geometry queries "
        "find surfaces near. The preview is kept for the Details summary line, the Developer panel's Irradiance Volume "
        "Diagnostics group and the brick gizmo. The editor does nothing else "
        "until it is done; fails on a device without the ray traced baker. Returns {entity, name, error, minSpacing, "
        "maxSpacing, levels: [{spacing, bricks, uniqueNodes, stitchedNodes}] coarse to fine, totals: {bricks, "
        "uniqueNodes, stitchedNodes, bakedNodes, indirectionCells}, estimates: {runtimeVramBytes, diskBytes, "
        "bakePrimarySamples, volumeSamples}, validation: {passed, failure}, timings: {totalSeconds, layoutSeconds, "
        "querySeconds, queryBatches, queryPoints}}. error is null unless the volume description is invalid "
        "(InvalidDesc: e.g. non-positive half extents, min spacing above max spacing, a box too far from the origin), "
        "the layout exceeded a limit or a query failed; levels, totals and estimates are then empty and validation "
        "is null. Stitched nodes are interpolated "
        "rather than baked, so bakePrimarySamples is bakedNodes times the volumeSamples render setting.",
      .params = { RequiredParam("entity", ParamType::Entity, "Entity with an irradianceVolume component.") },
      .refusedWhileCapturing = true,
      .handler = [this, volumeBakeUnavailable](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        Entity entity = args.GetEntity("entity");
        if (!scene.HasComponent<IrradianceVolumeComponent>(entity))
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "entity " + std::to_string(entt::to_integral(entity))
            + " has no 'irradianceVolume' component");
          return;
        }

        if (!GetRender().IsRayTracedBakeAvailable())
        {
          reply.Fail(BridgeErrorCode::FAILED, volumeBakeUnavailable);
          return;
        }

        const IrradianceVolumePlacementPreview* preview =
          GetRender().PreviewIrradianceVolumePlacement(entity, scene, GetAssets());
        if (preview == nullptr)
        {
          reply.Fail(BridgeErrorCode::FAILED, "the placement preview of '" + scene.GetName(entity) + "' could not "
            "run: the ray traced bake scene could not be built (nothing traceable in the scene); see log.tail");
          return;
        }

        const bool layoutBuilt = preview->error.empty();
        const uint32_t volumeSamples = uint32_t(std::clamp(GetRender().GetVolumeSampleCount(),
          Render::MIN_VOLUME_SAMPLES, Render::MAX_VOLUME_SAMPLES));
        const IrradianceVolumePlacementEstimate estimate =
          Render::EstimateIrradianceVolumePlacement(*preview, volumeSamples);

        Json result = EntityResult(scene, entity);
        result["error"] = layoutBuilt ? Json(nullptr) : Json(preview->error);
        result["minSpacing"] = preview->fingerprint.minSpacing;
        result["maxSpacing"] = preview->fingerprint.maxSpacing;

        Json levels = Json::array();
        Json totals = Json::object();
        Json estimates = Json::object();
        Json validation = nullptr;
        if (layoutBuilt)
        {
          for (uint32_t level = preview->maxSpacingIndex + 1; level-- > preview->minSpacingIndex;)
          {
            const IrradianceBrickLevelStats& stats = preview->levelStats[level];
            Json entry = Json::object();
            entry["spacing"] = IRRADIANCE_SPACINGS[level];
            entry["bricks"] = stats.bricks;
            entry["uniqueNodes"] = stats.uniqueNodes;
            entry["stitchedNodes"] = stats.stitchedNodes;
            levels.push_back(std::move(entry));
          }

          totals["bricks"] = estimate.bricks;
          totals["uniqueNodes"] = estimate.uniqueNodes;
          totals["stitchedNodes"] = estimate.stitchedNodes;
          totals["bakedNodes"] = estimate.bakedNodes;
          totals["indirectionCells"] = estimate.indirectionCells;

          estimates["runtimeVramBytes"] = estimate.runtimeBytes;
          estimates["diskBytes"] = estimate.diskBytes;
          estimates["bakePrimarySamples"] = estimate.primarySamples;
          estimates["volumeSamples"] = volumeSamples;

          validation = Json::object();
          validation["passed"] = preview->validationPassed;
          validation["failure"] = preview->validationFailure;
        }
        result["levels"] = std::move(levels);
        result["totals"] = std::move(totals);
        result["estimates"] = std::move(estimates);
        result["validation"] = std::move(validation);

        Json timings = Json::object();
        timings["totalSeconds"] = preview->totalSeconds;
        timings["layoutSeconds"] = preview->layoutSeconds;
        timings["querySeconds"] = preview->querySeconds;
        timings["queryBatches"] = preview->queryBatches;
        timings["queryPoints"] = preview->queryPoints;
        result["timings"] = std::move(timings);
        reply.Ok(std::move(result));
      }
    });

    actions.Register({
      .name = "shaders.recompileAll",
      .description = "Recompile every shader and rebuild the pipelines that use them; shader hot reload does the same "
        "for shader files that change on disk. Completes when the whole batch is done. When a shader does not compile "
        "the action fails and the pipelines stay as they were; log.tail has the compiler output. Returns {shaderCount}.",
      .refusedWhileCapturing = true,
      .refusedWhileMinimized = true,
      .handler = [this](const BridgeActionArgs&, const BridgeReply& reply) {
        ShaderHotReload& hotReload = GetRender().GetShaderHotReload();
        if (hotReload.IsCompiling())
        {
          reply.Fail(BridgeErrorCode::BUSY, "a shader compile batch is already running");
          return;
        }

        uint32_t shaderCount = hotReload.RecompileAll();

        m_Bridge.GetActions().Defer(reply, [this, shaderCount](const BridgeReply& pending) {
          ShaderHotReload& batch = GetRender().GetShaderHotReload();
          if (batch.IsCompiling())
            return false;

          uint32_t failures = batch.GetLastBatchFailureCount();
          if (shaderCount > 0 && failures > 0)
          {
            pending.Fail(BridgeErrorCode::FAILED, std::to_string(failures) + " of " + std::to_string(shaderCount)
              + " shaders failed to compile; log.tail has the errors");
            return true;
          }

          Json result = Json::object();
          result["shaderCount"] = shaderCount;
          pending.Ok(std::move(result));
          return true;
        });
      }
    });
  }

  void EditorLayer::RegisterPlaybackActions(BridgeActions& actions)
  {
    // Camera track to play: the explicit one, else the Sequencer's, else the only one in the scene.
    // Fails the reply and returns false when none can be chosen.
    auto resolveTrack = [this](const BridgeActionArgs& args, const BridgeReply& reply, Entity& track) {
      Scene& scene = GetScene();
      track = entt::null;

      if (args.Has("entity"))
      {
        track = args.GetEntity("entity");
        if (!scene.HasComponent<CameraTrackComponent>(track))
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "entity " + std::to_string(entt::to_integral(track))
            + " has no 'cameraTrack' component");
          return false;
        }
      }
      else if (m_Context.sequencerTrack != entt::null && scene.GetRegistry().valid(m_Context.sequencerTrack)
        && scene.HasComponent<CameraTrackComponent>(m_Context.sequencerTrack))
      {
        track = m_Context.sequencerTrack;
      }
      else
      {
        std::vector<Entity> tracks = CollectEntitiesWith<CameraTrackComponent>(scene);
        if (tracks.empty())
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "the scene has no camera track; pass timeline: true to play the "
            "motion paths through the active camera");
          return false;
        }
        if (tracks.size() > 1)
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS, "the scene has " + std::to_string(tracks.size())
            + " camera tracks; give the 'entity' to play");
          return false;
        }
        track = tracks.front();
      }

      if (!scene.HasComponent<CameraComponent>(track))
      {
        reply.Fail(BridgeErrorCode::FAILED, "'" + scene.GetName(track) + "' has a camera track but no camera");
        return false;
      }
      if (scene.GetComponent<CameraTrackComponent>(track).keys.empty())
      {
        reply.Fail(BridgeErrorCode::FAILED, "the camera track of '" + scene.GetName(track) + "' has no keys");
        return false;
      }
      return true;
    };

    auto sessionResult = [this]() {
      const SequencePlayer& player = m_Registry->Get<SequencePlayer>();
      Json result = Json::object();
      result["active"] = player.IsActive();
      result["playing"] = player.IsAdvancing();
      result["time"] = player.GetTime();
      result["start"] = player.GetStartTime();
      result["end"] = player.GetEndTime();
      return result;
    };

    actions.Register({
      .name = "sequencer.play",
      .description = "Play the timeline, as the Play button of the Sequencer does. With a camera track this plays its "
        "shot (first to last key) through that camera; motion paths are posed at the same timeline time. A paused or "
        "scrubbed session of the same shot resumes. Every transform the session moved is restored when it ends. "
        "Returns {entity, name, resumed, active, playing, time, start, end}; entity is absent for timeline: true.",
      .params = {
        OptionalParam("entity", ParamType::Entity, "Entity with a cameraTrack component. Default: the track bound in "
          "the Sequencer panel, else the only camera track of the scene."),
        OptionalParam("timeline", ParamType::Bool, "True plays the whole timeline without a camera track, through the "
          "active camera. Default false.") },
      .refusedWhileCapturing = true,
      .handler = [this, resolveTrack, sessionResult](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        bool timeline = args.Has("timeline") && args.GetBool("timeline");

        Entity track = entt::null;
        if (!timeline && !resolveTrack(args, reply, track))
          return;

        bool resumed = EditorCommands::PlaySequence(m_Registry->Get<SequencePlayer>(), scene, track);

        Json result = track != entt::null ? EntityResult(scene, track) : Json::object();
        result.update(sessionResult());
        result["resumed"] = resumed;
        reply.Ok(std::move(result));
      }
    });

    actions.Register({
      .name = "sequencer.setTime",
      .description = "Pose the timeline at a time, as scrubbing the Sequencer does. Without a running session this "
        "opens a paused preview that leaves the active camera alone; the transforms it moves are restored by "
        "sequencer.stop. A pilot is put back on its track pose at the new time. Returns {active, playing, time, start, end}.",
      .params = {
        RequiredParam("time", ParamType::Number, "Seconds on the timeline, clamped to its length."),
        OptionalParam("entity", ParamType::Entity, "Camera track to pose along. Default: the session's track, else "
          "the track bound in the Sequencer panel, else none.") },
      .refusedWhileCapturing = true,
      .handler = [this, sessionResult](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        SequencePlayer& player = m_Registry->Get<SequencePlayer>();

        Entity track = player.IsActive() ? player.GetCameraTrack() : m_Context.sequencerTrack;
        if (args.Has("entity"))
        {
          track = args.GetEntity("entity");
          if (!scene.HasComponent<CameraTrackComponent>(track))
          {
            reply.Fail(BridgeErrorCode::NOT_FOUND, "entity " + std::to_string(entt::to_integral(track))
              + " has no 'cameraTrack' component");
            return;
          }
        }
        if (track != entt::null && !scene.GetRegistry().valid(track))
          track = entt::null;

        player.Scrub(scene, track, args.GetNumber("time"));
        m_Context.sequencerScrubRequest = float(player.GetTime());
        reply.Ok(sessionResult());
      }
    });

    actions.Register({
      .name = "sequencer.stop",
      .description = "End the timeline session, as the Stop button of the Sequencer does: every transform it moved "
        "goes back and the viewport returns to the camera that was active before. Returns {wasActive}.",
      .refusedWhileCapturing = true,
      .handler = [this](const BridgeActionArgs&, const BridgeReply& reply) {
        SequencePlayer& player = m_Registry->Get<SequencePlayer>();
        bool wasActive = player.IsActive();
        player.Stop(GetScene());

        Json result = Json::object();
        result["wasActive"] = wasActive;
        reply.Ok(std::move(result));
      }
    });

    RegisterPilotActions(actions);
    RegisterShotActions(actions);
  }

  void EditorLayer::RegisterPilotActions(BridgeActions& actions)
  {
    auto pilotResult = [this]() {
      Scene& scene = GetScene();
      Json result = Json::object();
      const bool piloting = m_Context.IsPiloting();
      result["piloting"] = piloting;
      result["entity"] = piloting ? Json(entt::to_integral(m_Context.pilotTrack)) : Json(nullptr);
      result["name"] = piloting ? Json(scene.GetName(m_Context.pilotTrack)) : Json(nullptr);
      result["modified"] = m_Context.pilotModified;
      result["time"] = m_Context.sequencerPlayhead;
      return result;
    };

    actions.Register({
      .name = "sequencer.pilot",
      .description = "Enter or leave pilot mode, as the Pilot and Exit Pilot buttons of the Sequencer do. Piloting puts "
        "the editor camera on the camera track's pose and fov at the playhead (roll dropped); it can then be moved "
        "with camera.set, sequencer.setTime puts it back on the track, and sequencer.setKey writes its pose as the "
        "key at the playhead. Playback makes it follow the shot. The viewport keeps looking through the editor camera "
        "with the 16:9 output framed. Leaving puts the editor camera pose and fov back; the pilot also ends on a "
        "camera preview, a scene save or load, and when the track is deleted or unbound. Returns {piloting, entity, "
        "name, modified, time}; entity and name are null while not piloting.",
      .params = {
        RequiredParam("enabled", ParamType::Bool, "True enters pilot mode, false leaves it."),
        OptionalParam("entity", ParamType::Entity, "Entity with a cameraTrack and a camera component; it gets bound in "
          "the Sequencer. Default: the track bound in the Sequencer, else the only camera track of the scene. "
          "Ignored when leaving.") },
      .refusedWhileCapturing = true,
      .handler = [this, pilotResult](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        if (!args.GetBool("enabled"))
        {
          SequencerEditing::StopPilot(m_Context);
          reply.Ok(pilotResult());
          return;
        }

        Entity track = entt::null;
        if (args.Has("entity"))
        {
          track = args.GetEntity("entity");
          if (!scene.HasComponent<CameraTrackComponent>(track))
          {
            reply.Fail(BridgeErrorCode::NOT_FOUND, "entity " + std::to_string(entt::to_integral(track))
              + " has no 'cameraTrack' component");
            return;
          }
        }
        else if (SequencerEditing::GetBoundTrack(m_Context) != nullptr)
        {
          track = m_Context.sequencerTrack;
        }
        else
        {
          std::vector<Entity> tracks = CollectEntitiesWith<CameraTrackComponent>(scene);
          if (tracks.size() != 1)
          {
            reply.Fail(tracks.empty() ? BridgeErrorCode::NOT_FOUND : BridgeErrorCode::INVALID_PARAMS,
              tracks.empty() ? std::string("the scene has no camera track")
                : "the scene has " + std::to_string(tracks.size()) + " camera tracks; give the 'entity' to pilot");
            return;
          }
          track = tracks.front();
        }

        if (const char* reason = SequencerEditing::GetPilotUnavailableReason(m_Context, track))
        {
          reply.Fail(BridgeErrorCode::FAILED, reason);
          return;
        }

        SequencerEditing::StartPilot(m_Context, track);
        YA_LOG_INFO("Bridge", "Action sequencer.pilot: piloting '%s' (%u)", scene.GetName(track).c_str(),
          EntityIdForLog(track));
        reply.Ok(pilotResult());
      }
    });

    actions.Register({
      .name = "sequencer.setKey",
      .description = "Press K in the Sequencer. While piloting, writes the editor camera pose and the track fov as the "
        "camera key at the playhead; a key already within 10 ms of it keeps its time and roll. Otherwise adds a key "
        "from the editor camera at the playhead, unless one is already there, which is then only selected. Refused "
        "while a piloted shot is playing. Returns {entity, name, key, keyCount, time, created, piloting}; key is the "
        "selected key index (-1 when none), created whether the key count grew.",
      .refusedWhileCapturing = true,
      .handler = [this](const BridgeActionArgs&, const BridgeReply& reply) {
        Scene& scene = GetScene();
        SequencerEditing::ResolveBinding(m_Context);
        CameraTrackComponent* track = SequencerEditing::GetBoundTrack(m_Context);
        if (track == nullptr)
        {
          reply.Fail(BridgeErrorCode::FAILED, "no camera track is bound in the Sequencer; select a camera with a track "
            "or call sequencer.pilot with its entity");
          return;
        }

        const Entity trackEntity = m_Context.sequencerTrack;
        const size_t keysBefore = track->keys.size();
        const bool pilotPlaying = m_Context.pilotTrack == trackEntity
          && m_Registry->Get<SequencePlayer>().IsAdvancing();
        if (pilotPlaying)
        {
          reply.Fail(BridgeErrorCode::FAILED, "the piloted shot is playing; pause it first");
          return;
        }

        SequencerEditing::SetKey(m_Context);

        const int key = m_Context.sequencerSelectedKey;
        Json result = EntityResult(scene, trackEntity);
        result["key"] = key;
        result["keyCount"] = track->keys.size();
        result["time"] = key >= 0 && key < int(track->keys.size()) ? track->keys[key].time : m_Context.sequencerPlayhead;
        result["created"] = track->keys.size() > keysBefore;
        result["piloting"] = m_Context.pilotTrack == trackEntity;
        reply.Ok(std::move(result));
      }
    });
  }

  void EditorLayer::RegisterShotActions(BridgeActions& actions)
  {
    std::string templateNames;
    for (size_t i = 0; i < size_t(ShotTemplate::Count); i++)
    {
      if (i > 0)
        templateNames += ", ";
      for (const char* c = ShotTemplates::GetInfo(ShotTemplate(i)).name; *c != '\0'; c++)
      {
        if (*c != ' ' && *c != '-')
          templateNames += *c;
      }
    }

    actions.Register({
      .name = "sequencer.addShot",
      .description = "Add a ready-made camera move, as Add Shot in the Sequencer does. The template becomes ordinary camera "
        "keys (relative to the target for the moves that follow it) plus the track settings the move needs; the camera's "
        "keys inside [start, start + duration] are replaced, so running it again over the same range redoes the shot. On "
        "success the camera is bound in the Sequencer, the shot's first key selected and the playhead moved to the start. "
        "StaticFollow stands at the editor camera; FlyTo flies from the editor camera to the pose the Sequencer's bound "
        "track has at start (5 m ahead of the editor camera when that track has no keys). Returns {entity, name, "
        "template, created, firstKey, keyCount, start, end, warning, ignored}; warning is empty unless a track-wide setting "
        "changed that other keys of that camera use too, ignored lists the given params the template does not read.",
      .params = {
        RequiredParam("template", ParamType::String, "One of " + templateNames + "; case, spaces and hyphens are ignored."),
        OptionalParam("start", ParamType::Number, "Timeline seconds the shot begins at. Default: the Sequencer playhead."),
        OptionalParam("duration", ParamType::Number, "Seconds. Default: the template's."),
        OptionalParam("target", ParamType::Entity, "Entity to follow or aim at; its name must be unique. Default: the "
          "motion path bound in the Sequencer, else the only motion path of the scene. Ignored by FlyTo."),
        OptionalParam("camera", ParamType::Entity, "Camera entity that gets the shot. Default: the camera track bound in "
          "the Sequencer, else a new root camera 'Shot N'."),
        OptionalParam("newCamera", ParamType::Bool, "True always creates a new camera 'Shot N'; do not give camera then. "
          "Default false."),
        OptionalParam("distance", ParamType::Number, "Metres from the target's body centre along the ground at the first "
          "key (wheel close-ups: ahead of the wheel). The end distance keeps the template's ratio to it."),
        OptionalParam("height", ParamType::Number, "Metres above the target frame origin (the ground under the car)."),
        OptionalParam("side", ParamType::String, "left or right (SideTracking)."),
        OptionalParam("fov", ParamType::Number, "Vertical field of view in degrees.") },
      .refusedWhileCapturing = true,
      .handler = [this, templateNames](const BridgeActionArgs& args, const BridgeReply& reply) {
        Scene& scene = GetScene();
        const std::string templateName = args.GetString("template");
        ShotTemplate shot = ShotTemplate::Chase;
        if (!ShotTemplates::FindByName(templateName, shot))
        {
          reply.Fail(BridgeErrorCode::INVALID_PARAMS, "unknown template '" + templateName + "'; use one of "
            + templateNames);
          return;
        }

        const ShotTemplateInfo& info = ShotTemplates::GetInfo(shot);
        SequencerEditing::ResolveBinding(m_Context);

        ShotTemplateParams params = info.defaults;
        params.start = args.Has("start") ? float(args.GetNumber("start")) : m_Context.sequencerPlayhead;
        if (args.Has("duration"))
          params.duration = float(args.GetNumber("duration"));

        Json ignored = Json::array();
        auto reads = [&info, &ignored, &args](const char* name, uint32_t bit) {
          if (!args.Has(name))
            return false;
          if ((info.params & bit) != 0)
            return true;
          ignored.push_back(name);
          return false;
        };

        if (reads("distance", ShotParam::Distance))
        {
          const float distance = float(args.GetNumber("distance"));
          params.endDistance = params.distance > 0.0f ? params.endDistance * distance / params.distance : distance;
          params.distance = distance;
        }
        if (reads("height", ShotParam::Height))
          params.height = float(args.GetNumber("height"));
        if (reads("fov", ShotParam::Fov))
          params.fovDegrees = float(args.GetNumber("fov"));
        if (reads("side", ShotParam::Side))
        {
          std::string side = args.GetString("side");
          std::transform(side.begin(), side.end(), side.begin(),
            [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
          if (side != "left" && side != "right")
          {
            reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'side' must be left or right");
            return;
          }
          params.side = side == "left" ? ShotSide::Left : ShotSide::Right;
        }

        Entity target = entt::null;
        if (!info.needsTarget)
        {
          if (args.Has("target"))
            ignored.push_back("target");
        }
        else if (args.Has("target"))
        {
          target = args.GetEntity("target");
        }
        else if (SequencerEditing::GetBoundPath(m_Context) != nullptr)
        {
          target = m_Context.sequencerPath;
        }
        else
        {
          std::vector<Entity> paths = CollectEntitiesWith<MotionPathComponent>(scene);
          if (paths.size() != 1)
          {
            reply.Fail(BridgeErrorCode::INVALID_PARAMS, paths.empty()
              ? std::string("the scene has no motion path to follow; give the 'target'")
              : "the scene has " + std::to_string(paths.size()) + " motion paths; give the 'target'");
            return;
          }
          target = paths.front();
        }

        const bool newCamera = args.Has("newCamera") && args.GetBool("newCamera");
        Entity camera = entt::null;
        if (args.Has("camera"))
        {
          if (newCamera)
          {
            reply.Fail(BridgeErrorCode::INVALID_PARAMS, "give either 'camera' or newCamera: true, not both");
            return;
          }
          camera = args.GetEntity("camera");
        }
        else if (!newCamera && SequencerEditing::GetBoundTrack(m_Context) != nullptr
          && scene.HasComponent<CameraComponent>(m_Context.sequencerTrack))
        {
          camera = m_Context.sequencerTrack;
        }

        const EditorCommands::AddShotResult result = SequencerEditing::AddShot(m_Context, shot, params, target, camera);
        if (!result.error.empty())
        {
          reply.Fail(BridgeErrorCode::FAILED, result.error);
          return;
        }

        YA_LOG_INFO("Bridge", "Action sequencer.addShot: %s on '%s' (%u) at %.2f s", info.name,
          EditorCommands::GetEntityDisplayName(scene.GetRegistry(), result.camera).c_str(),
          EntityIdForLog(result.camera), params.start);

        Json out = EntityResult(scene, result.camera);
        out["template"] = info.name;
        out["created"] = camera == entt::null;
        out["firstKey"] = result.firstKey;
        out["keyCount"] = result.keyCount;
        out["start"] = params.start;
        out["end"] = params.start + params.duration;
        out["warning"] = result.warning;
        out["ignored"] = std::move(ignored);
        reply.Ok(std::move(out));
      }
    });

    // The track a key edit works on: the given entity, else the one bound in the Sequencer, else the only one of
    // the scene. It gets bound. False once the reply has failed.
    auto bindEditedTrack = [this](const BridgeActionArgs& args, const BridgeReply& reply, Entity& track) {
      Scene& scene = GetScene();
      SequencerEditing::ResolveBinding(m_Context);

      track = entt::null;
      if (args.Has("entity"))
      {
        track = args.GetEntity("entity");
        if (!scene.HasComponent<CameraTrackComponent>(track))
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "entity " + std::to_string(entt::to_integral(track))
            + " has no 'cameraTrack' component");
          return false;
        }
      }
      else if (SequencerEditing::GetBoundTrack(m_Context) != nullptr)
      {
        track = m_Context.sequencerTrack;
      }
      else
      {
        std::vector<Entity> tracks = CollectEntitiesWith<CameraTrackComponent>(scene);
        if (tracks.size() != 1)
        {
          reply.Fail(tracks.empty() ? BridgeErrorCode::NOT_FOUND : BridgeErrorCode::INVALID_PARAMS,
            tracks.empty() ? std::string("the scene has no camera track")
              : "the scene has " + std::to_string(tracks.size()) + " camera tracks; give the 'entity'");
          return false;
        }
        track = tracks.front();
      }

      if (track != m_Context.sequencerTrack)
      {
        SequencerEditing::BindTrack(m_Context, track);
        // A selected entity with another track would take the binding back on the next frame
        Entity selected = m_Context.selectedEntity;
        if (selected != track && scene.GetRegistry().valid(selected)
          && scene.HasComponent<CameraTrackComponent>(selected))
        {
          m_Context.SelectEntity(track);
        }
      }
      return true;
    };

    using SequencerEditing::RetimePace;
    auto registerRetime = [this, &actions, bindEditedTrack](const char* name, RetimePace pace, const char* summary) {
      actions.Register({
        .name = name,
        .description = std::string(summary) + " Retimes the camera keys strictly between first and last; those two "
          "keep their times and so do Hold segments. Camera travel is the arc length of the evaluated curve: between "
          "two keys relative to the follow target it is measured around the target, elsewhere in the world. The track "
          "gets bound in the Sequencer. Returns {entity, name, first, last, keyTimesBefore, keyTimesAfter, mode, "
          "iterations, settled, shift}: the key times of first..last, mode target, world or mixed (where the travel "
          "was measured), iterations the retime passes run (up to 32; they stop once a further pass would move no "
          "key by 1 ms). settled false means that did not happen: shift is the largest key move of the applied "
          "pass, about what running the action again may still move a key by.",
        .params = {
          OptionalParam("first", ParamType::Integer, "0-based index of the first key of the range. Default: 0 when "
            "last is given, else the key range selected in the Sequencer (Shift+click), else the whole track."),
          OptionalParam("last", ParamType::Integer, "0-based index of the last key, at least first + 2. Default: the "
            "last key when first is given, else as for first."),
          OptionalParam("entity", ParamType::Entity, "Entity with a cameraTrack component. Default: the track bound in "
            "the Sequencer, else the only camera track of the scene.") },
        .refusedWhileCapturing = true,
        .handler = [this, name, pace, bindEditedTrack](const BridgeActionArgs& args, const BridgeReply& reply) {
          Scene& scene = GetScene();
          Entity track = entt::null;
          if (!bindEditedTrack(args, reply, track))
            return;

          int first = -1;
          int last = -1;
          if (args.Has("first") || args.Has("last"))
          {
            const int64_t count = int64_t(scene.GetComponent<CameraTrackComponent>(track).keys.size());
            const int64_t from = args.Has("first") ? args.GetInteger("first") : 0;
            const int64_t to = args.Has("last") ? args.GetInteger("last") : count - 1;
            if (from < 0 || to >= count || to - from < 2)
            {
              reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'first' and 'last' must be key indices in 0.."
                + std::to_string(count - 1) + " with last >= first + 2 (the track has " + std::to_string(count)
                + " keys)");
              return;
            }
            first = int(from);
            last = int(to);
          }

          const SequencerEditing::RetimeResult result = SequencerEditing::RetimeKeys(m_Context, pace, first, last);
          if (!result.error.empty())
          {
            reply.Fail(BridgeErrorCode::FAILED, result.error);
            return;
          }

          YA_LOG_INFO("Bridge", "Action %s: keys %d-%d of '%s' (%u), %s, %d passes", name, result.first, result.last,
            scene.GetName(track).c_str(), EntityIdForLog(track), result.mode, int(result.iterations));

          Json out = EntityResult(scene, track);
          out["first"] = result.first;
          out["last"] = result.last;
          out["keyTimesBefore"] = result.keyTimesBefore;
          out["keyTimesAfter"] = result.keyTimesAfter;
          out["mode"] = result.mode;
          out["iterations"] = result.iterations;
          out["settled"] = result.settled;
          out["shift"] = result.shift;
          reply.Ok(std::move(out));
        }
      });
    };

    registerRetime("sequencer.evenOutSpeed", RetimePace::Steady, "Even Out Speed in the Shot Inspector: the camera "
      "moves at a steady pace between the keys; relative keys are measured around the car, so the car's own braking "
      "is kept.");
    registerRetime("sequencer.matchCarPace", RetimePace::Car, "Match Car Pace in the Shot Inspector: the camera speeds "
      "up and slows down together with the car between the keys - each inner key gets the time at which the car has "
      "covered the same share of its distance between the first and the last key as the camera has of its travel. "
      "The car is the follow target when it drives along a motion path, else the motion path bound in the Sequencer; "
      "fails when there is none or it does not move in the range.");

    // Runs a timing edit over the key range first..last of the track and replies with the times before and after.
    // A range drag rewrites the key times every frame, so an edit during any timeline drag is refused.
    auto editKeyRange = [this, bindEditedTrack](const char* name, const BridgeActionArgs& args, const BridgeReply& reply,
      const auto& edit) {
      if (m_Context.sequencerDragActive)
      {
        reply.Fail(BridgeErrorCode::FAILED, "a Sequencer timeline drag is in progress; try again after it ends");
        return;
      }

      Scene& scene = GetScene();
      Entity track = entt::null;
      if (!bindEditedTrack(args, reply, track))
        return;

      std::vector<CameraTrackKey>& keys = scene.GetComponent<CameraTrackComponent>(track).keys;
      const int64_t count = int64_t(keys.size());
      const int64_t from = args.GetInteger("first");
      const int64_t to = args.GetInteger("last");
      if (from < 0 || to >= count || to <= from)
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'first' and 'last' must be key indices in 0.."
          + std::to_string(count - 1) + " with last > first (the track has " + std::to_string(count) + " keys)");
        return;
      }
      const int first = int(from);
      const int last = int(to);

      std::vector<float> before;
      for (int i = first; i <= last; i++)
        before.push_back(keys[size_t(i)].time);
      const float applied = edit(keys, first, last);
      std::vector<float> after;
      for (int i = first; i <= last; i++)
        after.push_back(keys[size_t(i)].time);
      SequencerEditing::Repose(m_Context);

      YA_LOG_INFO("Bridge", "Action %s: keys %d-%d of '%s' (%u), applied %.3f s", name, first, last,
        scene.GetName(track).c_str(), EntityIdForLog(track), applied);

      Json out = EntityResult(scene, track);
      out["first"] = first;
      out["last"] = last;
      out["keyTimesBefore"] = before;
      out["keyTimesAfter"] = after;
      out["applied"] = applied;
      reply.Ok(std::move(out));
    };

    const char* entityDescription = "Entity with a cameraTrack component. Default: the track bound in the Sequencer, "
      "else the only camera track of the scene.";

    actions.Register({
      .name = "sequencer.moveKeys",
      .description = "Move a camera key range in time, as dragging inside the key range band on the Sequencer's Camera "
        "lane does. The keys keep their spacing and every other key stays where it is, so the range is clamped between "
        "its neighbours (10 ms gap) and never starts below 0. Poses and blend settings are untouched. The track gets "
        "bound in the Sequencer; refused during a timeline drag. Returns {entity, name, first, last, keyTimesBefore, "
        "keyTimesAfter, applied}: the key times of first..last and the delta in seconds applied after clamping.",
      .params = {
        RequiredParam("first", ParamType::Integer, "0-based index of the first key of the range."),
        RequiredParam("last", ParamType::Integer, "0-based index of the last key, greater than first."),
        RequiredParam("delta", ParamType::Number, "Seconds to move the range by; negative moves it earlier."),
        OptionalParam("entity", ParamType::Entity, entityDescription) },
      .refusedWhileCapturing = true,
      .handler = [editKeyRange](const BridgeActionArgs& args, const BridgeReply& reply) {
        const float delta = float(args.GetNumber("delta"));
        editKeyRange("sequencer.moveKeys", args, reply, [delta](std::vector<CameraTrackKey>& keys, int first, int last) {
          return SequencerEditing::MoveKeyRange(keys, first, last, delta);
        });
      }
    });

    actions.Register({
      .name = "sequencer.stretchKeys",
      .description = "Stretch or shrink a camera key range in time, as dragging an edge of the key range band on the "
        "Sequencer's Camera lane does. The inner keys keep their relative spacing, so the camera flies the same path "
        "slower (longer) or faster (shorter). Anchor start (the right edge) keeps the first key and shifts every key "
        "after the range by the change of the last key's time. Anchor end (the left edge) keeps the last key and every "
        "key before the range; the first key stops 10 ms after the key before it (at 0 without one). No two keys of the "
        "range get closer than 10 ms, which limits shrinking. Poses and blend settings are untouched. The track gets "
        "bound in the Sequencer; refused during a timeline drag. Returns {entity, name, first, last, keyTimesBefore, "
        "keyTimesAfter, applied}: the key times of first..last and the duration in seconds applied after clamping.",
      .params = {
        RequiredParam("first", ParamType::Integer, "0-based index of the first key of the range."),
        RequiredParam("last", ParamType::Integer, "0-based index of the last key, greater than first."),
        RequiredParam("duration", ParamType::Number, "Seconds from the first to the last key of the range after the edit."),
        OptionalParam("anchor", ParamType::String, "start (default: the right edge moves, with ripple) or end (the left "
          "edge moves)."),
        OptionalParam("entity", ParamType::Entity, entityDescription) },
      .refusedWhileCapturing = true,
      .handler = [editKeyRange](const BridgeActionArgs& args, const BridgeReply& reply) {
        using KeyRangeTiming::StretchAnchor;
        StretchAnchor anchor = StretchAnchor::Start;
        if (args.Has("anchor"))
        {
          const std::string value = args.GetString("anchor");
          if (value != "start" && value != "end")
          {
            reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'anchor' must be start or end");
            return;
          }
          anchor = value == "end" ? StretchAnchor::End : StretchAnchor::Start;
        }

        const float duration = float(args.GetNumber("duration"));
        editKeyRange("sequencer.stretchKeys", args, reply,
          [duration, anchor](std::vector<CameraTrackKey>& keys, int first, int last) {
            return SequencerEditing::StretchKeyRange(keys, first, last, duration, anchor);
          });
      }
    });

    // The path a speed key edit works on: the given entity, else the one bound in the Sequencer, else the only one of
    // the scene. It gets bound. False once the reply has failed.
    auto bindEditedPath = [this](const BridgeActionArgs& args, const BridgeReply& reply, Entity& path) {
      Scene& scene = GetScene();
      SequencerEditing::ResolveBinding(m_Context);

      path = entt::null;
      if (args.Has("entity"))
      {
        path = args.GetEntity("entity");
        if (!scene.HasComponent<MotionPathComponent>(path))
        {
          reply.Fail(BridgeErrorCode::NOT_FOUND, "entity " + std::to_string(entt::to_integral(path))
            + " has no 'motionPath' component");
          return false;
        }
      }
      else if (SequencerEditing::GetBoundPath(m_Context) != nullptr)
      {
        path = m_Context.sequencerPath;
      }
      else
      {
        std::vector<Entity> paths = CollectEntitiesWith<MotionPathComponent>(scene);
        if (paths.size() != 1)
        {
          reply.Fail(paths.empty() ? BridgeErrorCode::NOT_FOUND : BridgeErrorCode::INVALID_PARAMS,
            paths.empty() ? std::string("the scene has no motion path")
              : "the scene has " + std::to_string(paths.size()) + " motion paths; give the 'entity'");
          return false;
        }
        path = paths.front();
      }

      if (path != m_Context.sequencerPath)
      {
        SequencerEditing::BindPath(m_Context, path);
        Entity selected = m_Context.selectedEntity;
        if (selected != path && scene.GetRegistry().valid(selected)
          && scene.HasComponent<MotionPathComponent>(selected))
        {
          m_Context.SelectEntity(path);
        }
      }
      return true;
    };

    // Runs a timing edit over the speed key range first..last and replies with the times and speeds before and after
    auto editSpeedKeyRange = [this, bindEditedPath](const char* name, const BridgeActionArgs& args,
      const BridgeReply& reply, const auto& edit) {
      if (m_Context.sequencerDragActive)
      {
        reply.Fail(BridgeErrorCode::FAILED, "a Sequencer timeline drag is in progress; try again after it ends");
        return;
      }

      Scene& scene = GetScene();
      Entity pathEntity = entt::null;
      if (!bindEditedPath(args, reply, pathEntity))
        return;

      std::vector<MotionSpeedKey>& keys = scene.GetComponent<MotionPathComponent>(pathEntity).speedKeys;
      const int64_t count = int64_t(keys.size());
      const int64_t from = args.GetInteger("first");
      const int64_t to = args.GetInteger("last");
      if (from < 0 || to >= count || to <= from)
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'first' and 'last' must be speed key indices in 0.."
          + std::to_string(count - 1) + " with last > first (the path has " + std::to_string(count) + " speed keys)");
        return;
      }

      auto collect = [&keys](std::vector<float>& times, std::vector<float>& speeds) {
        for (const MotionSpeedKey& key : keys)
        {
          times.push_back(key.time);
          speeds.push_back(key.speed);
        }
      };
      std::vector<float> timesBefore;
      std::vector<float> speedsBefore;
      collect(timesBefore, speedsBefore);
      const SpeedKeyTiming::Result result = edit(keys, int(from), int(to));
      std::vector<float> timesAfter;
      std::vector<float> speedsAfter;
      collect(timesAfter, speedsAfter);
      SequencerEditing::Repose(m_Context);

      YA_LOG_INFO("Bridge", "Action %s: speed keys %d-%d of '%s' (%u), applied %.3f s, speeds x%.3f", name, int(from),
        int(to), scene.GetName(pathEntity).c_str(), EntityIdForLog(pathEntity), result.applied, result.speedScale);

      Json out = EntityResult(scene, pathEntity);
      out["first"] = from;
      out["last"] = to;
      out["keyTimesBefore"] = timesBefore;
      out["keyTimesAfter"] = timesAfter;
      out["speedsBefore"] = speedsBefore;
      out["speedsAfter"] = speedsAfter;
      out["applied"] = result.applied;
      out["speedScale"] = result.speedScale;
      reply.Ok(std::move(out));
    };

    const char* pathDescription = "Entity with a motionPath component. Default: the path bound in the Sequencer, else "
      "the only motion path of the scene.";
    const char* speedResult = "Returns {entity, name, first, last, keyTimesBefore, keyTimesAfter, speedsBefore, "
      "speedsAfter, applied, speedScale}: every speed key's time and speed, the value applied after clamping and the "
      "factor the rescaled speeds got.";

    actions.Register({
      .name = "sequencer.moveSpeedKeys",
      .description = std::string("Move a speed key range in time, as dragging inside the speed key range band on the "
        "Sequencer's Speed lane does. The range is clamped between its neighbours (10 ms gap) and never starts below "
        "0; its speeds are rescaled so the drive covers the same road from the key before the range to the key after "
        "it, and a move they cannot absorb is shortened. The path gets bound in the Sequencer; refused during a "
        "timeline drag. ") + speedResult,
      .params = {
        RequiredParam("first", ParamType::Integer, "0-based index of the first speed key of the range."),
        RequiredParam("last", ParamType::Integer, "0-based index of the last speed key, greater than first."),
        RequiredParam("delta", ParamType::Number, "Seconds to move the range by; negative moves it earlier."),
        OptionalParam("entity", ParamType::Entity, pathDescription) },
      .refusedWhileCapturing = true,
      .handler = [editSpeedKeyRange](const BridgeActionArgs& args, const BridgeReply& reply) {
        const float delta = float(args.GetNumber("delta"));
        editSpeedKeyRange("sequencer.moveSpeedKeys", args, reply,
          [delta](std::vector<MotionSpeedKey>& keys, int first, int last) {
            return SequencerEditing::MoveSpeedKeyRange(keys, first, last, delta);
          });
      }
    });

    actions.Register({
      .name = "sequencer.stretchSpeedKeys",
      .description = std::string("Slow down or speed up a stretch of the drive, as dragging an edge of the speed key "
        "range band on the Sequencer's Speed lane does: the range is scaled in time and its speeds rescaled so the car "
        "drives the same road, and the drive outside plays as before, only shifted. Anchor start (the right edge) keeps "
        "the drive up to the range's first key and shifts every later key by the change of the last key's time; anchor "
        "end (the left edge) keeps the drive from the range's last key on, and the first key stops 10 ms after the key "
        "before it. Speeds stay within 5% of their old value and 100 m/s, which can shorten the stretch. The path gets "
        "bound in the Sequencer; refused during a timeline drag. ") + speedResult,
      .params = {
        RequiredParam("first", ParamType::Integer, "0-based index of the first speed key of the range."),
        RequiredParam("last", ParamType::Integer, "0-based index of the last speed key, greater than first."),
        RequiredParam("duration", ParamType::Number, "Seconds from the first to the last key of the range after the edit."),
        OptionalParam("anchor", ParamType::String, "start (default: the right edge moves, with ripple) or end (the left "
          "edge moves)."),
        OptionalParam("entity", ParamType::Entity, pathDescription) },
      .refusedWhileCapturing = true,
      .handler = [editSpeedKeyRange](const BridgeActionArgs& args, const BridgeReply& reply) {
        using KeyRangeTiming::StretchAnchor;
        StretchAnchor anchor = StretchAnchor::Start;
        if (args.Has("anchor"))
        {
          const std::string value = args.GetString("anchor");
          if (value != "start" && value != "end")
          {
            reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'anchor' must be start or end");
            return;
          }
          anchor = value == "end" ? StretchAnchor::End : StretchAnchor::Start;
        }

        const float duration = float(args.GetNumber("duration"));
        editSpeedKeyRange("sequencer.stretchSpeedKeys", args, reply,
          [duration, anchor](std::vector<MotionSpeedKey>& keys, int first, int last) {
            return SequencerEditing::StretchSpeedKeyRange(keys, first, last, duration, anchor);
          });
      }
    });
  }
}
