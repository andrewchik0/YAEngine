"""Discovery files that running editors publish in %LOCALAPPDATA%/YAEngine/Bridge (protocol v1)."""

import ctypes
import json
import logging
import os
import re
import sys
from dataclasses import dataclass, replace
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Callable, Optional

from . import paths

log = logging.getLogger(__name__)

_FILE_NAME = re.compile(r"^(\d+)\.json$")

# startedAt has whole seconds and the listener may start in the same second as the process.
IDENTITY_TOLERANCE = timedelta(seconds=5)


@dataclass(frozen=True)
class Instance:
    pid: int
    port: int
    token: str
    protocol_version: int
    exe_path: str
    build_config: str
    repo_root: str
    scene_path: str
    started_at: str
    file: Path
    # False when the process behind pid could not be confirmed as the editor that wrote the file.
    verified: bool = True
    identity_note: str = ""

    def describe(self) -> dict:
        entry = {
            "pid": self.pid,
            "buildConfig": self.build_config,
            "scenePath": self.scene_path,
            "port": self.port,
            "startedAt": self.started_at,
            "exePath": self.exe_path,
            "protocolVersion": self.protocol_version,
            "verified": self.verified,
        }
        if self.identity_note:
            entry["identityNote"] = self.identity_note
        return entry


@dataclass(frozen=True)
class ProcessInfo:
    alive: bool
    image: Optional[str] = None
    created: Optional[datetime] = None
    # Why image or created could not be read from a live process.
    problem: str = ""


