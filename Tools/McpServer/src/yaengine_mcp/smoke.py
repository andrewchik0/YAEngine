"""Manual bridge check against an editor that is already running: hello, engine.status, last log lines.

    uv run yaengine-mcp-smoke [--pid PID] [--count 20]
"""

import argparse
import asyncio
import sys
from typing import Optional

from . import discovery
from .bridge import BridgeClient, BridgeError
from .formatting import format_log_tail, to_json


async def run(pid: Optional[int], count: int) -> int:
    instances = discovery.list_instances()
    if pid is not None:
        instances = [instance for instance in instances if instance.pid == pid]
    if not instances:
        suffix = f" with pid {pid}" if pid is not None else ""
        print(f"no running editor{suffix} has the agent bridge open", file=sys.stderr)
        return 1
    if len(instances) > 1:
        print("several editors are running; pass --pid:", file=sys.stderr)
        for instance in instances:
            print(to_json(instance.describe()), file=sys.stderr)
        return 1

    instance = instances[0]
    print("instance:", to_json(instance.describe()))
    client = BridgeClient(instance.port, instance.token)
    try:
        print("hello:", to_json(await client.connect()))
        print("engine.status:", to_json(await client.request("engine.status")))
        print(format_log_tail(await client.request("log.tail", {"count": count})))
    except BridgeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        await client.close()
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description="Check the YAEngine agent bridge of a running editor.")
    parser.add_argument("--pid", type=int, help="editor process id (required when several editors run)")
    parser.add_argument("--count", type=int, default=20, help="number of log lines to print")
    args = parser.parse_args()
    sys.exit(asyncio.run(run(args.pid, args.count)))
