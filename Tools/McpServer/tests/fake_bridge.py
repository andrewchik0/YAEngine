"""In-process stand-in for the editor side of bridge protocol v1."""

import asyncio
import contextlib
import json
import os
import secrets
import sys
from datetime import datetime, timezone
from pathlib import Path

from synthetic_capture import write_shot

MAX_MESSAGE_BYTES = 4 * 1024 * 1024
LEVELS = ("verbose", "info", "warning", "error")
# exePath of bridges that stand in for another process (a made-up pid)
FAKE_EXE_PATH = "C:\\fake\\cmake-build-debugeditor\\RacingDemo\\RacingDemo.exe"


def own_image_path() -> str:
    """This process's executable as Windows reports it; under a venv that is the base interpreter."""
    try:
        from yaengine_mcp.discovery import probe_process
    except ImportError:
        # The fake engine subprocess runs the base interpreter directly, without the package.
        return sys.executable
    return probe_process(os.getpid()).image or sys.executable


async def wait_disconnected(client, timeout: float = 5.0) -> None:
    """Returns once a BridgeClient has noticed that the other side closed its connection."""
    task = client._reader_task
    if task is not None:
        await asyncio.wait_for(asyncio.shield(task), timeout)


class Gate:
    """Holds back a test.deferred reply until the test opens it."""

    def __init__(self):
        self.received = asyncio.Event()
        self.opened = asyncio.Event()
        self.replied = asyncio.Event()


def sample_entities() -> list:
    return [
        {"id": 1, "name": "Sun", "parent": None, "components": ["transform", "light"]},
        {"id": 2, "name": "Car body", "parent": None, "components": ["transform", "model"]},
        {"id": 4294967299, "name": 'wheel "front"', "parent": 2, "components": ["transform", "mesh", "material"]},
        {"id": 7, "name": "EditorCamera", "parent": None, "components": ["transform", "camera"], "editorOnly": True},
    ]


def sample_components() -> dict:
    return {
        (1, "transform"): {"position": [0.0, 10.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]},
        (1, "light"): {"type": "directional", "color": [1.0, 0.9, 0.8], "intensity": 3.0},
        (2, "transform"): {"position": [5.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]},
    }


def sample_settings() -> dict:
    return {"gamma": 2.2, "exposure": 1.0, "ao": True, "aoRadius": 1.5, "bloom": True, "fogColor": [0.5, 0.6, 0.7]}


def sample_actions() -> list:
    return [
        {"name": "scene.save", "description": "Save the open scene.", "params": [
            {"name": "path", "type": "path", "required": False, "description": "Scene file to write."}]},
        {"name": "selection.set", "description": "Select an entity.", "params": [
            {"name": "entity", "type": "entity", "required": True, "description": "Entity id from scene.entities."}]},
        {"name": "shaders.recompileAll", "description": "Recompile every shader.", "params": []},
    ]


def sample_passes() -> list:
    return [
        {"name": "GBufferPass", "altName": None, "executionIndex": 1, "colorOutputs": ["gbuffer0", "gbuffer1"],
         "storageOutputs": [], "depthOutput": "mainDepth", "enabled": True},
        {"name": "GTAOPass", "altName": "SSGIPass", "executionIndex": 5, "colorOutputs": ["gtaoWorkingAO"],
         "storageOutputs": ["gtaoEdges"], "depthOutput": None, "enabled": False},
        {"name": "LightCull", "altName": None, "executionIndex": 9, "colorOutputs": [], "storageOutputs": [],
         "depthOutput": None, "enabled": True},
    ]


def _merge(base, patch):
    """The engine's rule: mappings merge recursively, anything else replaces."""
    if not isinstance(base, dict) or not isinstance(patch, dict):
        return patch
    merged = dict(base)
    for key, value in patch.items():
        merged[key] = _merge(base[key], value) if key in base else value
    return merged


def _dump_yaml(value) -> str:
    # Imported here: the fake engine subprocess imports this module on an interpreter without PyYAML.
    import yaml

    return yaml.safe_dump(value, sort_keys=False, default_flow_style=None)


def sample_log() -> list:
    return [
        {
            "seq": seq,
            "level": LEVELS[seq % len(LEVELS)],
            "tag": "Render",
            "file": "C:\\work\\YAEngine\\Core\\Source\\Render\\Render.cpp",
            "line": 100 + seq,
            "text": f"line {seq}",
        }
        for seq in range(1, 31)
    ]


