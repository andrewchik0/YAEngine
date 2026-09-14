"""Command line cases of Tools/FrameAnalysis/capture.py on a synthetic capture.

Shared by test_capture_cli_golden.py and generate_capture_golden.py, which records the output of the
committed script so the working tree copy can be compared against it.
"""

import hashlib
import subprocess
import sys
from pathlib import Path

from PIL import Image

from synthetic_capture import write_capture, write_shot

GOLDEN_FILE = Path(__file__).with_name("data") / "capture_cli_golden.json"
ROOT_TOKEN = "<CAPTURE>"

# Summary cases hold no failed shots and no after-pass shots: their lines are new since the golden commit.
CASES = [
    {"id": "summary-session", "args": ["summary", "{session}"]},
    {"id": "summary-folders", "args": ["summary", "{loose}"]},
    {"id": "stats-first-target", "args": ["stats", "{raster}"]},
    {"id": "stats-hdr", "args": ["stats", "{raster}", "--target", "resolved"]},
    {"id": "stats-rect", "args": ["stats", "{raster}", "--target", "resolved", "--rect", "10,5,4,2"]},
    {"id": "stats-depth", "args": ["stats", "{raster}", "--target", "mainDepth"]},
    {"id": "stats-packed", "args": ["stats", "{bright}", "--target", "gbuffer1"]},
    {"id": "px-hdr", "args": ["px", "{raster}", "resolved", "1", "0"]},
    {"id": "px-ldr", "args": ["px", "{raster}", "final", "63", "35"]},
    {"id": "rect-stats", "args": ["rect", "{raster}", "resolved", "8,4,16,8"]},
    {"id": "rect-dump", "args": ["rect", "{raster}", "mainDepth", "0,0,2,2", "--dump"]},
    {"id": "preview-default", "args": ["preview", "{raster}", "resolved"],
     "png": "{raster}/resolved.auto.preview.png"},
    {"id": "preview-log-out", "args": ["preview", "{bright}", "resolved", "--map", "log", "--exposure", "2",
                                       "--out", "{root}/log.png"], "png": "{root}/log.png"},
    {"id": "preview-falsecolor", "args": ["preview", "{raster}", "resolved", "--map", "falsecolor"],
     "png": "{raster}/resolved.falsecolor.preview.png"},
    {"id": "preview-linear-ldr", "args": ["preview", "{multi}", "final", "--map", "linear"],
     "png": "{multi}/final.linear.preview.png"},
    {"id": "diff-default", "args": ["diff", "{raster}", "{bright}"]},
    {"id": "diff-hdr", "args": ["diff", "{raster}", "{bright}", "--target", "resolved"]},
    {"id": "diff-extent-mismatch", "args": ["diff", "{raster}", "{wide}"]},
    {"id": "temporal-hdr", "args": ["temporal", "{multi}", "resolved"]},
    {"id": "temporal-ldr", "args": ["temporal", "{multi}", "final"]},
    {"id": "error-no-manifest", "args": ["stats", "{root}/missing"]},
    {"id": "error-unknown-target", "args": ["stats", "{raster}", "--target", "nosuch"]},
    {"id": "error-rect-outside", "args": ["stats", "{raster}", "--target", "resolved", "--rect", "60,0,10,10"]},
    {"id": "error-rect-syntax", "args": ["stats", "{raster}", "--rect", "1,2,3"]},
    {"id": "error-px-outside", "args": ["px", "{raster}", "final", "64", "0"]},
    {"id": "error-dump-too-large", "args": ["rect", "{raster}", "resolved", "0,0,16,16", "--dump"]},
    {"id": "error-temporal-single-frame", "args": ["temporal", "{raster}", "resolved"]},
    {"id": "error-diff-missing", "args": ["diff", "{raster}", "{root}/missing"]},
]


def build_capture(root: Path) -> dict:
    root = Path(root)
    capture = write_capture(root / "session")
    loose = root / "loose"
    write_shot(loose / "000_first", name="first")
    write_shot(loose / "001_second", name="second", index=1, exposure=0.5, view="Normals",
               warnings=["target 'taa' skipped: not in the graph"])
    wide = write_shot(root / "wide", name="wide", final_size=(80, 45))
    return {"root": root, "session": capture["root"], "raster": capture["raster"], "bright": capture["bright"],
            "multi": capture["multi"], "loose": loose, "wide": wide}


def run_case(script, locations: dict, case: dict) -> dict:
    values = {key: str(value) for key, value in locations.items()}
    args = [part.format(**values) for part in case["args"]]
    completed = subprocess.run([sys.executable, str(script), *args], capture_output=True, stdin=subprocess.DEVNULL)

    root = values["root"]

    def clean(data: bytes) -> str:
        return data.decode("utf-8", errors="backslashreplace").replace("\r\n", "\n").replace(root, ROOT_TOKEN)

    outcome = {"returncode": completed.returncode, "stdout": clean(completed.stdout), "stderr": clean(completed.stderr)}
    if "png" in case:
        with Image.open(case["png"].format(**values)) as image:
            outcome["png"] = {"size": list(image.size), "mode": image.mode,
                              "sha256": hashlib.sha256(image.tobytes()).hexdigest()}
    return outcome
