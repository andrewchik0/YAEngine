"""Editor instances: selection, attach, launch, graceful stop, restart."""

import asyncio
import datetime
import itertools
import logging
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Optional, Sequence

from . import bridge, discovery, paths
from .bridge import BridgeClient, BridgeError
from .formatting import to_json

log = logging.getLogger(__name__)

LAUNCH_TIMEOUT = 180.0
STOP_TIMEOUT = 30.0
POLL_INTERVAL = 0.25
LOG_TAIL_LINES = 40
LOG_TAIL_BYTES = 64 * 1024

# The editor gets its own hidden console: its output goes to the log file, and console control
# events aimed at this server (Ctrl+C, closing the terminal) do not reach it.
_CREATION_FLAGS = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
_ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


class EngineError(Exception):
    """A failure whose message is written for the MCP client."""


@dataclass
class LaunchedEditor:
    process: subprocess.Popen
    config: str
    extra_args: list
    log_path: Path
    launched_at: datetime.datetime

    @property
    def pid(self) -> int:
        return self.process.pid

    @property
    def running(self) -> bool:
        return self.process.poll() is None


def taskkill_command(pid: int) -> list:
    # No /F: without it taskkill posts WM_CLOSE, the same path as closing the editor window.
    return ["taskkill", "/PID", str(pid)]


def run_taskkill(pid: int) -> str:
    result = subprocess.run(taskkill_command(pid), stdin=subprocess.DEVNULL, capture_output=True)
    output = result.stdout + result.stderr
    return output.decode("oem" if sys.platform == "win32" else "utf-8", errors="replace").strip()


def default_command(exe: Path, args: list) -> list:
    return [str(exe), *args]


