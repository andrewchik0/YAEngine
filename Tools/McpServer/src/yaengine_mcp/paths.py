"""Filesystem locations shared by the server, the smoke script and the tests."""

import os
from pathlib import Path

PACKAGE_DIR = Path(__file__).resolve().parent
# src/yaengine_mcp -> Tools/McpServer -> repository root
PROJECT_DIR = PACKAGE_DIR.parents[1]
REPO_ROOT = PROJECT_DIR.parents[1]

ENGINE_CONFIGS = ("releaseeditor", "debugeditor")


def local_app_data() -> Path:
    value = os.environ.get("LOCALAPPDATA")
    return Path(value) if value else Path.home() / "AppData" / "Local"


def discovery_dir() -> Path:
    return local_app_data() / "YAEngine" / "Bridge"


def launch_logs_dir() -> Path:
    return discovery_dir() / "logs"


def engine_exe(config: str, repo_root: Path = REPO_ROOT) -> Path:
    return Path(repo_root) / f"cmake-build-{config}" / "RacingDemo" / "RacingDemo.exe"
