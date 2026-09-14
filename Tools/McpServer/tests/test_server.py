import json
import os
import queue
import subprocess
import sys
import threading
from pathlib import Path

import pytest
from mcp.server.mcpserver.exceptions import ToolError

from synthetic_capture import write_shot
from yaengine_mcp.server import create_server
from yaengine_mcp.tools import group_names

ENGINE_TOOLS = {
    "engine_list_instances",
    "engine_attach",
    "engine_launch",
    "engine_stop",
    "engine_restart",
    "engine_status",
    "engine_log",
}


def test_tool_groups_are_discovered():
    assert "engine" in group_names()


async def test_engine_tools_are_registered(make_manager):
    server = create_server(make_manager())
    tools = {tool.name: tool for tool in await server.list_tools()}

    assert ENGINE_TOOLS <= tools.keys()
    assert all(tools[name].description for name in ENGINE_TOOLS)
    assert tools["engine_attach"].input_schema["required"] == ["pid"]
    assert set(tools["engine_log"].input_schema["properties"]) == {"count", "min_level"}
    assert set(tools["engine_launch"].input_schema["properties"]) == {"config", "extra_args"}
    assert tools["engine_status"].output_schema is None


async def test_status_and_log_tools_use_the_bridge(tmp_path, make_manager, fake_bridge):
    fake_bridge.write_discovery(tmp_path / "Bridge")
    server = create_server(make_manager())

    status = await server.call_tool("engine_status", {})
    assert not status.is_error
    assert json.loads(status.content[0].text)["pid"] == fake_bridge.pid

    log = await server.call_tool("engine_log", {"count": 2, "min_level": "error"})
    lines = log.content[0].text.splitlines()
    assert lines == [
        "lastSeq=30 lines=2",
        "23 error [Render] line 23 (Render.cpp:123)",
        "27 error [Render] line 27 (Render.cpp:127)",
    ]


async def test_engine_failures_become_tool_errors(make_manager):
    server = create_server(make_manager())
    with pytest.raises(ToolError, match="No running YAEngine editor"):
        await server.call_tool("engine_status", {})


def test_stdio_session_carries_only_json_rpc(tmp_path):
    script = Path(sys.executable).with_name("yaengine-mcp.exe" if sys.platform == "win32" else "yaengine-mcp")
    if not script.is_file():
        pytest.skip(f"console script not installed at {script}")

    shot = write_shot(tmp_path / "cap" / "000_Straße", name="Straße", requested_by="name=Straße;view=normals",
                      warnings=["unknown target 'Ωmega'"])
    env = dict(os.environ, LOCALAPPDATA=str(tmp_path / "LocalAppData"))
    process = subprocess.Popen(
        [str(script)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env
    )
    stdout_lines = queue.Queue()

    def pump_stdout():
        for line in process.stdout:
            stdout_lines.put(line)
        stdout_lines.put(None)

    threading.Thread(target=pump_stdout, daemon=True).start()
    threading.Thread(target=process.stderr.read, daemon=True).start()
    received = []

    def send(message):
        process.stdin.write(json.dumps(message).encode("utf-8") + b"\n")
        process.stdin.flush()

    def reply(request_id):
        while True:
            line = stdout_lines.get(timeout=60)
            assert line is not None, "server closed stdout before replying"
            message = json.loads(line)
            received.append(message)
            if message.get("id") == request_id:
                return message

    def call(request_id, name, arguments):
        send({"jsonrpc": "2.0", "id": request_id, "method": "tools/call", "params": {"name": name, "arguments": arguments}})
        return reply(request_id)["result"]

    try:
        send({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "pytest", "version": "0"}},
        })
        assert reply(1)["result"]["serverInfo"]["name"] == "yaengine"
        send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        send({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        assert ENGINE_TOOLS <= {tool["name"] for tool in reply(2)["result"]["tools"]}

        status = call(3, "engine_status", {})
        assert status["isError"] and "No running YAEngine editor" in status["content"][0]["text"]

        stats = call(4, "capture_stats", {"shot_dir": str(shot), "target": "resolved"})
        assert not stats.get("isError") and stats["content"][0]["text"].startswith("resolved  R16G16B16A16_SFLOAT")

        view = call(5, "capture_view", {"shot_dir": str(shot), "target": "final", "max_size": 32})
        assert not view.get("isError") and [item["type"] for item in view["content"]] == ["text", "image"]
        assert "000_Straße" in view["content"][0]["text"]

        missing = call(6, "capture_stats", {"shot_dir": str(tmp_path / "missing")})
        assert missing["isError"] and "no manifest.json in" in missing["content"][0]["text"]

        process.stdin.close()
        assert process.wait(timeout=60) == 0
        while (line := stdout_lines.get(timeout=10)) is not None:
            received.append(json.loads(line))
    finally:
        if process.poll() is None:
            process.kill()

    assert all(message.get("jsonrpc") == "2.0" for message in received)
