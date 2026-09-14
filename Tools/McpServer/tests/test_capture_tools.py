import base64
import functools
import io
import os

import pytest
from mcp.server.mcpserver.exceptions import ToolError
from PIL import Image

from synthetic_capture import write_capture, write_shot
from yaengine_mcp import images as image_encoding
from yaengine_mcp.server import create_server
from yaengine_mcp.tools import capture as capture_tools
from yaengine_mcp.tools import group_names

CAPTURE_TOOLS = {"capture_targets", "capture_shot", "capture_view", "capture_stats", "capture_diff"}


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
def capture(tmp_path):
    return write_capture(tmp_path / "cap")


@pytest.fixture
def attached(tmp_path, fake_bridge, make_manager):
    fake_bridge.capture_root = tmp_path / "Captures" / "mcp"
    fake_bridge.write_discovery(tmp_path / "Bridge")
    return create_server(make_manager())


async def test_capture_tools_are_registered(make_manager):
    assert "capture" in group_names()
    tools = {tool.name: tool for tool in await create_server(make_manager()).list_tools()}
    assert CAPTURE_TOOLS <= tools.keys()
    assert set(tools["capture_shot"].input_schema["properties"]) == {"shot", "name"}
    assert set(tools["capture_view"].input_schema["properties"]) == {"shot_dir", "target", "map", "exposure", "rect", "max_size"}
    assert tools["capture_view"].input_schema["required"] == ["shot_dir"]
    assert tools["capture_view"].input_schema["properties"]["max_size"]["maximum"] == 2048
    assert set(tools["capture_stats"].input_schema["properties"]) == {"shot_dir", "target", "rect"}
    assert "captureBusy false" in tools["capture_shot"].description
    assert tools["capture_diff"].input_schema["required"] == ["shot_a", "shot_b"]


async def test_capture_targets_lists_targets_aliases_and_views(attached):
    text = texts(await attached.call_tool("capture_targets", {}))[0].splitlines()
    assert "  sceneColor R8G8B8A8_UNORM 1600x900 output" in text
    assert "  swapchain UNSUPPORTED 1600x900 output imported" in text
    assert "  default -> sceneColor,dlssOutput" in text
    assert "  pt_noisy -> (nothing in this state)" in text
    assert "  4 normals (Normals)" in text
    assert text[-4:] == [
        "passes in execution order (after=<name or alt name> in a shot captures right after one):",
        "  1 GBufferPass -> gbuffer0,gbuffer1,depth:mainDepth",
        "  5 GTAOPass [SSGIPass] -> gtaoWorkingAO,gtaoEdges (disabled now)",
        "  9 LightCull -> (no image outputs)",
    ]


async def test_capture_targets_without_passes(attached, fake_bridge):
    fake_bridge.passes = []
    text = texts(await attached.call_tool("capture_targets", {}))[0].splitlines()
    assert text[-1] == "passes: (none)"


async def test_capture_shot_returns_stats_and_a_downscaled_final(attached, fake_bridge):
    result = await attached.call_tool("capture_shot", {"shot": "view=normals;exposure=2", "name": "normals"})

    request = fake_bridge.requests[-1]
    assert request["method"] == "capture.shot"
    assert request["params"] == {"shot": "view=normals;exposure=2", "name": "normals"}

    lines = texts(result)[0].splitlines()
    directory = str(fake_bridge.capture_root / "000_normals")
    assert lines[0] == f"status=ok directory={directory}"
    assert lines[1].startswith("shot normals: path=Raster aa=TAA view=Off exposure=1")
    assert lines[3].startswith("  final R8G8B8A8_UNORM 1600x900 ldr_display min=[0,0,0.502,1]")
    assert lines[4].startswith("  resolved R16G16B16A16_SFLOAT 64x36 linear_hdr") and "nan=1 inf=1" in lines[4]
    assert lines[-1] == f"image final: {os.path.join(directory, 'final.png')} (1600x900, shown at 1280x720)"

    shown = images(result)
    assert len(shown) == 1 and shown[0].size == (1280, 720) and shown[0].mode == "RGB"


async def test_capture_shot_names_the_pass_it_was_captured_after(attached, fake_bridge):
    result = await attached.call_tool("capture_shot", {"shot": "after=GBufferPass;targets=gbuffer0", "name": "gbuffer"})
    lines = texts(result)[0].splitlines()
    assert lines[1].startswith("shot gbuffer: path=Raster") and lines[1].endswith(" frames=1 after=GBufferPass")


async def test_capture_shot_without_a_manifest_reports_the_failure(attached, fake_bridge):
    fake_bridge.capture_root = None
    result = await attached.call_tool("capture_shot", {"shot": "targets=nosuch"})
    assert texts(result)[0].splitlines() == [
        "status=failed directory=C:\\nowhere\\000_shot",
        "warning: unknown target 'nosuch'",
        "no manifest was written, so there is nothing to show",
    ]
    assert images(result) == []


@pytest.mark.parametrize(
    "manifest, message",
    [
        ("{not json", "cannot read the capture: Expecting property name"),
        ('{"shot": {"name": "bad"}}', "the capture files lack the expected entry 'targets'"),
        ("", "cannot read the capture: Expecting value"),
    ],
)
async def test_capture_shot_keeps_status_and_directory_when_the_analysis_fails(attached, fake_bridge, manifest, message):
    fake_bridge.shot_manifest_text = manifest
    result = await attached.call_tool("capture_shot", {"shot": "", "name": "bad"})

    lines = texts(result)[0].splitlines()
    assert lines[0] == f"status=ok directory={fake_bridge.capture_root / '000_bad'}"
    assert len(lines) == 2 and lines[1].startswith("cannot analyse the shot: ") and message in lines[1]
    assert images(result) == []


