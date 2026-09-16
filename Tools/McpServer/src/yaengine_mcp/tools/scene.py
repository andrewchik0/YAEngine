"""Scene data tools: list entities, read and patch components and render settings as YAML."""

import json
from typing import Annotated, Optional

from mcp.server.mcpserver import MCPServer
from pydantic import Field

from ..formatting import capped_lines
from ..instances import InstanceManager
from . import tool

DEFAULT_ENTITY_COUNT = 500
MAX_ENTITY_COUNT = 5000

PATCH_RULES = (
    "The patch is a partial YAML mapping merged into the current state: nested mappings merge key by key, "
    "sequences (vectors, colors, lists) and scalars replace the old value. Changes live in the editor's memory "
    "only; nothing is saved to the scene file."
)


def format_entities(result: dict) -> str:
    entities = result.get("entities") or []
    header = f"{len(entities)} entities (id name parent [components])"
    if result.get("truncated"):
        header += "; truncated: more entities match, narrow the filter or raise max_count"
    lines = []
    for entity in entities:
        parent = entity.get("parent")
        lines.append("%s %s parent=%s [%s]" % (
            entity.get("id"), json.dumps(entity.get("name", ""), ensure_ascii=False),
            "-" if parent is None else parent, ", ".join(entity.get("components") or [])))
    return capped_lines([header], lines, "narrow the filter or lower max_count")


def register(server: MCPServer, manager: InstanceManager) -> None:
    @tool(
        server,
        description=(
            "List the entities of the scene open in the attached YAEngine editor, depth first in hierarchy order. "
            "One line per entity: id, name (JSON quoted), parent id ('-' for a root) and the names of its "
            "components as component_get and component_patch accept them: 'transform' plus the scene file keys "
            "(camera, light, mesh, material, model, reflectionProbe, irradianceVolume, terrain, road, scatter, "
            "collider, ...). Ids stay valid only while the editor keeps the scene open."
        ),
    )
    async def scene_entities(
        filter: Annotated[
            Optional[str], Field(description="Only entities whose name contains this text (case-insensitive).")
        ] = None,
        include_editor_only: Annotated[
            bool, Field(description="Also list editor-only entities such as the editor camera.")
        ] = False,
        max_count: Annotated[
            int, Field(ge=1, le=MAX_ENTITY_COUNT, description="Maximum number of entities to return.")
        ] = DEFAULT_ENTITY_COUNT,
    ) -> str:
        params = {"includeEditorOnly": include_editor_only, "maxCount": max_count}
        if filter:
            params["filter"] = filter
        return format_entities(await manager.request("scene.entities", params))

    @tool(
        server,
        description=(
            "Return one component of an entity as YAML, in the shape the scene file stores it. 'transform' has "
            "position [x,y,z], rotation as a quaternion [x,y,z,w] and scale [x,y,z], local to the parent. Angles in "
            "other components (light cones, camera fov) are radians. '~' means the component has nothing the scene "
            "file can store (a mesh imported from a model file)."
        ),
    )
    async def component_get(
        entity: Annotated[int, Field(description="Entity id from scene_entities.")],
        component: Annotated[str, Field(description="Component name from scene_entities, e.g. transform or light.")],
    ) -> str:
        result = await manager.request("scene.componentGet", {"entity": entity, "component": component})
        return result.get("yaml", "")

    @tool(
        server,
        description=(
            "Change fields of one component of an entity and return the component as YAML afterwards. "
            + PATCH_RULES
            + " Only components the entity already has can be patched. Editing follows the editor's Details panel: "
            "a transform marks the entity moved, terrain, road and scatter regenerate, a model node shows as "
            "overridden. A material patch gives the entity a new material built from the YAML, and is refused when "
            "the material uses textures embedded in a model file; a model's path cannot change. Keys a component "
            "does not know are ignored, so check the returned YAML. Game code can keep driving a component, e.g. a "
            "vehicle rewrites its own transform every frame, so re-read it after a frame when that matters."
        ),
    )
    async def component_patch(
        entity: Annotated[int, Field(description="Entity id from scene_entities.")],
        component: Annotated[str, Field(description="Component name from scene_entities, e.g. transform or light.")],
        yaml: Annotated[
            str, Field(description="Partial YAML mapping, e.g. 'intensity: 20' or 'position: [0, 1.5, 3]'.")
        ],
    ) -> str:
        result = await manager.request(
            "scene.componentPatch", {"entity": entity, "component": component, "yaml": yaml}
        )
        return result.get("yaml", "")

    @tool(
        server,
        description=(
            "Return the render settings of the attached editor as YAML: the 'settings' block of the scene file "
            "(skybox, exposure, tonemapMode, dither, ao*, ssr*, ssgi*, antialiasing, renderPath, pt*, shadows*, bloom*, "
            "fog*, autoExposure, probe and volume settings). Editor-only toggles such as the debug view are not "
            "part of it."
        ),
    )
    async def render_settings_get() -> str:
        result = await manager.request("render.settingsGet")
        return result.get("yaml", "")

    @tool(
        server,
        description=(
            "Change render settings of the attached editor and return all render settings as YAML afterwards. "
            + PATCH_RULES
            + " Keys must be ones render_settings_get returns (plus skybox, a path relative to the app directory). "
            "Enum values are numbers: antialiasing 0 none, 1 TAA, then the DLSS modes; renderPath 0 raster, "
            "1 path tracing; tonemapMode 0 ACES, 1 AgX. A value that does not convert leaves every setting as it was."
        ),
    )
    async def render_settings_patch(
        yaml: Annotated[str, Field(description="Partial YAML mapping, e.g. 'exposure: 1.5\\nbloom: false'.")],
    ) -> str:
        result = await manager.request("render.settingsPatch", {"yaml": yaml})
        return result.get("yaml", "")