if sys.platform == "win32":
    from ctypes import wintypes

    _kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    _kernel32.OpenProcess.restype = wintypes.HANDLE
    _kernel32.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
    _kernel32.GetExitCodeProcess.restype = wintypes.BOOL
    _kernel32.QueryFullProcessImageNameW.argtypes = (
        wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD))
    _kernel32.QueryFullProcessImageNameW.restype = wintypes.BOOL
    _kernel32.GetProcessTimes.argtypes = (wintypes.HANDLE,) + (ctypes.POINTER(wintypes.FILETIME),) * 4
    _kernel32.GetProcessTimes.restype = wintypes.BOOL
    _kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    _kernel32.CloseHandle.restype = wintypes.BOOL

    _PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    _ERROR_ACCESS_DENIED = 5
    _STILL_ACTIVE = 259
    _IMAGE_PATH_CHARS = 32768
    _FILETIME_EPOCH = datetime(1601, 1, 1, tzinfo=timezone.utc)

    def _image_path(handle) -> tuple:
        buffer = ctypes.create_unicode_buffer(_IMAGE_PATH_CHARS)
        size = wintypes.DWORD(_IMAGE_PATH_CHARS)
        if not _kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
            return None, f"cannot read the process image path (Windows error {ctypes.get_last_error()})"
        return buffer.value, ""

    def _creation_time(handle) -> tuple:
        times = [wintypes.FILETIME() for _ in range(4)]
        if not _kernel32.GetProcessTimes(handle, *(ctypes.byref(value) for value in times)):
            return None, f"cannot read the process creation time (Windows error {ctypes.get_last_error()})"
        ticks = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        return _FILETIME_EPOCH + timedelta(microseconds=ticks // 10), ""

    def probe_process(pid: int) -> ProcessInfo:
        # os.kill(pid, 0) terminates the target on Windows, so the kernel is queried instead.
        if pid <= 0 or pid > 0xFFFFFFFF:
            return ProcessInfo(alive=False)
        handle = _kernel32.OpenProcess(_PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            # Access denied still proves the process exists (for example a protected process).
            if ctypes.get_last_error() == _ERROR_ACCESS_DENIED:
                return ProcessInfo(alive=True, problem="access to the process was denied")
            return ProcessInfo(alive=False)
        try:
            code = wintypes.DWORD()
            if _kernel32.GetExitCodeProcess(handle, ctypes.byref(code)) and code.value != _STILL_ACTIVE:
                return ProcessInfo(alive=False)
            image, image_problem = _image_path(handle)
            created, time_problem = _creation_time(handle)
            return ProcessInfo(alive=True, image=image, created=created, problem=image_problem or time_problem)
        finally:
            _kernel32.CloseHandle(handle)

else:

    def probe_process(pid: int) -> ProcessInfo:
        if pid <= 0:
            return ProcessInfo(alive=False)
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return ProcessInfo(alive=False)
        except PermissionError:
            pass
        return ProcessInfo(alive=True, problem="process identity can only be checked on Windows")


def is_pid_alive(pid: int) -> bool:
    return probe_process(pid).alive


def parse_timestamp(text: str) -> Optional[datetime]:
    """ISO 8601 as the protocol writes it (UTC, 'Z' suffix); a time without offset counts as UTC."""
    value = text.strip() if isinstance(text, str) else ""
    if value[-1:] in ("Z", "z"):
        value = value[:-1] + "+00:00"
    try:
        moment = datetime.fromisoformat(value)
    except ValueError:
        return None
    if moment.tzinfo is None:
        moment = moment.replace(tzinfo=timezone.utc)
    return moment.astimezone(timezone.utc)


def _normalized_path(path: str) -> str:
    if path.startswith("\\\\?\\"):
        path = path[4:]
    return os.path.normcase(os.path.normpath(path))


def same_executable(first: str, second: str) -> bool:
    if _normalized_path(first) == _normalized_path(second):
        return True
    # Covers spellings that name the same file differently (8.3 short names, junctions).
    try:
        return os.path.samefile(first, second)
    except (OSError, ValueError):
        return False


def identify(instance: Instance, info: ProcessInfo) -> tuple:
    """(True, "") when pid still runs the editor that wrote the file, (False, why) when the file is stale,
    (None, why) when that cannot be decided."""
    pid = instance.pid
    if not info.alive:
        return False, f"pid {pid} is not running"
    if info.image is None or info.created is None:
        return None, f"the identity of process {pid} cannot be read: {info.problem or 'unknown reason'}"
    if not instance.exe_path:
        return None, "the discovery file names no exePath"
    started = parse_timestamp(instance.started_at)
    if started is None:
        return None, f"the discovery file has an unreadable startedAt {instance.started_at!r}"
    if not same_executable(info.image, instance.exe_path):
        return False, f"pid {pid} now belongs to {info.image}"
    if info.created > started + IDENTITY_TOLERANCE:
        return False, (
            f"process {pid} was created at {info.created.isoformat(timespec='seconds')}, after the editor that "
            f"wrote the file started its bridge ({started.isoformat(timespec='seconds')})"
        )
    return True, ""


Identify = Callable[[Instance, ProcessInfo], tuple]


def _parse_instance(raw: bytes, path: Path) -> Optional[Instance]:
    try:
        data = json.loads(raw.decode("utf-8-sig"))
    except ValueError as exc:
        log.warning("unreadable discovery file %s: %s", path, exc)
        return None
    if not isinstance(data, dict):
        log.warning("discovery file %s is not a JSON object", path)
        return None
    pid, port, token = _int(data.get("pid")), _int(data.get("port")), data.get("token")
    if pid is None or port is None or not isinstance(token, str):
        log.warning("discovery file %s lacks a valid pid, port or token", path)
        return None
    return Instance(
        pid=pid,
        port=port,
        token=token,
        protocol_version=_int(data.get("protocolVersion")) or 0,
        exe_path=_text(data, "exePath"),
        build_config=_text(data, "buildConfig"),
        repo_root=_text(data, "repoRoot"),
        scene_path=_text(data, "scenePath"),
        started_at=_text(data, "startedAt"),
        file=Path(path),
    )


def _int(value) -> Optional[int]:
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def _text(data: dict, key: str) -> str:
    value = data.get(key)
    return value if isinstance(value, str) else ""


def _read_raw(path: Path) -> Optional[bytes]:
    try:
        return Path(path).read_bytes()
    except OSError as exc:
        log.warning("unreadable discovery file %s: %s", path, exc)
        return None


def read_instance(path: Path) -> Optional[Instance]:
    raw = _read_raw(path)
    return None if raw is None else _parse_instance(raw, Path(path))


def list_instances(
    directory: Optional[Path] = None,
    probe: Callable[[int], ProcessInfo] = probe_process,
    identify: Identify = identify,
) -> list[Instance]:
    """Instances sorted by pid. Files whose pid is gone or now runs another process are deleted;
    files whose process identity cannot be read are listed with verified=False."""
    directory = paths.discovery_dir() if directory is None else Path(directory)
    try:
        entries = list(directory.iterdir())
    except FileNotFoundError:
        return []
    except OSError as exc:
        log.warning("cannot list discovery directory %s: %s", directory, exc)
        return []

    instances = []
    for path in entries:
        match = _FILE_NAME.match(path.name)
        if match is None or not path.is_file():
            continue
        raw = _read_raw(path)
        instance = None if raw is None else _parse_instance(raw, path)
        if instance is None:
            pid = int(match.group(1))
            if not probe(pid).alive:
                _delete_stale(path, raw, f"pid {pid} is not running")
            continue
        verdict, reason = identify(instance, probe(instance.pid))
        if verdict is False:
            _delete_stale(path, raw, reason)
            continue
        if verdict is None:
            instance = replace(instance, verified=False, identity_note=reason)
        instances.append(instance)
    return sorted(instances, key=lambda instance: instance.pid)


def find_instance(
    pid: int,
    directory: Optional[Path] = None,
    probe: Callable[[int], ProcessInfo] = probe_process,
    identify: Identify = identify,
) -> Optional[Instance]:
    return next((instance for instance in list_instances(directory, probe, identify) if instance.pid == pid), None)


def _delete_stale(path: Path, raw: Optional[bytes], reason: str) -> None:
    try:
        # An editor that now owns the pid may have just replaced the file; its fresh copy stays.
        if raw is not None and path.read_bytes() != raw:
            return
        path.unlink(missing_ok=True)
        log.info("removed stale discovery file %s (%s)", path, reason)
    except FileNotFoundError:
        pass
    except OSError as exc:
        log.warning("cannot remove stale discovery file %s: %s", path, exc)
