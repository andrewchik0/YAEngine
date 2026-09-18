"""Editor actions: the named operations of the editor's menus and panels, listed and run generically."""

from typing import Annotated, Any, Optional

from mcp.server.mcpserver import MCPServer
from pydantic import Field

from ..formatting import to_json
from ..instances import EngineError, InstanceManager
from . import tool

# Bakes and model imports run on the editor's main thread and can take minutes.
DEFAULT_RUN_TIMEOUT = 600.0
MAX_RUN_TIMEOUT = 3600.0


def format_actions(result: dict) -> str:
    actions = result.get("actions") or []
    lines = [f"{len(actions)} actions; run one with editor_run(name, params)"]
    for action in actions:
        params = action.get("params") or []
        signature = ", ".join(
            f"{param.get('name')}{'' if param.get('required') else '?'}: {param.get('type')}" for param in params
        )
        lines.append(f"{action.get('name')}({signature}) - {action.get('description', '')}")
        lines.extend(f"    {param.get('name')}: {param.get('description', '')}" for param in params)
    return "\n".join(lines)


def register(server: MCPServer, manager: InstanceManager) -> None:
    @tool(
        server,
        description=(
            "List the actions the attached YAEngine editor can run with editor_run: the operations of its menus and "
            "panels, such as selecting, creating, duplicating, renaming and deleting entities, importing models, "
            "moving the editor camera, debug views and gizmos, opening and saving scenes, the skybox, reflection "
            "probe and irradiance volume bakes, camera track playback and shader recompiles. One line per action, "
            "'name(param: type, optional_param?: type) - description', then one indented line per param. Param "
            "types: entity (an id from scene_entities), string, number, integer, bool, vec3 ([x, y, z]) and path "
            "(absolute, or relative to the editor's asset base path)."
        ),
    )
    async def editor_actions() -> str:
        return format_actions(await manager.request("actions.list"))

    @tool(
        server,
        description=(
            "Run one editor action by name (see editor_actions) and return its result as JSON. An action behaves "
            "like the menu item or button it names and runs on the editor's main thread: scene.open and scene.new "
            "answer once the scene is in place, shaders.recompileAll once the batch is done, and bakes or model "
            "imports stall the editor until they finish, so raise timeout_seconds for a large bake. Errors: "
            "invalid_params (a missing, unknown or mistyped param), not_found (unknown action, entity or file), "
            "busy (a capture shot or a shader batch is running, or the editor window is minimized for scene.new, "
            "scene.open and shaders.recompileAll), failed (the operation itself failed; engine_log "
            "has the details). Nothing is written to disk unless the action says so (scene.save, bakes)."
        ),
    )
    async def editor_run(
        name: Annotated[str, Field(description="Action name from editor_actions, e.g. selection.set.")],
        params: Annotated[
            Optional[dict[str, Any]],
            Field(description='The action\'s params by name, e.g. {"entity": 12} or {"position": [0, 2, 5]}.'),
        ] = None,
        timeout_seconds: Annotated[
            float, Field(gt=0, le=MAX_RUN_TIMEOUT, description="How long to wait for the action to finish.")
        ] = DEFAULT_RUN_TIMEOUT,
    ) -> str:
        result = await manager.request("actions.run", {"name": name, "params": params or {}}, timeout=timeout_seconds)
        return to_json(result)

    @tool(
        server,
        description=(
            "Run several editor operations in one frame, so the viewport never shows a half-done change: a light is "
            "created and moved into place before anything is drawn. Each step is {\"action\": name, \"params\": "
            "{...}} for an action from editor_actions, or {\"method\": name, \"params\": {...}} for scene.entities, "
            "scene.componentGet, scene.componentPatch (params entity, component, yaml), render.settingsGet or "
            "render.settingsPatch (params yaml). A param value {\"$ref\": \"<step>.<key>\"} is replaced by that key "
            "of an earlier step's result, e.g. {\"$ref\": \"0.entity\"} for the entity step 0 created; keys chain "
            "with dots and array indices. Steps run in order and the batch stops at the first failing one, whose "
            "error is raised together with the results of the steps before it, which stay applied: there is no "
            "rollback. Actions that answer in a later frame (scene.new, scene.open, shaders.recompileAll) are "
            "refused; bakes and model imports are allowed and stall the editor as usual. Returns the result of "
            "every step as a JSON array."
        ),
    )
    async def editor_batch(
        steps: Annotated[
            list[dict[str, Any]],
            Field(
                min_length=1,
                max_length=256,
                description=(
                    'Steps in order, e.g. [{"action": "entity.create", "params": {"type": "pointLight"}}, '
                    '{"method": "scene.componentPatch", "params": {"entity": {"$ref": "0.entity"}, '
                    '"component": "transform", "yaml": "position: [0, 2, 0]"}}].'
                ),
            ),
        ],
        timeout_seconds: Annotated[
            float, Field(gt=0, le=MAX_RUN_TIMEOUT, description="How long to wait for the whole batch.")
        ] = DEFAULT_RUN_TIMEOUT,
    ) -> str:
        result = await manager.request("batch.run", {"steps": steps}, timeout=timeout_seconds)
        if not result.get("ok"):
            error = result.get("error") or {}
            raise EngineError(
                f"batch step {result.get('failedStep')} failed: {error.get('code')} {error.get('message')}; "
                f"steps before it were applied, results: {to_json(result.get('steps') or [])}"
            )
        return to_json(result.get("steps") or [])
