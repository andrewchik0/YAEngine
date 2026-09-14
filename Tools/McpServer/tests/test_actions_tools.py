import json

import pytest
from mcp.server.mcpserver.exceptions import ToolError

from yaengine_mcp.server import create_server
from yaengine_mcp.tools import group_names
from yaengine_mcp.tools.actions import DEFAULT_RUN_TIMEOUT


def text(result):
    items = [item.text for item in result.content if item.type == "text"]
    assert len(items) == 1
    return items[0]


@pytest.fixture
def attached(tmp_path, fake_bridge, make_manager):
    fake_bridge.write_discovery(tmp_path / "Bridge")
    return create_server(make_manager())


async def test_action_tools_are_registered(make_manager):
    assert "actions" in group_names()
    tools = {tool.name: tool for tool in await create_server(make_manager()).list_tools()}
    assert not tools["editor_actions"].input_schema.get("properties")
    assert set(tools["editor_run"].input_schema["properties"]) == {"name", "params", "timeout_seconds"}
    assert tools["editor_run"].input_schema["required"] == ["name"]


async def test_editor_actions_lists_signatures_and_params(attached, fake_bridge):
    lines = text(await attached.call_tool("editor_actions", {})).splitlines()

    assert fake_bridge.requests[-1]["method"] == "actions.list"
    assert lines == [
        "3 actions; run one with editor_run(name, params)",
        "scene.save(path?: path) - Save the open scene.",
        "    path: Scene file to write.",
        "selection.set(entity: entity) - Select an entity.",
        "    entity: Entity id from scene.entities.",
        "shaders.recompileAll() - Recompile every shader.",
    ]


async def test_editor_run_sends_the_name_and_params_and_returns_the_result(attached, fake_bridge):
    result = json.loads(text(await attached.call_tool(
        "editor_run", {"name": "selection.set", "params": {"entity": 2}})))

    assert fake_bridge.requests[-1]["method"] == "actions.run"
    assert fake_bridge.requests[-1]["params"] == {"name": "selection.set", "params": {"entity": 2}}
    assert result == {"entity": 2, "name": "Car body"}


async def test_editor_run_without_params_sends_an_empty_object(attached, fake_bridge):
    result = json.loads(text(await attached.call_tool("editor_run", {"name": "scene.save"})))

    assert fake_bridge.requests[-1]["params"] == {"name": "scene.save", "params": {}}
    assert result == {"scenePath": fake_bridge.scene_path}


async def test_editor_run_waits_for_a_deferred_action(attached):
    result = json.loads(text(await attached.call_tool("editor_run", {"name": "shaders.recompileAll"})))

    assert result == {"shaderCount": 42}


@pytest.mark.parametrize(
    "arguments, message",
    [
        ({"name": "nosuch.action"}, "not_found .*no action 'nosuch.action'"),
        ({"name": "selection.set"}, "invalid_params .*needs the param 'entity'"),
        ({"name": "selection.set", "params": {"entity": 99}}, "not_found .*no entity with id 99"),
        ({"name": "scene.save", "params": {"file": "x"}}, "invalid_params .*has no param 'file'"),
    ],
)
async def test_editor_run_errors_become_tool_errors(attached, arguments, message):
    with pytest.raises(ToolError, match=message):
        await attached.call_tool("editor_run", arguments)


async def test_editor_run_passes_its_timeout_to_the_request(tmp_path, fake_bridge, make_manager):
    fake_bridge.write_discovery(tmp_path / "Bridge")
    manager = make_manager()
    seen = []
    request = manager.request

    async def spy(method, params=None, timeout=None):
        seen.append((method, timeout))
        return await request(method, params, timeout)

    manager.request = spy
    server = create_server(manager)
    await server.call_tool("editor_run", {"name": "scene.save"})
    await server.call_tool("editor_run", {"name": "scene.save", "timeout_seconds": 5})

    assert seen == [("actions.run", DEFAULT_RUN_TIMEOUT), ("actions.run", 5)]