async def test_capture_tools_read_utf8_manifests(attached, fake_bridge):
    result = await attached.call_tool("capture_shot", {"shot": "view=normals;exposure=2", "name": "Straße"})
    lines = texts(result)[0].splitlines()
    directory = fake_bridge.capture_root / "000_Straße"
    assert lines[0] == f"status=ok directory={directory}"
    assert lines[1].startswith("shot Straße: path=Raster")
    assert len(images(result)) == 1

    stats = texts(await attached.call_tool("capture_stats", {"shot_dir": str(directory), "target": "resolved"}))[0]
    assert stats.startswith("resolved  R16G16B16A16_SFLOAT  64x36")
    view = await attached.call_tool("capture_view", {"shot_dir": str(directory), "target": "final", "max_size": 64})
    assert [image.size for image in images(view)] == [(64, 36)]


async def test_capture_view_caps_the_encoded_image(tmp_path, make_manager, monkeypatch):
    # Below the PNG of this view even at the PNG floor, above the fixed overhead of a JPEG.
    budget = 4000
    monkeypatch.setattr(capture_tools, "encode_for_client",
                        functools.partial(image_encoding.encode_for_client, max_base64=budget))
    shot = write_shot(tmp_path / "000_big", name="big", final_size=(1600, 900))
    server = create_server(make_manager())

    result = await server.call_tool("capture_view", {"shot_dir": str(shot), "target": "final", "map": "linear"})

    [image] = [item for item in result.content if item.type == "image"]
    assert image.mime_type == "image/jpeg" and len(image.data) <= budget
    text = texts(result)[0]
    assert "(1600x900, shown at " in text and "as JPEG quality 60)" in text
    with Image.open(os.path.join(str(shot), "final.linear.preview.png")) as written:
        assert written.size == (1600, 900)


async def test_capture_shot_while_busy_is_a_tool_error(attached, fake_bridge):
    fake_bridge.capture_busy = True
    with pytest.raises(ToolError, match="busy"):
        await attached.call_tool("capture_shot", {"shot": ""})


async def test_capture_view_writes_the_png_it_returns(capture, make_manager):
    server = create_server(make_manager())
    result = await server.call_tool(
        "capture_view", {"shot_dir": str(capture["raster"]), "target": "resolved", "map": "log", "rect": "8,4,32,16", "max_size": 16}
    )

    path = os.path.join(str(capture["raster"]), "resolved.log.rect_8_4_32_16.preview.png")
    assert texts(result) == [f"resolved log exposure=1 rect=8,4,32,16 -> {path} (32x16, shown at 16x8)"]
    with Image.open(path) as written:
        assert written.size == (32, 16)
    assert [image.size for image in images(result)] == [(16, 8)]


async def test_capture_stats_and_diff_use_the_analysis_script(capture, make_manager):
    server = create_server(make_manager())

    stats = texts(await server.call_tool("capture_stats", {"shot_dir": str(capture["raster"]), "target": "resolved", "rect": "0,0,2,1"}))[0]
    assert stats.splitlines()[:2] == ["resolved rect=0,0,2,1  R16G16B16A16_SFLOAT  64x36  linear_hdr", "  pixels=2  NaN=1  Inf=1"]

    diff = texts(await server.call_tool("capture_diff", {"shot_a": str(capture["raster"]), "shot_b": str(capture["bright"])}))[0]
    assert diff.splitlines()[:3] == ["settings that differ (raster vs bright):", "  exposure               1.0  ->  2.0", "image diff on 'final':"]


@pytest.mark.parametrize(
    "name, arguments, message",
    [
        ("capture_stats", {"target": "nosuch"}, "no target 'nosuch'"),
        ("capture_view", {"rect": "1,2"}, "rect must be four integers"),
        ("capture_view", {"rect": "0,0,999,1"}, "does not fit in 64x36"),
        ("capture_diff", {"shot_b": "missing"}, "no manifest.json in missing"),
    ],
)
async def test_analysis_problems_become_tool_errors(capture, make_manager, name, arguments, message):
    server = create_server(make_manager())
    defaults = {"shot_dir": str(capture["raster"]), "shot_a": str(capture["raster"])}
    required = {"capture_stats": ["shot_dir"], "capture_view": ["shot_dir"], "capture_diff": ["shot_a"]}[name]
    call = {key: defaults[key] for key in required} | arguments
    with pytest.raises(ToolError, match=message):
        await server.call_tool(name, call)


@pytest.mark.parametrize(
    "content, message",
    [
        (b"\xff\xfe{", "cannot read the capture: 'utf-8' codec can't decode"),
        (b'{"shot": {"name": "x"}}', "the capture files lack the expected entry 'targets'"),
    ],
)
async def test_unreadable_manifests_become_tool_errors(tmp_path, make_manager, content, message):
    shot = write_shot(tmp_path / "000_bad", name="bad")
    (shot / "manifest.json").write_bytes(content)
    server = create_server(make_manager())
    for name in ("capture_stats", "capture_view"):
        with pytest.raises(ToolError, match=message):
            await server.call_tool(name, {"shot_dir": str(shot)})
