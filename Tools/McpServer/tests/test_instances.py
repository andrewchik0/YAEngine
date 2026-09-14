import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

from fake_bridge import FAKE_EXE_PATH, FakeBridge, wait_disconnected
from yaengine_mcp import instances
from yaengine_mcp.bridge import DISCONNECTED, BridgeClient, BridgeError
from yaengine_mcp.discovery import ProcessInfo
from yaengine_mcp.instances import EngineError

FAKE_ENGINE = Path(__file__).with_name("fake_engine.py")
# A venv python.exe on Windows is a launcher that runs the interpreter as a child process, so its
# pid would differ from the pid the fake engine publishes.
BASE_PYTHON = getattr(sys, "_base_executable", sys.executable)
LONG_AGO = datetime(2020, 1, 1, tzinfo=timezone.utc)


class Liveness:
    """Process probe for made-up pids: live ones run the fake editor executable, denied ones hide it."""

    def __init__(self, denied=()):
        self.alive = set()
        self.denied = set(denied)

    def __call__(self, pid: int) -> ProcessInfo:
        if pid not in self.alive:
            return ProcessInfo(alive=False)
        if pid in self.denied:
            return ProcessInfo(alive=True, problem="access to the process was denied")
        return ProcessInfo(alive=True, image=FAKE_EXE_PATH, created=LONG_AGO)


@pytest.fixture
async def bridges(tmp_path):
    started = []

    async def start(pid: int, alive: Liveness) -> FakeBridge:
        bridge = await FakeBridge(pid=pid).start()
        bridge.write_discovery(tmp_path / "Bridge")
        alive.alive.add(pid)
        bridge.on_quit = lambda: alive.alive.discard(pid)
        started.append(bridge)
        return bridge

    yield start
    for bridge in started:
        await bridge.stop()


def fake_engine_command(discovery_dir: Path):
    def build(exe: Path, args: list) -> list:
        return [BASE_PYTHON, str(FAKE_ENGINE), "--discovery-dir", str(discovery_dir), *args]

    return build


def create_exe(repo_root: Path, config: str) -> Path:
    exe = repo_root / f"cmake-build-{config}" / "RacingDemo" / "RacingDemo.exe"
    exe.parent.mkdir(parents=True, exist_ok=True)
    exe.write_bytes(b"")
    return exe


def recording_taskkill(alive=None):
    killed = []

    def taskkill(pid):
        killed.append(pid)
        if alive is not None:
            alive.alive.discard(pid)
        return "SUCCESS"

    return killed, taskkill


async def test_no_editor_running(make_manager):
    with pytest.raises(EngineError, match="No running YAEngine editor"):
        await make_manager().request("engine.status")


async def test_single_editor_is_attached_automatically(tmp_path, make_manager, fake_bridge):
    fake_bridge.write_discovery(tmp_path / "Bridge")
    manager = make_manager()

    status = await manager.request("engine.status")

    assert status["pid"] == fake_bridge.pid
    assert manager.attached_pid == fake_bridge.pid
    [entry] = manager.describe_instances()
    assert entry["attached"] and not entry["launchedByServer"] and entry["verified"]
    assert "token" not in entry


async def test_several_editors_require_explicit_attach(make_manager, bridges):
    alive = Liveness()
    await bridges(1111, alive)
    await bridges(2222, alive)
    manager = make_manager(probe=alive)

    with pytest.raises(EngineError) as info:
        await manager.request("engine.status")
    assert "engine_attach" in str(info.value)
    assert "1111" in str(info.value) and "2222" in str(info.value)

    assert (await manager.attach(2222))["attached"] == 2222
    assert (await manager.request("engine.status"))["pid"] == 2222


async def test_attach_unknown_pid(make_manager):
    with pytest.raises(EngineError, match="No running editor with pid 9999"):
        await make_manager(probe=lambda pid: ProcessInfo(alive=True)).attach(9999)


async def test_reconnects_with_new_token_after_listener_restart(make_manager, bridges):
    alive = Liveness()
    old = await bridges(3333, alive)
    manager = make_manager(probe=alive)
    assert await manager.request("bridge.ping") == {}

    await old.stop()
    await wait_disconnected(manager._client)
    new = await bridges(3333, alive)
    assert (new.port, new.token) != (old.port, old.token)

    assert await manager.request("bridge.ping") == {}
    assert len(new.hellos()) == 1


async def test_attached_editor_that_exited_is_reported(tmp_path, make_manager, bridges):
    alive = Liveness()
    bridge = await bridges(4444, alive)
    manager = make_manager(probe=alive)
    await manager.attach(4444)

    alive.alive.discard(4444)
    await bridge.stop()
    await wait_disconnected(manager._client)

    with pytest.raises(EngineError, match="no longer running"):
        await manager.request("engine.status")
    assert manager.attached_pid is None
    assert not (tmp_path / "Bridge" / "4444.json").exists()