class FakeBridge:
    def __init__(self, *, pid=None, token=None, build_config="DebugEditor", scene_path="Assets/Scenes/test.yaml",
                 exe_path=None, started_at=None):
        self.pid = os.getpid() if pid is None else pid
        self.token = token or secrets.token_hex(16)
        self.build_config = build_config
        self.scene_path = scene_path
        self.exe_path = exe_path or (own_image_path() if pid is None else FAKE_EXE_PATH)
        # None writes the time of write_discovery
        self.started_at = started_at
        self.protocol_version = 1
        self.log = sample_log()
        self.port = 0
        self.ignore_quit = False
        # engine.quit: "reply" answers first, "close" closes the socket without answering, "none" never answers
        self.quit_reply = "reply"
        self.on_quit = None
        self.quit_requested = asyncio.Event()
        self.requests = []
        # capture.shot writes synthetic shots here; None answers 'failed'
        self.capture_root = None
        self.capture_busy = False
        # replaces the manifest.json of the shots capture.shot writes
        self.shot_manifest_text = None
        self.shots_taken = 0
        self.passes = sample_passes()
        self.entities = sample_entities()
        self.components = sample_components()
        self.settings = sample_settings()
        self.actions = sample_actions()
        self.selection = None
        # ui.screenshot writes its PNGs here; None answers 'failed'
        self.ui_root = None
        self.screenshot_size = (320, 200)
        self.screenshot_noise = False
        self.screenshots_taken = 0
        self._server = None
        self._writers = set()
        self._tasks = set()
        self._gates = {}

    def gate(self, name: str) -> Gate:
        if name not in self._gates:
            self._gates[name] = Gate()
        return self._gates[name]

    async def start(self) -> "FakeBridge":
        self._server = await asyncio.start_server(self._handle, "127.0.0.1", 0, limit=MAX_MESSAGE_BYTES)
        self.port = self._server.sockets[0].getsockname()[1]
        return self

    async def stop(self) -> None:
        await self.drop_connections()
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
        for task in list(self._tasks):
            task.cancel()

    async def drop_connections(self) -> None:
        writers = list(self._writers)
        for writer in writers:
            writer.close()
        for writer in writers:
            with contextlib.suppress(Exception):
                await writer.wait_closed()

    def hellos(self) -> list:
        return [request for request in self.requests if request.get("method") == "hello"]

    def write_discovery(self, directory: Path) -> Path:
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        payload = {
            "protocolVersion": 1,
            "pid": self.pid,
            "port": self.port,
            "token": self.token,
            "exePath": self.exe_path,
            "buildConfig": self.build_config,
            "repoRoot": "C:\\fake",
            "scenePath": self.scene_path,
            "startedAt": self.started_at or datetime.now(timezone.utc).isoformat(),
        }
        path = directory / f"{self.pid}.json"
        temporary = directory / f"{self.pid}.json.tmp"
        temporary.write_text(json.dumps(payload), encoding="utf-8")
        os.replace(temporary, path)
        return path

    async def _handle(self, reader, writer) -> None:
        self._writers.add(writer)
        authorized = False
        try:
            while True:
                try:
                    line = await reader.readline()
                except (ValueError, OSError):
                    break
                if not line:
                    break
                request = json.loads(line)
                self.requests.append(request)
                request_id, method, params = request.get("id"), request.get("method"), request.get("params") or {}
                if not authorized:
                    if method != "hello" or params.get("token") != self.token:
                        await self._error(writer, request_id, "unauthorized", "hello with a valid token is required")
                        break
                    authorized = True
                    await self._reply(writer, request_id, {
                        "protocolVersion": self.protocol_version,
                        "pid": self.pid,
                        "buildConfig": self.build_config,
                        "scenePath": self.scene_path,
                        "engine": "YAEngine",
                    })
                    continue
                handler = getattr(self, "_m_" + str(method).replace(".", "_"), None)
                if handler is None:
                    await self._error(writer, request_id, "unknown_method", f"no method {method}")
                    continue
                await handler(writer, request_id, params)
        finally:
            self._writers.discard(writer)
            writer.close()

    async def _send(self, writer, message: dict) -> None:
        if writer.is_closing():
            return
        writer.write(json.dumps(message).encode("utf-8") + b"\n")
        with contextlib.suppress(ConnectionError):
            await writer.drain()

    async def _reply(self, writer, request_id, result) -> None:
        await self._send(writer, {"id": request_id, "result": result})

    async def _error(self, writer, request_id, code: str, message: str) -> None:
        await self._send(writer, {"id": request_id, "error": {"code": code, "message": message}})

    async def _m_bridge_ping(self, writer, request_id, params) -> None:
        await self._reply(writer, request_id, {})

    async def _m_engine_status(self, writer, request_id, params) -> None:
        await self._reply(writer, request_id, {
            "pid": self.pid,
            "buildConfig": self.build_config,
            "scenePath": self.scene_path,
            "frameIndex": 4242,
            "fps": 60.0,
            "frameTimeMs": 16.67,
            "renderExtent": [1280, 720],
            "outputExtent": [2560, 1440],
            "clients": len(self._writers),
            "captureBusy": self.capture_busy,
        })

    async def _m_log_tail(self, writer, request_id, params) -> None:
        count = params.get("count", 200)
        min_level = params.get("minLevel", "info")
        if not isinstance(count, int) or not 1 <= count <= 2000 or min_level not in LEVELS:
            await self._error(writer, request_id, "invalid_params", "count must be 1-2000 and minLevel a known level")
            return
        after = params.get("afterSeq")
        threshold = LEVELS.index(min_level)
        lines = [
            entry for entry in self.log
            if LEVELS.index(entry["level"]) >= threshold and (after is None or entry["seq"] > after)
        ]
        await self._reply(writer, request_id, {"lines": lines[-count:], "lastSeq": self.log[-1]["seq"] if self.log else 0})

    async def _m_engine_quit(self, writer, request_id, params) -> None:
        if self.quit_reply == "reply":
            await self._reply(writer, request_id, {})
        if self.ignore_quit:
            return
        self.quit_requested.set()
        if self.on_quit is not None:
            self.on_quit()
        if self.quit_reply != "none":
            await self.drop_connections()

    async def _m_capture_targets(self, writer, request_id, params) -> None:
        await self._reply(writer, request_id, {
            "targets": [
                {"name": "gbuffer0", "format": "R8G8B8A8_UNORM", "extent": [1280, 720], "resolution": "render", "managed": True},
                {"name": "sceneColor", "format": "R8G8B8A8_UNORM", "extent": [1600, 900], "resolution": "output", "managed": True},
                {"name": "swapchain", "format": "UNSUPPORTED", "extent": [1600, 900], "resolution": "output", "managed": False},
            ],
            "aliases": [
                {"name": "final", "resolvesTo": ["sceneColor"]},
                {"name": "pt_noisy", "resolvesTo": []},
                {"name": "default", "resolvesTo": ["sceneColor", "dlssOutput"]},
            ],
            "debugViews": [{"id": 0, "slug": "off", "name": "Off"}, {"id": 4, "slug": "normals", "name": "Normals"}],
            "passes": self.passes,
        })

    async def _m_capture_shot(self, writer, request_id, params) -> None:
        shot = params.get("shot")
        if not isinstance(shot, str):
            await self._error(writer, request_id, "invalid_params", "'shot' must be a string")
            return
        if self.capture_busy:
            await self._error(writer, request_id, "busy", "another capture shot is still running")
            return
        name = params.get("name") or "shot"
        index = self.shots_taken
        self.shots_taken += 1
        if self.capture_root is None:
            await self._reply(writer, request_id, {"directory": f"C:\\nowhere\\{index:03d}_{name}", "status": "failed",
                                                   "warnings": ["unknown target 'nosuch'"]})
            return
        after_pass = next((pair.split("=", 1)[1] for pair in shot.split(";") if pair.startswith("after=")), None)
        directory = write_shot(Path(self.capture_root) / f"{index:03d}_{name}", name=name, index=index,
                               final_size=(1600, 900), requested_by=shot, after_pass=after_pass)
        if self.shot_manifest_text is not None:
            (directory / "manifest.json").write_text(self.shot_manifest_text, encoding="utf-8")
        await self._reply(writer, request_id, {"directory": str(directory), "status": "ok", "warnings": []})

    async def _m_scene_entities(self, writer, request_id, params) -> None:
        name_filter = params.get("filter", "")
        max_count = params.get("maxCount", 2000)
        include_editor_only = params.get("includeEditorOnly", False)
        if not isinstance(name_filter, str) or not isinstance(max_count, int) or max_count < 1 \
                or not isinstance(include_editor_only, bool):
            await self._error(writer, request_id, "invalid_params", "bad scene.entities params")
            return
        matching = [
            {key: entity[key] for key in ("id", "name", "parent", "components")}
            for entity in self.entities
            if (include_editor_only or not entity.get("editorOnly")) and name_filter.lower() in entity["name"].lower()
        ]
        await self._reply(writer, request_id, {"entities": matching[:max_count], "truncated": len(matching) > max_count})

    async def _component_target(self, writer, request_id, params):
        entity, component = params.get("entity"), params.get("component")
        if not isinstance(entity, int) or not isinstance(component, str):
            await self._error(writer, request_id, "invalid_params", "'entity' and 'component' are required")
            return None
        if not any(item["id"] == entity for item in self.entities):
            await self._error(writer, request_id, "not_found", f"no entity with id {entity}")
            return None
        if (entity, component) not in self.components:
            await self._error(writer, request_id, "not_found", f"entity {entity} has no '{component}' component")
            return None
        return entity, component

    async def _m_scene_componentGet(self, writer, request_id, params) -> None:
        target = await self._component_target(writer, request_id, params)
        if target is not None:
            await self._reply(writer, request_id, {"yaml": _dump_yaml(self.components[target])})

    async def _m_scene_componentPatch(self, writer, request_id, params) -> None:
        patch = await self._yaml_patch(writer, request_id, params)
        if patch is None:
            return
        target = await self._component_target(writer, request_id, params)
        if target is not None:
            self.components[target] = _merge(self.components[target], patch)
            await self._reply(writer, request_id, {"yaml": _dump_yaml(self.components[target])})

    async def _m_render_settingsGet(self, writer, request_id, params) -> None:
        await self._reply(writer, request_id, {"yaml": _dump_yaml(self.settings)})

    async def _m_render_settingsPatch(self, writer, request_id, params) -> None:
        patch = await self._yaml_patch(writer, request_id, params)
        if patch is None:
            return
        unknown = sorted(set(patch) - set(self.settings))
        if unknown:
            await self._error(writer, request_id, "invalid_params", "unknown render settings: " + ", ".join(unknown))
            return
        self.settings = _merge(self.settings, patch)
        await self._reply(writer, request_id, {"yaml": _dump_yaml(self.settings)})

    async def _yaml_patch(self, writer, request_id, params):
        import yaml

        text = params.get("yaml")
        if not isinstance(text, str):
            await self._error(writer, request_id, "invalid_params", "'yaml' must be a string holding a YAML mapping")
            return None
        try:
            patch = yaml.safe_load(text)
        except yaml.YAMLError as exc:
            await self._error(writer, request_id, "invalid_params", f"cannot parse 'yaml': {exc}")
            return None
        patch = {} if patch is None else patch
        if not isinstance(patch, dict):
            await self._error(writer, request_id, "invalid_params", "'yaml' must be a YAML mapping of the keys to change")
            return None
        return patch

    async def _m_actions_list(self, writer, request_id, params) -> None:
        await self._reply(writer, request_id, {"actions": self.actions})

    async def _m_actions_run(self, writer, request_id, params) -> None:
        name, run_params = params.get("name"), params.get("params") or {}
        action = next((item for item in self.actions if item["name"] == name), None)
        if action is None:
            await self._error(writer, request_id, "not_found", f"no action '{name}'; actions.list names every action")
            return
        declared = {param["name"]: param for param in action["params"]}
        unknown = sorted(set(run_params) - set(declared))
        if unknown:
            await self._error(writer, request_id, "invalid_params", f"'{name}' has no param '{unknown[0]}'")
            return
        missing = [key for key, param in declared.items() if param["required"] and key not in run_params]
        if missing:
            await self._error(writer, request_id, "invalid_params", f"'{name}' needs the param '{missing[0]}'")
            return

        if name == "selection.set":
            entity = run_params["entity"]
            match = next((item for item in self.entities if item["id"] == entity), None)
            if match is None:
                await self._error(writer, request_id, "not_found", f"no entity with id {entity}")
                return
            self.selection = entity
            await self._reply(writer, request_id, {"entity": entity, "name": match["name"]})
        elif name == "scene.save":
            self.scene_path = run_params.get("path", self.scene_path)
            await self._reply(writer, request_id, {"scenePath": self.scene_path})
        else:
            # shaders.recompileAll answers in a later frame, like the engine
            async def later():
                await asyncio.sleep(0.05)
                await self._reply(writer, request_id, {"shaderCount": 42})

            task = asyncio.create_task(later())
            self._tasks.add(task)
            task.add_done_callback(self._tasks.discard)

    async def _m_ui_windows(self, writer, request_id, params) -> None:
        await self._reply(writer, request_id, {"windows": [
            {"name": "Render Settings", "visible": True, "focused": True, "collapsed": False, "docked": True},
            {"name": "Details", "visible": False, "focused": False, "collapsed": False, "docked": True},
            {"name": "##MainMenuBar", "visible": True, "focused": False, "collapsed": False, "docked": False},
        ]})

    async def _m_ui_tree(self, writer, request_id, params) -> None:
        window = params.get("window")
        if not isinstance(window, str) or not window:
            await self._error(writer, request_id, "invalid_params", "'window' must name a window as ui.windows lists it")
            return
        if window != "Render Settings":
            await self._error(writer, request_id, "not_found", f"no window '{window}'; ui.windows lists the windows")
            return

        def flags(disabled=False, checked=None, opened=None):
            return {"disabled": disabled, "checked": checked, "open": opened}

        await self._reply(writer, request_id, {"items": [
            {"path": "Render Settings/ Display", "label": " Display", "type": "header", "flags": flags(opened=True)},
            {"path": "Render Settings/Exposure", "label": "Exposure", "type": "input", "value": "1.000", "flags": flags()},
            {"path": "Render Settings/Debug View", "label": "Debug View", "type": "combo", "value": "Off", "flags": flags()},
            {"path": "Render Settings/SSR", "label": "SSR", "type": "checkbox", "value": True, "flags": flags(checked=True)},
            {"path": "Render Settings/AO Strength", "label": "AO Strength", "type": "input", "value": "1.000",
             "flags": flags(disabled=True)},
            {"path": None, "label": "a label cut short at thirty-one", "type": "item", "flags": flags()},
        ]})

    async def _m_ui_do(self, writer, request_id, params) -> None:
        path, action = params.get("path"), params.get("action")
        actions = ("click", "check", "uncheck", "set", "open", "close", "select", "menu")
        if not isinstance(path, str) or not path or action not in actions:
            await self._error(writer, request_id, "invalid_params", "'path' and a known 'action' are required")
            return
        if path.endswith("..."):
            await self._reply(writer, request_id, {"ok": False, "detail": (
                "'Load Skybox...' opens a native file dialog, which blocks the editor until someone closes it; "
                "run the action skybox.set with a path instead (actions.run)")})
        elif path == "Render Settings/Missing":
            await self._reply(writer, request_id, {
                "ok": False, "detail": "no item at 'Render Settings/Missing'; ui.tree lists the paths of a window"})
        else:
            await self._reply(writer, request_id, {"ok": True, "detail": f"{action} '{path}'"})

    async def _m_ui_screenshot(self, writer, request_id, params) -> None:
        if self.ui_root is None:
            await self._error(writer, request_id, "failed", "the window surface does not allow copying presented frames")
            return
        # Imported here: the fake engine subprocess imports this module on an interpreter without Pillow.
        from PIL import Image

        name = (params.get("window") or "editor").replace(" ", "_")
        path = Path(self.ui_root) / f"{self.screenshots_taken:03d}_{name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        width, height = self.screenshot_size
        if self.screenshot_noise:
            import numpy as np

            pixels = np.random.default_rng(3).integers(0, 256, (height, width, 3), dtype=np.uint8)
            Image.fromarray(pixels).save(path)
        else:
            Image.new("RGB", (width, height), (40, 80, 120)).save(path)
        self.screenshots_taken += 1
        await self._reply(writer, request_id, {"path": str(path)})

    async def _m_test_deferred(self, writer, request_id, params) -> None:
        gate = self.gate(params["gate"]) if "gate" in params else None
        if gate is not None:
            gate.received.set()

        async def later():
            if gate is None:
                await asyncio.sleep(params.get("delayMs", 0) / 1000.0)
            else:
                await gate.opened.wait()
            await self._reply(writer, request_id, {"value": params.get("value")})
            if gate is not None:
                gate.replied.set()

        task = asyncio.create_task(later())
        self._tasks.add(task)
        task.add_done_callback(self._tasks.discard)

    async def _m_test_error(self, writer, request_id, params) -> None:
        await self._error(writer, request_id, params["code"], params.get("message", ""))

    async def _m_test_event(self, writer, request_id, params) -> None:
        await self._send(writer, {"event": "test.unknownEvent", "params": {"x": 1}})
        await self._reply(writer, request_id, {})

    async def _m_test_oversized(self, writer, request_id, params) -> None:
        await self._reply(writer, request_id, {"blob": "x" * (MAX_MESSAGE_BYTES + 16)})
