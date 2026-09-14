import pytest
import yaml
from mcp.server.mcpserver.exceptions import ToolError

from yaengine_mcp.formatting import MAX_TEXT_CHARS
from yaengine_mcp.server import create_server
from yaengine_mcp.tools import group_names

SCENE_TOOLS = {"scene_entities", "component_get", "component_patch", "render_settings_get", "render_settings_patch"}


def text(result):
    items = [item.text for item in result.content if item.type == "text"]
    assert len(items) == 1
    return items[0]


@pytest.fixture
def attached(tmp_path, fake_bridge, make_manager):
    fake_bridge.write_discovery(tmp_path / "Bridge")
    return create_server(make_manager())


async def test_scene_tools_are_registered(make_manager):
    assert "scene" in group_names()
    tools = {tool.name: tool for tool in await create_server(make_manager()).list_tools()}
    assert SCENE_TOOLS <= tools.keys()
    assert set(tools["scene_entities"].input_schema["properties"]) == {"filter", "include_editor_only", "max_count"}
    max_count = tools["scene_entities"].input_schema["properties"]["max_count"]
    assert (max_count["default"], max_count["maximum"]) == (500, 5000)
    assert "required" not in tools["scene_entities"].input_schema
    assert tools["component_get"].input_schema["required"] == ["entity", "component"]
    assert tools["component_patch"].input_schema["required"] == ["entity", "component", "yaml"]
    assert "properties" not in tools["render_settings_get"].input_schema or not tools["render_settings_get"].input_schema["properties"]
    assert tools["render_settings_patch"].input_schema["required"] == ["yaml"]


async def test_scene_entities_lists_one_line_per_entity(attached, fake_bridge):
    lines = text(await attached.call_tool("scene_entities", {})).splitlines()

    assert fake_bridge.requests[-1]["params"] == {"includeEditorOnly": False, "maxCount": 500}
    assert lines == [
        "3 entities (id name parent [components])",
        '1 "Sun" parent=- [transform, light]',
        '2 "Car body" parent=- [transform, model]',
        '4294967299 "wheel \\"front\\"" parent=2 [transform, mesh, material]',
    ]


async def test_scene_entities_passes_filter_and_reports_truncation(attached, fake_bridge):
    lines = text(await attached.call_tool(
        "scene_entities", {"filter": "a", "include_editor_only": True, "max_count": 1})).splitlines()

    assert fake_bridge.requests[-1]["params"] == {"includeEditorOnly": True, "maxCount": 1, "filter": "a"}
    assert lines == [
        "1 entities (id name parent [components]); truncated: more entities match, narrow the filter or raise max_count",
        '2 "Car body" parent=- [transform, model]',
    ]


async def test_scene_entities_text_is_capped(attached, fake_bridge):
    fake_bridge.entities = [
        {"id": index, "name": f"rock {index:04d} " + "x" * 200, "parent": None, "components": ["transform", "mesh"]}
        for index in range(1000)
    ]

    result = text(await attached.call_tool("scene_entities", {"max_count": 5000}))

    lines = result.splitlines()
    assert len(result) <= MAX_TEXT_CHARS
    assert lines[0] == "1000 entities (id name parent [components])"
    assert lines[1].startswith('0 "rock 0000 ')
    assert lines[-1].startswith("truncated: ") and "narrow the filter" in lines[-1]
    assert int(lines[-1].split()[1]) == 1000 - (len(lines) - 2)


async def test_scene_entities_refuses_max_count_over_5000(attached):
    with pytest.raises(ToolError):
        await attached.call_tool("scene_entities", {"max_count": 5001})


async def test_component_get_returns_the_yaml(attached, fake_bridge):
    result = text(await attached.call_tool("component_get", {"entity": 1, "component": "light"}))

    assert fake_bridge.requests[-1]["params"] == {"entity": 1, "component": "light"}
    assert yaml.safe_load(result) == {"type": "directional", "color": [1.0, 0.9, 0.8], "intensity": 3.0}


async def test_component_patch_sends_the_yaml_text_and_returns_the_state_after(attached, fake_bridge):
    patch = "intensity: 10\ncolor: [1, 0, 0]\n"
    result = text(await attached.call_tool("component_patch", {"entity": 1, "component": "light", "yaml": patch}))

    assert fake_bridge.requests[-1]["method"] == "scene.componentPatch"
    assert fake_bridge.requests[-1]["params"] == {"entity": 1, "component": "light", "yaml": patch}
    assert yaml.safe_load(result) == {"type": "directional", "color": [1, 0, 0], "intensity": 10}


@pytest.mark.parametrize(
    "arguments, message",
    [
        ({"entity": 99, "component": "light", "yaml": "intensity: 1"}, "not_found .*no entity with id 99"),
        ({"entity": 1, "component": "terrain", "yaml": "size: 1"}, "not_found .*has no 'terrain' component"),
        ({"entity": 1, "component": "light", "yaml": "intensity: [1"}, "invalid_params .*cannot parse 'yaml'"),
        ({"entity": 1, "component": "light", "yaml": "- 1"}, "invalid_params .*must be a YAML mapping"),
    ],
)
async def test_component_patch_errors_become_tool_errors(attached, arguments, message):
    with pytest.raises(ToolError, match=message):
        await attached.call_tool("component_patch", arguments)


async def test_render_settings_get_and_patch(attached, fake_bridge):
    before = yaml.safe_load(text(await attached.call_tool("render_settings_get", {})))
    assert fake_bridge.requests[-1]["method"] == "render.settingsGet"
    assert before["exposure"] == 1.0

    after = yaml.safe_load(text(await attached.call_tool("render_settings_patch", {"yaml": "exposure: 2.5\nbloom: false"})))
    assert fake_bridge.requests[-1]["params"] == {"yaml": "exposure: 2.5\nbloom: false"}
    assert after == before | {"exposure": 2.5, "bloom": False}


async def test_render_settings_patch_rejection_is_a_tool_error(attached):
    with pytest.raises(ToolError, match="invalid_params .*unknown render settings: nosuch"):
        await attached.call_tool("render_settings_patch", {"yaml": "nosuch: 1"})