class InstanceManager:
    """Owns the connection to the attached editor and the editors this server launched."""

    def __init__(
        self,
        *,
        repo_root: Path = paths.REPO_ROOT,
        discovery_dir: Optional[Path] = None,
        logs_dir: Optional[Path] = None,
        launch_timeout: float = LAUNCH_TIMEOUT,
        stop_timeout: float = STOP_TIMEOUT,
        request_timeout: float = bridge.DEFAULT_TIMEOUT,
        probe: Callable[[int], discovery.ProcessInfo] = discovery.probe_process,
        taskkill: Callable[[int], str] = run_taskkill,
        build_command: Callable[[Path, list], list] = default_command,
    ):
        self.repo_root = Path(repo_root)
        self._discovery_dir = discovery_dir
        self._logs_dir = logs_dir
        self._launch_timeout = launch_timeout
        self._stop_timeout = stop_timeout
        self._request_timeout = request_timeout
        self._probe = probe
        self._taskkill = taskkill
        self._build_command = build_command

        self._client: Optional[BridgeClient] = None
        self._attached: Optional[discovery.Instance] = None
        self._explicit = False
        self._launched: dict[int, LaunchedEditor] = {}
        self._state_lock = asyncio.Lock()
        self._lifecycle_lock = asyncio.Lock()

    @property
    def discovery_dir(self) -> Path:
        return paths.discovery_dir() if self._discovery_dir is None else Path(self._discovery_dir)

    @property
    def logs_dir(self) -> Path:
        return paths.launch_logs_dir() if self._logs_dir is None else Path(self._logs_dir)

    @property
    def attached_pid(self) -> Optional[int]:
        return self._attached.pid if self._attached is not None else None

    def instances(self) -> list:
        return discovery.list_instances(self.discovery_dir, self._probe, self._identify)

    def find(self, pid: int) -> Optional[discovery.Instance]:
        return discovery.find_instance(pid, self.discovery_dir, self._probe, self._identify)

    def _identify(self, instance: discovery.Instance, info: discovery.ProcessInfo) -> tuple:
        editor = self._launched.get(instance.pid)
        if editor is None or not editor.running:
            return discovery.identify(instance, info)
        # The Popen handle keeps the pid from being reused, so only a file older than the launch can
        # belong to another process.
        started = discovery.parse_timestamp(instance.started_at)
        if started is not None and started + discovery.IDENTITY_TOLERANCE < editor.launched_at:
            return False, f"the file predates the launch of pid {instance.pid} by this server"
        return True, ""

    def default_config(self) -> str:
        return "releaseeditor" if paths.engine_exe("releaseeditor", self.repo_root).is_file() else "debugeditor"

    def describe_instances(self) -> list:
        entries, listed = [], set()
        for instance in self.instances():
            listed.add(instance.pid)
            entry = instance.describe()
            entry["attached"] = instance.pid == self.attached_pid
            entry["launchedByServer"] = instance.pid in self._launched
            entries.append(entry)
        for pid, editor in self._launched.items():
            if pid not in listed and editor.running:
                entries.append(
                    {"pid": pid, "config": editor.config, "bridge": "not listening", "launchedByServer": True,
                     "log": str(editor.log_path)}
                )
        return entries

    async def client(self) -> BridgeClient:
        """Connection to the attached editor; attaches automatically when exactly one verified editor runs."""
        async with self._state_lock:
            if self._attached is not None:
                return await self._reconnect_locked()
            running = self.instances()
            if not running:
                raise EngineError(
                    "No running YAEngine editor with the agent bridge enabled was found. "
                    "Start the editor, or call engine_launch."
                )
            if len(running) > 1:
                listing = to_json([instance.describe() for instance in running])
                raise EngineError(
                    f"Several YAEngine editors are running; call engine_attach with the pid of the one to use. "
                    f"Running: {listing}"
                )
            [instance] = running
            if not instance.verified:
                raise EngineError(
                    f"An agent bridge entry for pid {instance.pid} was found, but that process could not be verified "
                    f"as the editor that published it ({instance.identity_note}), so it was not attached "
                    f"automatically. Call engine_attach(pid={instance.pid}) to use it anyway."
                )
            return await self._attach_locked(instance, explicit=False)

    async def request(self, method: str, params: Optional[dict] = None, timeout: Optional[float] = None) -> Any:
        client = await self.client()
        return await client.request(method, params, timeout)

    async def attach(self, pid: int) -> dict:
        async with self._state_lock:
            instance = self.find(pid)
            if instance is None:
                raise EngineError(
                    f"No running editor with pid {pid} has the agent bridge open. "
                    f"Running: {to_json(self.describe_instances())}"
                )
            client = await self._attach_locked(instance, explicit=True)
            return self._connection_summary(instance, client)

    async def launch(self, config: Optional[str] = None, extra_args: Sequence[str] = ()) -> dict:
        async with self._lifecycle_lock:
            return await self._launch_locked(config or self.default_config(), list(extra_args))

    async def stop(self, pid: Optional[int] = None) -> dict:
        async with self._lifecycle_lock:
            return await self._stop_locked(self._stop_target() if pid is None else pid)

    async def restart(self) -> dict:
        async with self._lifecycle_lock:
            editor = self._restart_target()
            stopped = await self._stop_locked(editor.pid)
            if not stopped["stopped"]:
                raise EngineError(f"Restart aborted because the editor did not stop: {to_json(stopped)}")
            launched = await self._launch_locked(editor.config, editor.extra_args)
            return {"stopped": stopped, "launched": launched}

    async def shutdown(self) -> None:
        """Stops the editors this server launched. Attached editors are left running."""
        async with self._lifecycle_lock:
            for pid, editor in list(self._launched.items()):
                if not editor.running:
                    self._launched.pop(pid, None)
                    continue
                try:
                    log.info("shutdown: %s", to_json(await self._stop_locked(pid)))
                except Exception:
                    log.exception("shutdown: stopping editor pid %d failed", pid)
        async with self._state_lock:
            await self._detach_locked()

    async def _reconnect_locked(self) -> BridgeClient:
        if self._client is not None and self._client.connected:
            return self._client
        pid = self._attached.pid
        # Re-read discovery: a restarted listener has a new port and token.
        current = self.find(pid)
        if current is None:
            await self._detach_locked()
            raise EngineError(
                f"The attached editor (pid {pid}) is no longer running or has closed its agent bridge. "
                "Call engine_list_instances, then engine_attach or engine_launch."
            )
        return await self._attach_locked(current, explicit=self._explicit)

    async def _attach_locked(self, instance: discovery.Instance, explicit: bool) -> BridgeClient:
        if instance.protocol_version != bridge.PROTOCOL_VERSION:
            raise EngineError(
                f"The editor with pid {instance.pid} uses bridge protocol {instance.protocol_version}; "
                f"this server supports {bridge.PROTOCOL_VERSION}."
            )
        client = self._client
        if client is None or (client.port, client.token) != (instance.port, instance.token):
            await self._close_client_locked()
            client = BridgeClient(instance.port, instance.token, default_timeout=self._request_timeout)
            self._client = client
        try:
            await client.connect()
        except BridgeError as exc:
            await self._detach_locked()
            raise EngineError(f"Could not connect to the editor with pid {instance.pid}: {exc}") from exc
        self._attached = instance
        self._explicit = explicit
        return client

    async def _detach_locked(self) -> None:
        await self._close_client_locked()
        self._attached = None
        self._explicit = False

    async def _close_client_locked(self) -> None:
        client, self._client = self._client, None
        if client is not None:
            await client.close()

    async def _forget(self, pid: int) -> None:
        async with self._state_lock:
            if self.attached_pid == pid:
                await self._detach_locked()

    @staticmethod
    def _connection_summary(instance: discovery.Instance, client: BridgeClient) -> dict:
        hello = client.hello or {}
        summary = {
            "attached": instance.pid,
            "buildConfig": hello.get("buildConfig", instance.build_config),
            "scenePath": hello.get("scenePath", instance.scene_path),
            "port": instance.port,
            "exePath": instance.exe_path,
            "verified": instance.verified,
        }
        if instance.identity_note:
            summary["identityNote"] = instance.identity_note
        return summary

    async def _launch_locked(self, config: str, extra_args: list) -> dict:
        if config not in paths.ENGINE_CONFIGS:
            raise EngineError(f"Unknown config {config!r}; use one of: {', '.join(paths.ENGINE_CONFIGS)}.")
        exe = paths.engine_exe(config, self.repo_root)
        if not exe.is_file():
            raise EngineError(f"{exe} does not exist. Build the RacingDemo target in cmake-build-{config} first.")

        args = ["--mcp", *extra_args]
        log_path, log_file = _create_log_file(self.logs_dir)
        launched_at = datetime.datetime.now(datetime.timezone.utc)
        try:
            process = subprocess.Popen(
                self._build_command(exe, args),
                cwd=exe.parent,
                stdin=subprocess.DEVNULL,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                creationflags=_CREATION_FLAGS,
            )
        except OSError as exc:
            raise EngineError(f"Could not start {exe}: {exc}") from exc
        finally:
            log_file.close()

        editor = LaunchedEditor(process, config, list(extra_args), log_path, launched_at)
        self._launched[editor.pid] = editor
        log.info("launched %s (pid %d), output in %s", exe, editor.pid, log_path)

        instance = await self._wait_for_bridge(editor)
        async with self._state_lock:
            client = await self._attach_locked(instance, explicit=True)
            summary = self._connection_summary(instance, client)
        return {"pid": editor.pid, "config": config, "args": args, "log": str(log_path), **summary}

    async def _wait_for_bridge(self, editor: LaunchedEditor) -> discovery.Instance:
        loop = asyncio.get_running_loop()
        deadline = loop.time() + self._launch_timeout
        while True:
            instance = self.find(editor.pid)
            if instance is not None:
                return instance
            code = editor.process.poll()
            if code is not None:
                self._launched.pop(editor.pid, None)
                raise EngineError(
                    f"The editor exited with code {code} before opening the agent bridge. "
                    f"Log {editor.log_path}, last lines:\n{_log_tail(editor.log_path)}"
                )
            if loop.time() >= deadline:
                raise EngineError(
                    f"The editor (pid {editor.pid}) did not open the agent bridge within {self._launch_timeout:g} s. "
                    f"It is still running; stop it with engine_stop(pid={editor.pid}). "
                    f"Log {editor.log_path}, last lines:\n{_log_tail(editor.log_path)}"
                )
            await asyncio.sleep(POLL_INTERVAL)

    def _stop_target(self) -> int:
        if self._attached is not None:
            return self._attached.pid
        running = self.instances()
        if len(running) == 1:
            [instance] = running
            if not instance.verified:
                raise EngineError(
                    f"The only agent bridge entry found (pid {instance.pid}) could not be verified as an editor "
                    f"({instance.identity_note}); pass its pid to engine_stop to ask it to quit through the bridge."
                )
            return instance.pid
        if not running:
            raise EngineError("No editor is attached or running; nothing to stop.")
        raise EngineError(
            f"Several editors are running; pass the pid of the one to stop. Running: {to_json(self.describe_instances())}"
        )

    def _restart_target(self) -> LaunchedEditor:
        attached = self.attached_pid
        if attached is not None:
            editor = self._launched.get(attached)
            if editor is None:
                raise EngineError(
                    f"The attached editor (pid {attached}) was not started by this server; engine_restart only "
                    "restarts editors started with engine_launch."
                )
            if not editor.running:
                raise EngineError(f"The attached editor (pid {attached}) has exited; use engine_launch.")
            return editor
        running = [editor for editor in self._launched.values() if editor.running]
        if len(running) == 1:
            return running[0]
        if not running:
            raise EngineError("engine_restart only restarts editors started with engine_launch, and none is running.")
        pids = ", ".join(str(editor.pid) for editor in running)
        raise EngineError(f"Several launched editors are running (pids {pids}); engine_attach to the one to restart.")

    async def _stop_locked(self, pid: int) -> dict:
        editor = self._launched.get(pid)
        if editor is not None and not editor.running:
            self._launched.pop(pid, None)
            await self._forget(pid)
            return {"pid": pid, "stopped": True, "steps": [], "exitCode": editor.process.returncode}

        instance = self.find(pid)
        if instance is None and editor is None:
            raise EngineError(
                f"No running editor with pid {pid} is known (no open agent bridge and not launched by this server); "
                "nothing was stopped."
            )

        result: dict[str, Any] = {"pid": pid, "steps": []}
        exited = False
        if instance is not None:
            sent, problem = await self._send_quit(instance)
            if sent:
                result["steps"].append("engine.quit")
                if problem:
                    result["quitNote"] = problem
                exited = await self._wait_exit(pid, editor, instance)
            else:
                result["quitError"] = problem
        if not exited:
            refusal = self._taskkill_refusal(pid, editor, instance)
            if refusal is None:
                output = await asyncio.to_thread(self._taskkill, pid)
                result["steps"].append("taskkill")
                if output:
                    result["taskkill"] = output
                exited = await self._wait_exit(pid, editor, instance)
            elif refusal == "":
                exited = True
            else:
                result["stopped"] = False
                result["note"] = refusal
                return result

        result["stopped"] = exited
        if exited:
            self._launched.pop(pid, None)
            await self._forget(pid)
            if editor is not None:
                result["exitCode"] = editor.process.returncode
        else:
            result["note"] = "The editor is still running (it may be waiting on a dialog); it was not force-killed."
        return result

    def _taskkill_refusal(
        self, pid: int, editor: Optional[LaunchedEditor], instance: Optional[discovery.Instance]
    ) -> Optional[str]:
        """None when taskkill may run, "" when the process is already gone, otherwise why it must not run."""
        if editor is not None:
            return None
        # Checked again right before the kill: the pid may have changed hands while quit was pending.
        verdict, reason = self._identify(instance, self._probe(pid))
        if verdict is True:
            return None
        if verdict is False:
            return ""
        return (
            f"The editor did not quit through the bridge, and taskkill was not used because pid {pid} could not be "
            f"verified as that editor ({reason}). Close it by hand if it is still open."
        )

    async def _send_quit(self, instance: discovery.Instance) -> tuple:
        """(sent, detail): detail is the error when the quit was not delivered, else an optional note."""
        async with self._state_lock:
            shared = self._client if self.attached_pid == instance.pid else None
        if shared is not None and (shared.port, shared.token) == (instance.port, instance.token):
            client = shared
        else:
            client = BridgeClient(instance.port, instance.token, default_timeout=self._request_timeout)
        try:
            try:
                await client.connect()
            except BridgeError as exc:
                return False, str(exc)
            try:
                await client.request("engine.quit")
            except BridgeError as exc:
                if exc.method != "engine.quit":
                    return False, str(exc)
                # The editor closes the socket as it shuts down, possibly before the reply is written.
                if exc.code == bridge.DISCONNECTED and exc.sent:
                    return True, ""
                # A busy main thread still handles the queued quit once it gets to it.
                if exc.code == bridge.TIMEOUT:
                    return True, f"engine.quit was sent but not answered: {exc}"
                return False, str(exc)
            return True, ""
        finally:
            if client is not shared:
                await client.close()

    async def _wait_exit(
        self, pid: int, editor: Optional[LaunchedEditor], instance: Optional[discovery.Instance]
    ) -> bool:
        loop = asyncio.get_running_loop()
        deadline = loop.time() + self._stop_timeout
        while True:
            if editor is not None:
                gone = not editor.running
            else:
                gone = self._identify(instance, self._probe(pid))[0] is False
            if gone:
                return True
            if loop.time() >= deadline:
                return False
            await asyncio.sleep(POLL_INTERVAL)


def _create_log_file(directory: Path):
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    for index in itertools.count():
        path = directory / (f"{stamp}.log" if index == 0 else f"{stamp}-{index}.log")
        try:
            return path, open(path, "xb")
        except FileExistsError:
            continue


def _log_tail(path: Path) -> str:
    try:
        with open(path, "rb") as handle:
            handle.seek(0, os.SEEK_END)
            handle.seek(max(0, handle.tell() - LOG_TAIL_BYTES))
            data = handle.read()
    except OSError as exc:
        return f"(log unreadable: {exc})"
    text = _ANSI_ESCAPE.sub("", data.decode("utf-8", errors="replace"))
    lines = text.splitlines()[-LOG_TAIL_LINES:]
    return "\n".join(lines) if lines else "(log is empty)"
