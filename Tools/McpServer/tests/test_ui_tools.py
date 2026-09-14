import base64
import io

import pytest
from mcp.server.mcpserver.exceptions import ToolError
from PIL import Image

from yaengine_mcp.formatting import MAX_TEXT_CHARS
from yaengine_mcp.images import MAX_BASE64_CHARS
from yaengine_mcp.server import create_server
from yaengine_mcp.tools import group_names
from yaengine_mcp.tools.ui import UI_TIMEOUT, format_tree

UI_TOOLS = {"ui_windows", "ui_tree", "ui_do", "ui_screenshot"}


def texts(result):
    return [item.text for item in result.content if item.type == "text"]


def images(result):
    decoded = []
    for item in result.content:
        if item.type == "image":
            assert item.mime_type == "image/png"
            decoded.append(Image.open(io.BytesIO(base64.b64decode(item.data))))
    return decoded


@pytest.fixture
def attached(tmp_path, fake_bridge, make_manager):
    fake_bridge.ui_root = tmp_path / "Captures" / "mcp" / "ui"
    fake_bridge.write_discovery(tmp_path / "Bridge")
    return create_server(make_manager())


async def test_ui_tools_are_registered(make_manager):
    assert "ui" in group_names()
    tools = {tool.name: tool for tool in await create_server(make_manager()).list_tools()}
    assert UI_TOOLS <= tools.keys()
    assert not tools["ui_windows"].input_schema.get("properties")
    assert set(tools["ui_tree"].input_schema["properties"]) == {"window", "max_depth"}
    assert tools["ui_tree"].input_schema["required"] == ["window"]
    assert set(tools["ui_do"].input_schema["properties"]) == {"path", "action", "value"}
    assert tools["ui_do"].input_schema["required"] == ["path", "action"]
    assert set(tools["ui_screenshot"].input_schema["properties"]) == {"window", "max_size"}
    assert tools["ui_screenshot"].input_schema["properties"]["max_size"]["maximum"] == 2048
    assert "required" not in tools["ui_screenshot"].input_schema


async def test_ui_windows_lists_one_line_per_window(attached, fake_bridge):
    lines = texts(await attached.call_tool("ui_windows", {}))[0].splitlines()

    assert fake_bridge.requests[-1]["method"] == "ui.windows"
    assert lines == [
        "3 windows (name, then visible or hidden, focused, collapsed, docked)",
        '"Render Settings" visible focused docked',
        '"Details" hidden docked',
        '"##MainMenuBar" visible',
    ]


async def test_ui_tree_lists_types_paths_values_and_flags(attached, fake_bridge):
    lines = texts(await attached.call_tool("ui_tree", {"window": "Render Settings", "max_depth": 2}))[0].splitlines()

    assert fake_bridge.requests[-1]["method"] == "ui.tree"
    assert fake_bridge.requests[-1]["params"] == {"window": "Render Settings", "maxDepth": 2}
    assert lines == [
        '6 items in "Render Settings" (type path [= value] [flags])',
        'header "Render Settings/ Display" [open]',
        'input "Render Settings/Exposure" = "1.000"',
        'combo "Render Settings/Debug View" = "Off"',
        'checkbox "Render Settings/SSR" [checked]',
        'input "Render Settings/AO Strength" = "1.000" [disabled]',
        'item (no path; label "a label cut short at thirty-one")',
    ]


async def test_ui_tree_without_max_depth_sends_only_the_window(attached, fake_bridge):
    await attached.call_tool("ui_tree", {"window": "Render Settings"})
    assert fake_bridge.requests[-1]["params"] == {"window": "Render Settings"}


async def test_ui_tree_of_an_unknown_window_is_a_tool_error(attached):
    with pytest.raises(ToolError, match="not_found .*no window 'Nope'"):
        await attached.call_tool("ui_tree", {"window": "Nope"})


async def test_ui_do_returns_the_detail_and_sends_value_only_when_given(attached, fake_bridge):
    detail = texts(await attached.call_tool("ui_do", {"path": "Render Settings/SSR", "action": "uncheck"}))[0]
    assert detail == "uncheck 'Render Settings/SSR'"
    assert fake_bridge.requests[-1]["params"] == {"path": "Render Settings/SSR", "action": "uncheck"}

    await attached.call_tool("ui_do", {"path": "Render Settings/Exposure", "action": "set", "value": 1.5})
    assert fake_bridge.requests[-1]["params"] == {"path": "Render Settings/Exposure", "action": "set", "value": 1.5}

    await attached.call_tool("ui_do", {"path": "Outliner/**/Ground", "action": "click", "value": "right"})
    assert fake_bridge.requests[-1]["params"]["value"] == "right"


