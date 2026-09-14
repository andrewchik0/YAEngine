"""Stdio MCP server entry point (`yaengine-mcp`). Stdout carries MCP traffic only; logs go to stderr."""

import contextlib
import logging
import sys
from typing import Optional

import anyio
from mcp.server.mcpserver import MCPServer

from . import __version__, paths
from .instances import InstanceManager
from .tools import register_all

log = logging.getLogger("yaengine_mcp")

INSTRUCTIONS = (
    "Controls YAEngine editors (a Vulkan rendering engine) through their local agent bridge. "
    "Engine tools attach automatically when exactly one editor is running; otherwise use "
    "engine_list_instances and engine_attach, or engine_launch to start an editor."
)


def create_server(manager: Optional[InstanceManager] = None) -> MCPServer:
    manager = manager if manager is not None else InstanceManager()

    @contextlib.asynccontextmanager
    async def lifespan(_server):
        try:
            yield manager
        finally:
            # Shielded so launched editors still get a graceful stop when the session is cancelled.
            with anyio.CancelScope(shield=True):
                await manager.shutdown()

    server = MCPServer(name="yaengine", instructions=INSTRUCTIONS, version=__version__, lifespan=lifespan)
    register_all(server, manager)
    return server


def configure_logging() -> None:
    handler = logging.StreamHandler(sys.stderr)
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s"))
    root = logging.getLogger()
    root.handlers[:] = [handler]
    root.setLevel(logging.WARNING)
    log.setLevel(logging.INFO)


def main() -> None:
    configure_logging()
    if not (paths.REPO_ROOT / "Core").is_dir():
        log.warning("%s does not look like the YAEngine repository; engine_launch will not find builds", paths.REPO_ROOT)
    try:
        create_server().run("stdio")
    except KeyboardInterrupt:
        pass
