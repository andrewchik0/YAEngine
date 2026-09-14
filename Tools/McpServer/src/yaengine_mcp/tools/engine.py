"""Editor instance tools: list, attach, launch, stop, restart, status, log."""

from typing import Annotated, Literal, Optional

from mcp.server.mcpserver import MCPServer
from pydantic import Field

from ..formatting import format_log_tail, to_json
from ..instances import InstanceManager
from . import tool


def register(server: MCPServer, manager: InstanceManager) -> None:
    @tool(
        server,
        description=(
            "List the YAEngine editor processes on this machine that have the agent bridge open. Each entry has "
            "pid, buildConfig, scenePath, port, startedAt, exePath, whether this server is attached to it and "
            "whether this server launched it. Entries of editors that exited, or whose pid now belongs to another "
            "program, are removed automatically. 'verified' is false, with the reason in identityNote, when the "
            "process behind the pid cannot be checked against the entry (for example access is denied): such an "
            "editor is never attached automatically and never closed with taskkill."
        ),
    )
    async def engine_list_instances() -> str:
        return to_json({"instances": manager.describe_instances()})

    @tool(
        server,
        description=(
            "Attach to a running YAEngine editor by process id; engine tools then talk to that editor. Only needed "
            "when several editors are running: with exactly one, engine tools attach to it automatically."
        ),
    )
    async def engine_attach(
        pid: Annotated[int, Field(description="Process id of the editor, as listed by engine_list_instances.")],
    ) -> str:
        return to_json(await manager.attach(pid))

    @tool(
        server,
        description=(
            "Start a new YAEngine editor (RacingDemo.exe from the local build tree) with the agent bridge enabled, "
            "wait until the bridge accepts connections (up to 180 s) and attach to it. Editor output goes to the "
            "log file named in the result; if the editor exits during startup the error shows the end of that log. "
            "Editors started this way are closed gracefully when this server shuts down normally; if this server "
            "process is killed or crashes, they keep running (close them with engine_stop or their window)."
        ),
    )
    async def engine_launch(
        config: Annotated[
            Optional[Literal["releaseeditor", "debugeditor"]],
            Field(description="Build to run. Default: releaseeditor if it has been built, otherwise debugeditor."),
        ] = None,
        extra_args: Annotated[
            Optional[list[str]],
            Field(description="Additional command line arguments for the editor, passed after --mcp."),
        ] = None,
    ) -> str:
        return to_json(await manager.launch(config, extra_args or []))

    @tool(
        server,
        description=(
            "Close a YAEngine editor gracefully. The editor is asked to quit through the bridge (the same as closing "
            "its window); if it is still running after 30 s, a regular close request is sent with taskkill, but only "
            "to an editor this server launched or whose process identity was verified. The process is never "
            "force-killed, so an editor blocked by a dialog can stay open; the result reports whether it stopped."
        ),
    )
    async def engine_stop(
        pid: Annotated[
            Optional[int],
            Field(description="Process id of the editor to close. Default: the attached editor, or the only running one."),
        ] = None,
    ) -> str:
        return to_json(await manager.stop(pid))

    @tool(
        server,
        description=(
            "Restart an editor that this server started with engine_launch (the attached one when several are "
            "running): close it gracefully, start it again with the same config and arguments, and attach. Editors "
            "not started by this server are never restarted."
        ),
    )
    async def engine_restart() -> str:
        return to_json(await manager.restart())

    @tool(
        server,
        description=(
            "Report the state of the attached editor: pid, buildConfig, scenePath, frameIndex, fps, frameTimeMs, "
            "renderExtent and outputExtent as [width, height], the number of connected bridge clients, and "
            "captureBusy (true while a capture shot or a command line capture session is running)."
        ),
    )
    async def engine_status() -> str:
        return to_json(await manager.request("engine.status"))

    @tool(
        server,
        description=(
            "Return recent lines of the attached editor's log, oldest first, one per line formatted as "
            "'<seq> <level> [<tag>] <text> (<file>:<line>)'. The first line reports lastSeq, the newest "
            "sequence number in the editor's log buffer."
        ),
    )
    async def engine_log(
        count: Annotated[int, Field(ge=1, le=2000, description="Number of most recent lines to return.")] = 200,
        min_level: Annotated[
            Literal["verbose", "info", "warning", "error"], Field(description="Lowest severity to include.")
        ] = "info",
    ) -> str:
        return format_log_tail(await manager.request("log.tail", {"count": count, "minLevel": min_level}))
