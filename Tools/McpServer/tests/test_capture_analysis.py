import json
import os
import subprocess
import sys

import numpy as np
import pytest
from PIL import Image

from synthetic_capture import write_capture, write_shot
from yaengine_mcp.tools.capture import ANALYSIS_SCRIPT, analysis


@pytest.fixture
def capture(tmp_path):
    return write_capture(tmp_path / "cap")


@pytest.fixture
def module():
    return analysis()


def run_cli(*args):
    return subprocess.run([sys.executable, str(ANALYSIS_SCRIPT), *map(str, args)], capture_output=True, text=True)


def test_summary_lists_every_shot_and_its_warnings(capture, module):
    lines = list(module.summary_lines(capture["root"]))
    assert lines[0] == "session ok  2026-09-13T12:00:00Z  Test GPU"
    assert [line.split()[0] for line in lines[3:]] == ["raster", "bright", "!", "multi"]
    assert "    ! target 'pt_noisy' skipped: layout UNDEFINED" in lines


def test_summary_names_the_pass_a_shot_was_captured_after(tmp_path, module):
    write_shot(tmp_path / "000_lit", name="lit", after_pass="DeferredLighting")
    write_shot(tmp_path / "001_end", name="end", index=1)
    lines = list(module.summary_lines(tmp_path))
    assert [line.split()[0] for line in lines[1:]] == ["lit", "captured", "end"]
    assert lines[2] == "    captured after pass DeferredLighting"


def add_failed_shot(root):
    session_path = root / "session.json"
    session = json.loads(session_path.read_text(encoding="utf-8"))
    session["shots"].append({"index": 3, "name": "broken", "dir": "003_broken", "status": "failed"})
    session["status"] = "partial"
    session_path.write_text(json.dumps(session, indent=2), encoding="utf-8")


def test_summary_lists_a_failed_shot_and_goes_on(capture, module):
    add_failed_shot(capture["root"])
    # A failed shot ahead of a good one must not hide it.
    session = json.loads((capture["root"] / "session.json").read_text(encoding="utf-8"))
    session["shots"].insert(0, session["shots"].pop())
    (capture["root"] / "session.json").write_text(json.dumps(session), encoding="utf-8")

    lines = list(module.summary_lines(capture["root"]))

    missing = os.path.join(str(capture["root"]), "003_broken")
    assert lines[3] == "broken         failed  (no manifest.json in %s)" % missing
    assert [line.split()[0] for line in lines[4:]] == ["raster", "bright", "!", "multi"]


def test_cli_summary_of_a_session_with_a_failed_shot_succeeds(capture):
    add_failed_shot(capture["root"])
    result = run_cli("summary", capture["root"])
    assert result.returncode == 0, result.stderr
    assert result.stdout.splitlines()[-1].startswith("broken         failed  (no manifest.json in ")


def test_summary_without_a_session_only_lists_shot_folders(tmp_path, module):
    write_shot(tmp_path / "000_first", name="first")
    (tmp_path / "ui").mkdir()
    (tmp_path / "ui" / "000_editor.png").write_bytes(b"")
    write_shot(tmp_path / "001_second", name="second", index=1)

    lines = list(module.summary_lines(tmp_path))

    assert [line.split()[0] for line in lines[1:]] == ["first", "second"]


def test_manifests_are_read_as_utf8(tmp_path, module):
    shot = write_shot(tmp_path / "000_strasse", name="Straße", requested_by="name=Straße;view=normals",
                      warnings=["unknown target 'Ωmega'"])
    assert "Straße".encode("utf-8") in (shot / "manifest.json").read_bytes()

    assert module.load_manifest(shot)["shot"]["requestedBy"] == "name=Straße;view=normals"
    lines = list(module.summary_lines(tmp_path))
    assert lines[1].startswith("Straße ") and lines[2] == "    ! unknown target 'Ωmega'"
    assert list(module.stats_lines(shot, "resolved"))[0].startswith("resolved  R16G16B16A16_SFLOAT")

    result = run_cli("summary", tmp_path)
    assert result.returncode == 0 and result.stderr == ""
    assert "Stra" in result.stdout and "unknown target" in result.stdout


def test_stats_measure_the_raw_file(capture, module):
    lines = list(module.stats_lines(capture["raster"], "resolved"))
    assert lines[0] == "resolved  R16G16B16A16_SFLOAT  64x36  linear_hdr"
    assert lines[1] == "  pixels=2304  NaN=1  Inf=1"
    red = lines[3].split()
    assert red[0] == "r" and float(red[1]) == 0.0 and float(red[-1]) == 20.0
    # the brightest channel exceeds 1 everywhere but the first four columns, and 10 on the right half
    assert lines[-1] == "  fraction over 1/10/100: %.5f %.5f 0.00000" % (60 / 64, 32 / 64)


def test_stats_of_a_rect_and_depth(capture, module):
    rect = list(module.stats_lines(capture["raster"], "resolved", "10,5,4,2"))
    assert rect[0].startswith("resolved rect=10,5,4,2") and rect[1] == "  pixels=8  NaN=0  Inf=0"
    depth = list(module.stats_lines(capture["raster"], "mainDepth"))
    assert depth[2] == "  reversed-Z: 0 is the far plane, 1 is the near plane"