async def test_reused_pid_is_neither_attached_nor_stopped(tmp_path, make_manager, fake_bridge, spare_process):
    # The file an editor left behind, while its pid now belongs to an unrelated live process.
    hour_ago = (datetime.now(timezone.utc) - timedelta(hours=1)).isoformat()
    leftover = FakeBridge(pid=spare_process.pid, started_at=hour_ago)
    directory = tmp_path / "Bridge"
    killed, taskkill = recording_taskkill()
    manager = make_manager(taskkill=taskkill)

    stale = leftover.write_discovery(directory)
    with pytest.raises(EngineError, match="nothing to stop"):
        await manager.stop()
    assert not stale.exists()

    stale = leftover.write_discovery(directory)
    with pytest.raises(EngineError, match="nothing was stopped"):
        await manager.stop(spare_process.pid)
    assert not stale.exists()

    stale = leftover.write_discovery(directory)
    fake_bridge.write_discovery(directory)
    assert (await manager.request("engine.status"))["pid"] == fake_bridge.pid
    assert not stale.exists()

    assert killed == []
    assert spare_process.poll() is None


async def test_unverified_editor_is_listed_but_not_attached_automatically_or_taskkilled(make_manager, bridges):
    alive = Liveness(denied={6060})
    bridge = await bridges(6060, alive)
    bridge.ignore_quit = True
    # The default taskkill of make_manager fails the test when called.
    manager = make_manager(probe=alive, stop_timeout=0.2)

    [entry] = manager.describe_instances()
    assert entry["verified"] is False and "denied" in entry["identityNote"]
    with pytest.raises(EngineError, match="could not be verified.*engine_attach\\(pid=6060\\)"):
        await manager.request("engine.status")
    with pytest.raises(EngineError, match="pass its pid to engine_stop"):
        await manager.stop()

    result = await manager.stop(6060)
    assert result["steps"] == ["engine.quit"] and not result["stopped"]
    assert "taskkill was not used" in result["note"] and "denied" in result["note"]

    attached = await manager.attach(6060)
    assert attached["verified"] is False and "denied" in attached["identityNote"]
    assert (await manager.request("engine.status"))["pid"] == 6060


async def test_launched_editor_is_identified_by_its_process_handle(tmp_path, make_manager, spare_process):
    manager = make_manager()
    launched_at = datetime.now(timezone.utc)
    manager._launched[spare_process.pid] = instances.LaunchedEditor(
        spare_process, "debugeditor", [], tmp_path / "editor.log", launched_at)
    directory = tmp_path / "Bridge"
    try:
        older = (launched_at - timedelta(hours=1)).isoformat()
        stale = FakeBridge(pid=spare_process.pid, started_at=older).write_discovery(directory)
        assert manager.instances() == []
        assert not stale.exists()

        # The executable differs from the file's exePath; the Popen handle vouches for the pid.
        FakeBridge(pid=spare_process.pid).write_discovery(directory)
        [instance] = manager.instances()
        assert instance.pid == spare_process.pid and instance.verified
    finally:
        manager._launched.clear()


async def test_stop_uses_engine_quit(make_manager, bridges):
    alive = Liveness()
    bridge = await bridges(5555, alive)
    manager = make_manager(probe=alive)
    await manager.attach(5555)

    result = await manager.stop()

    assert result["stopped"] and result["steps"] == ["engine.quit"]
    assert bridge.quit_requested.is_set()
    assert manager.attached_pid is None


async def test_editor_closing_the_socket_before_the_quit_reply_counts_as_quit(make_manager, bridges):
    alive = Liveness()
    bridge = await bridges(5252, alive)
    bridge.quit_reply = "close"

    result = await make_manager(probe=alive).stop(5252)

    assert result["stopped"] and result["steps"] == ["engine.quit"]
    assert "quitError" not in result and "quitNote" not in result


async def test_unanswered_quit_waits_for_the_editor_to_exit(make_manager, bridges):
    alive = Liveness()
    bridge = await bridges(5151, alive)
    bridge.quit_reply = "none"

    result = await make_manager(probe=alive, request_timeout=0.5).stop(5151)

    assert result["stopped"] and result["steps"] == ["engine.quit"]
    assert "not answered" in result["quitNote"]
    assert bridge.quit_requested.is_set()


