import subprocess
import sys

import pytest

from fake_bridge import FakeBridge
from yaengine_mcp.bridge import BridgeClient
from yaengine_mcp.instances import InstanceManager

# A venv python.exe on Windows is a launcher that runs the interpreter as a child process.
BASE_PYTHON = getattr(sys, "_base_executable", sys.executable)


@pytest.fixture
def spare_process():
    """A live process that is not an editor, standing in for one that took over an editor's pid."""
    process = subprocess.Popen([BASE_PYTHON, "-c", "import time; time.sleep(300)"], stdin=subprocess.DEVNULL)
    yield process
    process.kill()
    process.wait()


@pytest.fixture(autouse=True)
def isolated_local_app_data(tmp_path, monkeypatch):
    # Keeps tests away from the discovery files of editors the user may have open.
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path / "LocalAppData"))


def forbid_taskkill(pid: int) -> str:
    raise AssertionError(f"tests must not run the real taskkill (pid {pid})")


@pytest.fixture
async def fake_bridge():
    bridge = await FakeBridge().start()
    yield bridge
    await bridge.stop()


@pytest.fixture
async def client(fake_bridge):
    client = BridgeClient(fake_bridge.port, fake_bridge.token)
    await client.connect()
    yield client
    await client.close()


@pytest.fixture
async def make_manager(tmp_path):
    managers = []

    def make(**overrides) -> InstanceManager:
        options = dict(
            repo_root=tmp_path / "repo",
            discovery_dir=tmp_path / "Bridge",
            logs_dir=tmp_path / "logs",
            launch_timeout=30.0,
            stop_timeout=5.0,
            taskkill=forbid_taskkill,
        )
        options.update(overrides)
        manager = InstanceManager(**options)
        managers.append(manager)
        return manager

    yield make
    for manager in managers:
        await manager.shutdown()