def test_compute_stats_returns_plain_numbers(capture, module):
    manifest = module.load_manifest(capture["raster"])
    target = module.find_target(manifest, "gbuffer1")
    stats = module.compute_stats(target, module.load_target(capture["raster"], target))
    assert stats["nan"] == 0 and stats["pixels"] == 64 * 36
    assert [channel["name"] for channel in stats["channels"]] == ["r", "g", "b", "a"]
    assert stats["channels"][0]["max"] == 1.0 and stats["channels"][3]["min"] == 1.0
    assert isinstance(stats["channels"][1]["mean"], float)


def test_px_and_rect_dump(capture, module):
    assert list(module.px_lines(capture["raster"], "resolved", 1, 0)) == [
        "resolved (1, 0) R16G16B16A16_SFLOAT", "  r      0.317382812", "  g      inf", "  b      0.25", "  a      1"]
    dump = list(module.rect_lines(capture["raster"], "mainDepth", "0,0,2,2", dump=True))
    assert dump[-2].startswith("  (0,1)") and dump[-1].startswith("  (1,1)")


def test_rect_dump_refuses_large_rects_after_the_stats(capture, module):
    produced = []
    with pytest.raises(module.CaptureError, match="--dump refuses 256 pixels"):
        for line in module.rect_lines(capture["raster"], "resolved", "0,0,16,16", dump=True):
            produced.append(line)
    assert produced[0] == "resolved rect=0,0,16,16  R16G16B16A16_SFLOAT  64x36  linear_hdr"


def test_preview_maps_hdr_to_8_bit(capture, module, tmp_path):
    out = tmp_path / "log.png"
    path, line = module.write_preview(capture["raster"], "resolved", "log", 1.0, str(out))
    assert line == "resolved -> %s  (log, exposure 1)" % out
    with Image.open(path) as image:
        assert image.size == (64, 36) and image.mode == "RGB"
    pixels = module.preview_pixels(np.full((2, 2, 4), 1.0, np.float32), "log", 1.0)
    # log10(1) sits in the middle of the -4..4 range
    assert pixels[0, 0, 0] == 128


def test_default_preview_path_is_inside_the_shot(capture, module):
    path, _ = module.write_preview(capture["raster"], "final", "linear")
    assert path.endswith("final.linear.preview.png")


def test_diff_reports_settings_and_image_delta(capture, module):
    lines = list(module.diff_lines(capture["raster"], capture["bright"], "resolved"))
    assert lines[0] == "settings that differ (raster vs bright):"
    assert lines[1] == "  exposure               1.0  ->  2.0"
    assert lines[2] == "image diff on 'resolved':"
    assert lines[3].startswith("  r      mean=+")
    assert lines[7] == "  where the difference lives:"
    assert len(lines) == 8 + 12


def test_temporal_detects_a_period_2_pattern(capture, module):
    lines = list(module.temporal_lines(capture["multi"], "resolved"))
    assert lines[0].startswith("resolved  3 frames")
    assert "ALTERNATING (period 2)" in lines[3]


@pytest.mark.parametrize(
    "call, message",
    [
        (lambda m, c: list(m.stats_lines(c["root"] / "missing")), "no manifest.json in"),
        (lambda m, c: list(m.stats_lines(c["raster"], "nosuch")), "no target 'nosuch'; have final, resolved"),
        (lambda m, c: list(m.stats_lines(c["raster"], "resolved", "60,0,10,10")), "does not fit in 64x36"),
        (lambda m, c: list(m.px_lines(c["raster"], "final", 64, 0)), "is outside 64x36"),
        (lambda m, c: list(m.temporal_lines(c["raster"], "resolved")), "holds a single frame"),
        (lambda m, c: list(m.stats_lines(c["raster"], None, "1,2,3")), "rect wants x,y,w,h"),
    ],
)
def test_problems_raise_capture_errors(capture, module, call, message):
    with pytest.raises(module.CaptureError, match=message):
        call(module, capture)


def test_cli_prints_the_library_lines(capture, module):
    for args, lines in [
        (["summary", capture["root"]], module.summary_lines(capture["root"])),
        (["stats", capture["raster"], "--target", "resolved"], module.stats_lines(capture["raster"], "resolved")),
        (["diff", capture["raster"], capture["bright"]], module.diff_lines(capture["raster"], capture["bright"])),
        (["temporal", capture["multi"], "resolved"], module.temporal_lines(capture["multi"], "resolved")),
    ]:
        result = run_cli(*args)
        assert result.returncode == 0, result.stderr
        assert result.stdout == "\n".join(lines) + "\n"


def test_cli_keeps_partial_output_and_exits_with_the_message(capture):
    result = run_cli("rect", capture["raster"], "resolved", "0,0,16,16", "--dump")
    assert result.returncode == 1
    assert result.stdout.startswith("resolved rect=0,0,16,16")
    assert result.stderr == "--dump refuses 256 pixels; keep the rect at 64 or fewer\n"
