import json
import os
import subprocess
import sys
from datetime import datetime, timedelta, timezone

import pytest

from conftest import BASE_PYTHON
from yaengine_mcp import discovery, paths
from yaengine_mcp.discovery import ProcessInfo


def exited_pid() -> int:
    process = subprocess.Popen([sys.executable, "-c", "pass"])
    process.wait()
    if discovery.is_pid_alive(process.pid):
        pytest.skip("the pid of an exited process was reused")
    return process.pid


def payload(pid: int, **overrides) -> dict:
    data = {
        "protocolVersion": 1,
        "pid": pid,
        "port": 50000,
        "token": "ab" * 16,
        "exePath": "C:\\work\\YAEngine\\cmake-build-debugeditor\\RacingDemo\\RacingDemo.exe",
        "buildConfig": "DebugEditor",
        "repoRoot": "C:\\work\\YAEngine",
        "scenePath": "",
        "startedAt": "2026-09-13T10:00:00Z",
    }
    data.update(overrides)
    return data


def own_payload(**overrides) -> dict:
    """A file this test process could have published: its own executable, started just now."""
    identity = {
        "exePath": discovery.probe_process(os.getpid()).image,
        "startedAt": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    return payload(os.getpid(), **(identity | overrides))


def write(directory, name, content):
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / name
    path.write_text(content if isinstance(content, str) else json.dumps(content), encoding="utf-8")
    return path


def test_repo_root_is_derived_from_package_location():
    assert (paths.REPO_ROOT / "Tools" / "McpServer" / "pyproject.toml").is_file()
    assert paths.PROJECT_DIR == paths.REPO_ROOT / "Tools" / "McpServer"


def test_discovery_dir_follows_localappdata(tmp_path, monkeypatch):
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path))
    assert paths.discovery_dir() == tmp_path / "YAEngine" / "Bridge"
    assert paths.launch_logs_dir() == tmp_path / "YAEngine" / "Bridge" / "logs"


def test_pid_liveness():
    assert discovery.is_pid_alive(os.getpid())
    assert not discovery.is_pid_alive(exited_pid())
    assert not discovery.is_pid_alive(0)
    assert not discovery.is_pid_alive(-1)


def test_live_instances_are_listed_and_stale_files_deleted(tmp_path):
    live = write(tmp_path, f"{os.getpid()}.json", own_payload(scenePath="Assets/a.yaml"))
    dead = exited_pid()
    stale = write(tmp_path, f"{dead}.json", payload(dead))

    [instance] = discovery.list_instances(tmp_path)

    assert (instance.pid, instance.port, instance.token) == (os.getpid(), 50000, "ab" * 16)
    assert (instance.build_config, instance.scene_path, instance.file) == ("DebugEditor", "Assets/a.yaml", live)
    assert instance.verified and instance.identity_note == ""
    assert live.exists()
    assert not stale.exists()


def test_process_probe_reads_image_and_creation_time(spare_process):
    info = discovery.probe_process(spare_process.pid)

    assert info.alive and info.problem == ""
    assert discovery.same_executable(info.image, BASE_PYTHON)
    assert info.created <= datetime.now(timezone.utc)


def test_reused_pid_is_stale_unless_image_and_start_time_match(tmp_path, spare_process):
    pid = spare_process.pid
    info = discovery.probe_process(pid)
    name = f"{pid}.json"

    # An editor's leftover file: its pid now runs python, started after the editor's bridge.
    other_program = write(tmp_path, name, payload(pid, startedAt=(info.created - timedelta(hours=1)).isoformat()))
    assert discovery.list_instances(tmp_path) == []
    assert not other_program.exists()

    same_program_restarted = write(
        tmp_path, name, payload(pid, exePath=info.image, startedAt=(info.created - timedelta(seconds=30)).isoformat()))
    assert discovery.list_instances(tmp_path) == []
    assert not same_program_restarted.exists()

    started = (info.created + timedelta(seconds=2)).strftime("%Y-%m-%dT%H:%M:%SZ")
    matching = write(tmp_path, name, payload(pid, exePath=info.image.upper().replace("\\", "/"), startedAt=started))
    [instance] = discovery.list_instances(tmp_path)
    assert instance.pid == pid and instance.verified
    assert matching.exists()


def test_unreadable_identity_is_listed_unverified(tmp_path):
    path = write(tmp_path, "4242.json", payload(4242))

    def denied(pid):
        return ProcessInfo(alive=True, problem="access to the process was denied")

    [instance] = discovery.list_instances(tmp_path, probe=denied)

    assert not instance.verified and "access to the process was denied" in instance.identity_note
    assert instance.describe()["verified"] is False and "identityNote" in instance.describe()
    assert path.exists()


EDITOR = "C:\\Games\\YAEngine\\RacingDemo.exe"
CREATED = datetime(2026, 9, 13, 10, 0, 0, tzinfo=timezone.utc)


@pytest.mark.parametrize(
    "exe_path, started_at, info, verdict",
    [
        ("c:/games/yaengine/RACINGDEMO.EXE", "2026-09-13T10:00:03Z", None, True),
        (EDITOR, "2026-09-13T10:00:00.250000+00:00", None, True),
        (EDITOR, "2026-09-13T09:59:56Z", None, True),
        (EDITOR, "2026-09-13T09:59:50Z", None, False),
        ("C:\\Games\\Other.exe", "2026-09-13T10:00:03Z", None, False),
        (EDITOR, "2026-09-13T10:00:03Z", ProcessInfo(alive=False), False),
        ("", "2026-09-13T10:00:03Z", None, None),
        (EDITOR, "yesterday", None, None),
        (EDITOR, "2026-09-13T10:00:03Z", ProcessInfo(alive=True, problem="access denied"), None),
    ],
)
def test_identity_rules(tmp_path, exe_path, started_at, info, verdict):
    instance = discovery.read_instance(write(tmp_path, "77.json", payload(77, exePath=exe_path, startedAt=started_at)))
    info = info or ProcessInfo(alive=True, image=EDITOR, created=CREATED)
    result, reason = discovery.identify(instance, info)
    assert result is verdict
    assert (reason == "") is (verdict is True)


def test_unparsable_file_is_deleted_only_when_its_pid_is_dead(tmp_path):
    dead = exited_pid()
    broken_dead = write(tmp_path, f"{dead}.json", "{not json")
    broken_live = write(tmp_path, f"{os.getpid()}.json", "{not json")

    assert discovery.list_instances(tmp_path) == []
    assert not broken_dead.exists()
    assert broken_live.exists()


def test_unrelated_files_are_left_alone(tmp_path):
    dead = exited_pid()
    others = [
        write(tmp_path, "notes.json", payload(dead)),
        write(tmp_path, f"{dead}.json.tmp", payload(dead)),
        write(tmp_path, f"{dead}.tmp", payload(dead)),
    ]
    (tmp_path / "logs").mkdir()

    assert discovery.list_instances(tmp_path) == []
    assert all(path.exists() for path in others)


def test_invalid_fields_are_skipped(tmp_path):
    write(tmp_path, f"{os.getpid()}.json", own_payload(port="50000"))
    assert discovery.list_instances(tmp_path) == []


def test_missing_directory_lists_nothing(tmp_path):
    assert discovery.list_instances(tmp_path / "absent") == []


def test_find_instance_and_describe_without_token(tmp_path):
    write(tmp_path, f"{os.getpid()}.json", own_payload())

    instance = discovery.find_instance(os.getpid(), tmp_path)

    assert instance is not None
    assert "token" not in instance.describe()
    assert discovery.find_instance(os.getpid() + 1, tmp_path) is None
