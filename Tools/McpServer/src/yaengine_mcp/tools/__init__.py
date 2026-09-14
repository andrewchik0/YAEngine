"""MCP tool groups.

Each public module in this package is a tool group that defines
`register(server: MCPServer, manager: InstanceManager) -> None` and registers its tools with `tool`.
register_all discovers the modules, so adding a group means adding a file here.
"""

import functools
import importlib
import logging
import pkgutil
from typing import Callable, Optional

from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from ..bridge import BridgeError
from ..formatting import to_json
from ..instances import EngineError, InstanceManager

log = logging.getLogger(__name__)

__all__ = ["group_names", "register_all", "to_json", "tool"]


def tool(server: MCPServer, *, description: str, name: Optional[str] = None) -> Callable:
    """Registers an async tool that returns MCP content: str, mcp Image, or a list of them.

    Editor and bridge failures become tool errors carrying their message; anything else is
    reported by the SDK as an unexpected crash.
    """

    def decorate(fn: Callable) -> Callable:
        @functools.wraps(fn)
        async def wrapper(*args, **kwargs):
            try:
                return await fn(*args, **kwargs)
            except (EngineError, BridgeError) as exc:
                raise ToolError(str(exc)) from exc

        # Unstructured: results are short text or images, and a structured copy would double them.
        server.add_tool(wrapper, name=name or fn.__name__, description=description, structured_output=False)
        return fn

    return decorate


def group_names() -> list:
    return sorted(module.name for module in pkgutil.iter_modules(__path__) if not module.name.startswith("_"))


def register_all(server: MCPServer, manager: InstanceManager) -> list:
    registered = []
    for name in group_names():
        module = importlib.import_module(f"{__name__}.{name}")
        register = getattr(module, "register", None)
        if register is None:
            log.warning("tool group %s defines no register(); skipped", name)
            continue
        register(server, manager)
        registered.append(name)
    return registered