async def test_quit_that_was_never_sent_goes_straight_to_taskkill(make_manager, bridges, monkeypatch):
    alive = Liveness()
    await bridges(5353, alive)
    request = BridgeClient.request

    async def unsendable_quit(self, method, params=None, timeout=None):
        if method == "engine.quit":
            raise BridgeError(DISCONNECTED, "send failed: connection reset", method, sent=False)
        return await request(self, method, params, timeout)

    monkeypatch.setattr(BridgeClient, "request", unsendable_quit)
    killed, taskkill = recording_taskkill(alive)

    result = await make_manager(probe=alive, taskkill=taskkill).stop(5353)

    assert result["stopped"] and result["steps"] == ["taskkill"]
    assert "send failed" in result["quitError"]
    assert killed == [5353]


async def test_stop_falls_back_to_taskkill(make_manager, bridges):
    alive = Liveness()
    bridge = await bridges(6666, alive)
    bridge.ignore_quit = True
    killed, taskkill = recording_taskkill(alive)

    result = await make_manager(probe=alive, taskkill=taskkill, stop_timeout=0.3).stop(6666)

    assert result["stopped"] and result["steps"] == ["engine.quit", "taskkill"]
    assert killed == [6666]


def test_taskkill_is_never_forced():
    assert instances.taskkill_command(42) == ["taskkill", "/PID", "42"]


async def test_stop_reports_editor_that_stays_open(make_manager, bridges):
    alive = Liveness()
    bridge = await bridges(7777, alive)
    bridge.ignore_quit = True

    result = await make_manager(probe=alive, taskkill=lambda pid: "", stop_timeout=0.2).stop(7777)

    assert not result["stopped"]
    assert "not force-killed" in result["note"]


async def test_stop_refuses_unknown_pid(make_manager):
    with pytest.raises(EngineError, match="nothing was stopped"):
        await make_manager(probe=lambda pid: ProcessInfo(alive=True)).stop(8888)


def test_default_config_prefers_release_build(tmp_path, make_manager):
    manager = make_manager()
    create_exe(tmp_path / "repo", "debugeditor")
    assert manager.default_config() == "debugeditor"
    create_exe(tmp_path / "repo", "releaseeditor")
    assert manager.default_config() == "releaseeditor"


async def test_launch_requires_built_exe(make_manager):
    with pytest.raises(EngineError, match="does not exist"):
        await make_manager().launch("releaseeditor")


async def test_launch_restart_and_shutdown_with_fake_engine(tmp_path, make_manager):
    create_exe(tmp_path / "repo", "debugeditor")
    manager = make_manager(build_command=fake_engine_command(tmp_path / "Bridge"))

    launched = await manager.launch(extra_args=["--scene", "Assets/test.yaml"])
    first_pid = launched["pid"]
    assert launched["config"] == "debugeditor"
    assert launched["attached"] == first_pid
    assert launched["args"] == ["--mcp", "--scene", "Assets/test.yaml"]
    assert Path(launched["log"]).parent == tmp_path / "logs"
    assert "fake engine argv: --mcp --scene Assets/test.yaml" in Path(launched["log"]).read_text()
    assert (await manager.request("engine.status"))["pid"] == first_pid
    [entry] = manager.describe_instances()
    assert entry["launchedByServer"]

    restarted = await manager.restart()
    second_pid = restarted["launched"]["pid"]
    assert restarted["stopped"]["stopped"] and restarted["stopped"]["steps"] == ["engine.quit"]
    assert restarted["stopped"]["exitCode"] == 0
    assert second_pid != first_pid
    assert restarted["launched"]["args"] == launched["args"]
    assert (await manager.request("engine.status"))["pid"] == second_pid

    await manager.shutdown()
    assert manager.describe_instances() == []
    assert manager.attached_pid is None


async def test_launch_reports_early_exit_with_log_tail(tmp_path, make_manager):
    create_exe(tmp_path / "repo", "releaseeditor")
    manager = make_manager(build_command=fake_engine_command(tmp_path / "Bridge"))

    with pytest.raises(EngineError) as info:
        await manager.launch("releaseeditor", ["--crash"])

    message = str(info.value)
    assert "exited with code 3" in message
    assert "fatal: simulated startup failure" in message
    assert "\x1b[" not in message
    assert manager.describe_instances() == []


async def test_restart_and_shutdown_leave_attached_editors_alone(tmp_path, make_manager, fake_bridge):
    fake_bridge.write_discovery(tmp_path / "Bridge")
    manager = make_manager()
    await manager.attach(fake_bridge.pid)

    with pytest.raises(EngineError, match="engine_launch"):
        await manager.restart()
    await manager.shutdown()

    assert not fake_bridge.quit_requested.is_set()
    assert [request["method"] for request in fake_bridge.requests] == ["hello"]
