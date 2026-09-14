"""Stand-in for RacingDemo.exe in launch tests: serves a FakeBridge and publishes its discovery file."""

import argparse
import asyncio
import sys
from pathlib import Path

from fake_bridge import FakeBridge


async def serve(discovery_dir: Path) -> None:
    bridge = await FakeBridge(build_config="DebugEditor").start()
    path = bridge.write_discovery(discovery_dir)
    try:
        await bridge.quit_requested.wait()
    finally:
        path.unlink(missing_ok=True)
        await bridge.stop()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--discovery-dir", required=True)
    parser.add_argument("--crash", action="store_true")
    args, _unknown = parser.parse_known_args()
    print("fake engine argv: " + " ".join(sys.argv[3:]), flush=True)
    if args.crash:
        print("\x1b[31mfatal: simulated startup failure\x1b[0m", flush=True)
        return 3
    asyncio.run(serve(Path(args.discovery_dir)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