@pytest.mark.parametrize(
    "arguments, message",
    [
        ({"path": "Render Settings/ Load Skybox...", "action": "click"}, "did not complete: .*skybox.set"),
        ({"path": "Render Settings/Missing", "action": "click"}, "did not complete: no item at"),
    ],
)
async def test_ui_do_that_did_not_complete_is_a_tool_error(attached, arguments, message):
    with pytest.raises(ToolError, match=message):
        await attached.call_tool("ui_do", arguments)


async def test_ui_do_rejects_an_unknown_action(attached):
    with pytest.raises(ToolError):
        await attached.call_tool("ui_do", {"path": "Render Settings/SSR", "action": "poke"})


async def test_ui_screenshot_returns_the_image_and_states_path_and_size(attached, fake_bridge):
    result = await attached.call_tool("ui_screenshot", {"window": "Render Settings", "max_size": 64})

    assert fake_bridge.requests[-1]["params"] == {"window": "Render Settings"}
    path = fake_bridge.ui_root / "000_Render_Settings.png"
    assert texts(result) == [f"{path} (320x200, shown at 64x40)"]
    shown = images(result)
    assert len(shown) == 1 and shown[0].size == (64, 40)


async def test_ui_screenshot_caps_the_encoded_size(attached, fake_bridge):
    # Noise defeats PNG compression: at 2048 px it would be about 16.8 MB of base64.
    fake_bridge.screenshot_size = (2048, 2048)
    fake_bridge.screenshot_noise = True

    result = await attached.call_tool("ui_screenshot", {"max_size": 2048})

    [image] = [item for item in result.content if item.type == "image"]
    assert len(image.data) <= MAX_BASE64_CHARS
    assert image.mime_type == "image/jpeg"
    assert "(2048x2048, shown at 1024x1024 as JPEG quality" in texts(result)[0]


async def test_ui_screenshot_refuses_sizes_over_2048(attached):
    with pytest.raises(ToolError):
        await attached.call_tool("ui_screenshot", {"max_size": 4096})


def test_ui_tree_text_is_capped():
    items = [{"path": f"Render Settings/{index:04d} " + "z" * 300, "type": "button", "flags": {}} for index in range(500)]

    text = format_tree("Render Settings", {"items": items})

    lines = text.splitlines()
    assert len(text) <= MAX_TEXT_CHARS
    assert lines[0] == '500 items in "Render Settings" (type path [= value] [flags])'
    assert lines[1].startswith('button "Render Settings/0000 ')
    assert lines[-1].startswith("truncated: ") and "max_depth" in lines[-1]


async def test_ui_screenshot_of_the_whole_editor_sends_no_window(attached, fake_bridge):
    await attached.call_tool("ui_screenshot", {})
    assert fake_bridge.requests[-1]["params"] == {}


async def test_ui_screenshot_failure_is_a_tool_error(attached, fake_bridge):
    fake_bridge.ui_root = None
    with pytest.raises(ToolError, match="failed .*copying presented frames"):
        await attached.call_tool("ui_screenshot", {})


async def test_ui_requests_use_the_ui_timeout(tmp_path, fake_bridge, make_manager):
    fake_bridge.ui_root = tmp_path / "ui"
    fake_bridge.write_discovery(tmp_path / "Bridge")
    manager = make_manager()
    seen = []
    request = manager.request

    async def spy(method, params=None, timeout=None):
        seen.append((method, timeout))
        return await request(method, params, timeout)

    manager.request = spy
    server = create_server(manager)
    await server.call_tool("ui_windows", {})
    await server.call_tool("ui_tree", {"window": "Render Settings"})
    await server.call_tool("ui_do", {"path": "Render Settings/SSR", "action": "check"})
    await server.call_tool("ui_screenshot", {})

    assert seen == [("ui.windows", None), ("ui.tree", UI_TIMEOUT), ("ui.do", UI_TIMEOUT), ("ui.screenshot", UI_TIMEOUT)]
