"""Records the command line output of a committed Tools/FrameAnalysis/capture.py into tests/data.

    uv run python tests/generate_capture_golden.py [--rev HEAD]

test_capture_cli_golden.py then checks that the working tree script prints the same. Regenerate only
after an intended change of the script's output has been committed.
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from capture_golden import CASES, GOLDEN_FILE, build_capture, run_case  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[3]


def git(*args) -> bytes:
    return subprocess.run(["git", *args], cwd=REPO_ROOT, capture_output=True, check=True).stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rev", default="HEAD")
    args = parser.parse_args()

    commit = git("rev-parse", args.rev).decode().strip()
    source = git("show", f"{commit}:Tools/FrameAnalysis/capture.py")
    with tempfile.TemporaryDirectory() as temporary:
        temporary = Path(temporary)
        script = temporary / "capture_committed.py"
        script.write_bytes(source)
        locations = build_capture(temporary / "capture")
        cases = {case["id"]: run_case(script, locations, case) for case in CASES}

    GOLDEN_FILE.parent.mkdir(exist_ok=True)
    GOLDEN_FILE.write_text(json.dumps({"commit": commit, "cases": cases}, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(cases)} cases from {commit} to {GOLDEN_FILE}")


if __name__ == "__main__":
    main()
