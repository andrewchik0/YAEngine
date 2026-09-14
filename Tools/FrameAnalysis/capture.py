"""Read and query FrameCapture sessions.

Everything is decoded from manifest.json - dtype, channel count, extent and row pitch -
so this file carries no VkFormat table of its own. Output is deliberately compact: the
consumer is a context window, not a terminal.

    python capture.py summary  <capture-dir>
    python capture.py stats    <shot-dir> [--target NAME] [--rect x,y,w,h]
    python capture.py px       <shot-dir> <target> <x> <y>
    python capture.py rect     <shot-dir> <target> <x,y,w,h> [--dump]
    python capture.py preview   <shot-dir> <target> [--map linear|log|auto|falsecolor]
                                [--exposure E] [--out FILE.png]
    python capture.py diff     <shot-A> <shot-B> [--target NAME]
    python capture.py temporal <shot-dir> <target>

Importable as well: each *_lines function yields exactly the lines its subcommand prints,
and a CaptureError carries the message the command line exits with.
"""

import argparse
import json
import os
import sys

import numpy as np

# Matches PT_DEBUG_LOG_MIN / PT_DEBUG_LOG_MAX in Core/Shared/FrameUniforms.h, so a log
# preview here and the engine's HDR Magnitude view read off the same scale.
LOG_MIN = -4.0
LOG_MAX = 4.0

MAP_MODES = ("linear", "log", "auto", "falsecolor")
STAT_COLUMNS = ("min", "p1", "p50", "mean", "p99", "p99.9", "max")


class CaptureError(Exception):
    """The capture on disk cannot answer the request; the message says why."""


def load_manifest(shot_dir):
    path = os.path.join(shot_dir, "manifest.json")
    if not os.path.isfile(path):
        raise CaptureError("no manifest.json in %s" % shot_dir)
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def find_target(manifest, name=None):
    targets = manifest["targets"]
    if not targets:
        raise CaptureError("manifest lists no targets")
    if name is None:
        return targets[0]
    for target in targets:
        if target["name"] == name:
            return target
    raise CaptureError("no target '%s'; have %s" % (name, ", ".join(t["name"] for t in targets)))


def load_target(shot_dir, target, filename=None):
    """Returns (h, w, channels) float32, NaN and Inf preserved."""
    fmt = target["format"]
    width, height = target["extent"]
    dtype = np.dtype(fmt["dtype"])
    # Elements per pixel rather than the logical channel count: a packed format stores one
    # uint32 that stands for four channels.
    elements = fmt["bytesPerPixel"] // dtype.itemsize

    path = os.path.join(shot_dir, filename or target["files"]["raw"])
    raw = np.fromfile(path, dtype=dtype)
    expected = width * height * elements
    if raw.size != expected:
        raise CaptureError("%s holds %d elements, manifest implies %d" % (path, raw.size, expected))

    raw = raw.reshape(height, width, elements)

    if fmt["name"] == "A2B10G10R10_UNORM_PACK32":
        packed = raw[:, :, 0].astype(np.uint32)
        return np.stack([
            (packed & 0x3FF) / 1023.0,
            ((packed >> 10) & 0x3FF) / 1023.0,
            ((packed >> 20) & 0x3FF) / 1023.0,
            ((packed >> 30) & 0x3) / 3.0,
        ], axis=2).astype(np.float32)

    image = raw.astype(np.float32)
    if dtype == np.uint8:
        image /= 255.0
    return image


def channel_names(target, count):
    if target["format"]["name"] == "D32_SFLOAT":
        return ["depth"]
    if target["format"]["name"] == "R32_UINT":
        return ["id"]
    return ["r", "g", "b", "a"][:count]


def crop(image, rect):
    if rect is None:
        return image
    x, y, w, h = rect
    height, width = image.shape[:2]
    if x < 0 or y < 0 or x + w > width or y + h > height or w <= 0 or h <= 0:
        raise CaptureError("rect %s does not fit in %dx%d" % (rect, width, height))
    return image[y:y + h, x:x + w]


def parse_rect(text):
    parts = text.split(",")
    if len(parts) != 4:
        raise CaptureError("rect wants x,y,w,h")
    return tuple(int(p) for p in parts)


def format_row(values, width=10, digits=4):
    return " ".join(("%*.*g" % (width, digits, v)) for v in values)


def compute_stats(target, image):
    """Per-channel distribution of a decoded image, as plain numbers.

    Channels without a single finite value carry only their name.
    """
    names = channel_names(target, image.shape[2])
    finite = np.where(np.isfinite(image), image, np.nan)

    channels = []
    for index, name in enumerate(names):
        column = finite[:, :, index].ravel()
        column = column[~np.isnan(column)]
        if column.size == 0:
            channels.append({"name": name})
            continue
        p1, p50, p99, p999 = np.percentile(column, [1, 50, 99, 99.9])
        values = (column.min(), p1, p50, column.mean(), p99, p999, column.max())
        channel = {"name": name}
        channel.update((key, float(value)) for key, value in zip(STAT_COLUMNS, values))
        channels.append(channel)

    peak = np.nanmax(np.where(np.isfinite(image), image, np.nan), axis=2)
    total = peak.size
    return {
        "pixels": image.shape[0] * image.shape[1],
        "nan": int(np.isnan(image).sum()),
        "inf": int(np.isinf(image).sum()),
        "channels": channels,
        "fractionOver1": np.count_nonzero(peak > 1.0) / total,
        "fractionOver10": np.count_nonzero(peak > 10.0) / total,
        "fractionOver100": np.count_nonzero(peak > 100.0) / total,
    }


def stats_report_lines(target, image, label):
    stats = compute_stats(target, image)

    yield "%s  %s  %s  %s" % (
        label, target["format"]["name"], "x".join(str(v) for v in target["extent"]),
        target["colorSpace"])
    yield "  pixels=%d  NaN=%d  Inf=%d" % (stats["pixels"], stats["nan"], stats["inf"])
    if target["format"]["name"] == "D32_SFLOAT":
        yield "  reversed-Z: 0 is the far plane, 1 is the near plane"

    header = "  %-6s %10s %10s %10s %10s %10s %10s %10s"
    yield header % (("chan",) + STAT_COLUMNS)
    for channel in stats["channels"]:
        if "min" not in channel:
            yield "  %-6s (no finite values)" % channel["name"]
            continue
        yield header % ((channel["name"],) + tuple("%.4g" % channel[key] for key in STAT_COLUMNS))

    yield "  fraction over 1/10/100: %.5f %.5f %.5f" % (
        stats["fractionOver1"], stats["fractionOver10"], stats["fractionOver100"])


def print_stats(target, image, label):
    for line in stats_report_lines(target, image, label):
        print(line)


def ascii_map(values, rows=12, columns=24):
    height, width = values.shape
    bh, bw = max(1, height // rows), max(1, width // columns)
    tile = values[:bh * rows, :bw * columns].reshape(rows, bh, columns, bw).mean(axis=(1, 3))
    tile = tile / (tile.max() + 1e-12)
    ramp = " .:-=+*#%@"
    return ["    " + "".join(ramp[min(9, int(v * 9.999))] for v in row) for row in tile]


def summary_lines(capture_dir):
    session_path = os.path.join(capture_dir, "session.json")
    if os.path.isfile(session_path):
        with open(session_path, encoding="utf-8") as handle:
            session = json.load(handle)
        yield "session %s  %s  %s" % (session["status"], session["startedUtc"], session["gpu"]["name"])
        yield "scene %s" % session["scene"]
        shots = [(os.path.join(capture_dir, s.get("dir") or ""), s["name"], s["status"])
                 for s in session["shots"]]
    else:
        # Only shot folders: a capture root can also hold others, such as the editor's ui screenshots.
        shots = [(os.path.join(capture_dir, name), name, "unreadable")
                 for name in sorted(os.listdir(capture_dir))
                 if os.path.isfile(os.path.join(capture_dir, name, "manifest.json"))]

    yield "%-14s %-12s %-14s %8s %-14s %s" % (
        "shot", "path", "aa", "exposure", "view", "targets (max/mean of first channel)")
    for shot_dir, name, status in shots:
        try:
            manifest = load_manifest(shot_dir)
        except CaptureError as exc:
            yield "%-14s %s  (%s)" % (name, status, exc)
            continue
        settings = manifest["settings"]
        summary = []
        for target in manifest["targets"]:
            stats = target["stats"]
            summary.append("%s %.3g/%.3g" % (target["name"], stats["max"][0], stats["mean"][0]))
        yield "%-14s %-12s %-14s %8.3f %-14s %s" % (
            manifest["shot"]["name"],
            settings["renderPath"]["effective"],
            settings["antialiasing"]["effective"],
            settings["exposure"],
            settings["debugView"]["name"],
            "  ".join(summary))
        if "capturedAfterPass" in manifest:
            yield "    captured after pass %s" % manifest["capturedAfterPass"]
        for warning in manifest["warnings"]:
            yield "    ! %s" % warning


def stats_lines(shot_dir, target_name=None, rect_text=None):
    manifest = load_manifest(shot_dir)
    target = find_target(manifest, target_name)
    image = load_target(shot_dir, target)
    rect = parse_rect(rect_text) if rect_text else None
    label = target["name"] if rect is None else "%s rect=%s" % (target["name"], rect_text)
    yield from stats_report_lines(target, crop(image, rect), label)


def px_lines(shot_dir, target_name, x, y):
    manifest = load_manifest(shot_dir)
    target = find_target(manifest, target_name)
    image = load_target(shot_dir, target)
    height, width = image.shape[:2]
    if not (0 <= x < width and 0 <= y < height):
        raise CaptureError("pixel (%d, %d) is outside %dx%d" % (x, y, width, height))

    names = channel_names(target, image.shape[2])
    values = image[y, x]
    yield "%s (%d, %d) %s" % (target["name"], x, y, target["format"]["name"])
    for name, value in zip(names, values):
        yield "  %-6s %.9g" % (name, value)


def rect_lines(shot_dir, target_name, rect_text, dump=False):
    manifest = load_manifest(shot_dir)
    target = find_target(manifest, target_name)
    image = load_target(shot_dir, target)
    rect = parse_rect(rect_text)
    region = crop(image, rect)
    yield from stats_report_lines(target, region, "%s rect=%s" % (target["name"], rect_text))

    if dump:
        count = region.shape[0] * region.shape[1]
        if count > 64:
            raise CaptureError("--dump refuses %d pixels; keep the rect at 64 or fewer" % count)
        names = channel_names(target, region.shape[2])
        yield "  " + " ".join("%10s" % n for n in names)
        for row in range(region.shape[0]):
            for column in range(region.shape[1]):
                yield "  (%d,%d) %s" % (rect[0] + column, rect[1] + row, format_row(region[row, column]))


def tone_map(image, mode, exposure):
    rgb = image[:, :, :3] if image.shape[2] >= 3 else np.repeat(image[:, :, :1], 3, axis=2)
    rgb = np.nan_to_num(rgb, nan=0.0, posinf=0.0, neginf=0.0) * exposure

    if mode == "linear":
        return np.clip(rgb, 0.0, 1.0)

    if mode == "auto":
        scale = np.percentile(rgb, 99.0)
        return np.clip(rgb / (scale + 1e-12), 0.0, 1.0)

    magnitude = np.maximum(np.max(rgb, axis=2), 1e-4)
    normalized = np.clip((np.log10(magnitude) - LOG_MIN) / (LOG_MAX - LOG_MIN), 0.0, 1.0)
    if mode == "log":
        return np.repeat(normalized[:, :, None], 3, axis=2)

    # falsecolor: one hue per decade, so a decade boundary is visible rather than inferred.
    hue = normalized * 5.0
    index = np.clip(hue.astype(np.int32), 0, 4)
    fraction = hue - index
    ramp = np.array([[0, 0, 0.5], [0, 0.7, 0.7], [0, 0.8, 0], [1, 1, 0], [1, 0.2, 0], [1, 1, 1]])
    return ramp[index] * (1.0 - fraction[:, :, None]) + ramp[index + 1] * fraction[:, :, None]


def preview_pixels(image, mode, exposure):
    """The 8-bit RGB image a preview PNG holds."""
    mapped = tone_map(image, mode, exposure)
    return (np.clip(mapped, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def write_preview(shot_dir, target_name, mode="auto", exposure=1.0, out=None):
    """Writes the preview PNG; returns its path and the line the command line prints."""
    from PIL import Image

    manifest = load_manifest(shot_dir)
    target = find_target(manifest, target_name)
    image = load_target(shot_dir, target)
    pixels = preview_pixels(image, mode, exposure)

    out = out or os.path.join(shot_dir, "%s.%s.preview.png" % (target["name"], mode))
    Image.fromarray(pixels).save(out)
    return out, "%s -> %s  (%s, exposure %g)" % (target["name"], out, mode, exposure)


def diff_lines(shot_a, shot_b, target_name=None):
    left = load_manifest(shot_a)
    right = load_manifest(shot_b)

    yield "settings that differ (%s vs %s):" % (left["shot"]["name"], right["shot"]["name"])
    differing = 0
    for key in sorted(set(left["settings"]) | set(right["settings"])):
        a = left["settings"].get(key)
        b = right["settings"].get(key)
        if a != b:
            yield "  %-22s %s  ->  %s" % (key, a, b)
            differing += 1
    if differing == 0:
        yield "  (none)"

    target_a = find_target(left, target_name)
    target_b = find_target(right, target_name or target_a["name"])
    if target_a["extent"] != target_b["extent"]:
        yield "extents differ (%s vs %s), skipping the image diff" % (target_a["extent"], target_b["extent"])
        return

    image_a = np.nan_to_num(load_target(shot_a, target_a))
    image_b = np.nan_to_num(load_target(shot_b, target_b))
    delta = image_b - image_a
    names = channel_names(target_a, delta.shape[2])

    yield "image diff on '%s':" % target_a["name"]
    for index, name in enumerate(names):
        column = delta[:, :, index]
        yield "  %-6s mean=%+.5g  mean|d|=%.5g  max|d|=%.5g" % (
            name, column.mean(), np.abs(column).mean(), np.abs(column).max())

    magnitude = np.abs(delta[:, :, :3]).mean(axis=2) if delta.shape[2] >= 3 \
        else np.abs(delta[:, :, 0])
    yield "  where the difference lives:"
    yield from ascii_map(magnitude)


def temporal_lines(shot_dir, target_name):
    manifest = load_manifest(shot_dir)
    target = find_target(manifest, target_name)
    files = target["files"]
    if "pattern" not in files:
        raise CaptureError("'%s' holds a single frame; capture it with frames=N for a temporal run"
                           % target["name"])

    frames = []
    for index in range(manifest["timing"]["framesCaptured"]):
        path = files["pattern"] % index
        if not os.path.isfile(os.path.join(shot_dir, path)):
            break
        frames.append(np.nan_to_num(load_target(shot_dir, target, path)))

    if len(frames) < 3:
        raise CaptureError("only %d frames on disk, need at least 3" % len(frames))

    stack = np.stack(frames)
    luminance = stack[:, :, :, :3].mean(axis=3) if stack.shape[3] >= 3 else stack[:, :, :, 0]
    base = float(luminance.mean()) + 1e-6

    d1 = float(np.abs(luminance[1:] - luminance[:-1]).mean())
    d2 = float(np.abs(luminance[2:] - luminance[:-2]).mean())
    deviation = luminance.std(axis=0)

    yield "%s  %d frames  mean_lum=%.4f" % (target["name"], len(frames), base)
    yield "  frame-to-frame |d| = %.5f  (%.2f%% of mean)" % (d1, 100 * d1 / base)
    yield "  two-frame      |d| = %.5f  (%.2f%% of mean)" % (d2, 100 * d2 / base)
    yield "  period-2 ratio d2/d1 = %.3f   %s" % (
        d2 / (d1 + 1e-12),
        "ALTERNATING (period 2)" if d2 < 0.5 * d1 else "(no period-2 pattern)")
    yield "  pixels unstable = %.2f%%" % (100 * float(np.mean(deviation > 0.02 * base)))
    yield "  instability map:"
    yield from ascii_map(deviation)


def emit(lines):
    # Printed as produced, so a failure part way still leaves the lines before it on stdout.
    for line in lines:
        print(line)


def cmd_summary(args):
    emit(summary_lines(args.capture_dir))


def cmd_stats(args):
    emit(stats_lines(args.shot_dir, args.target, args.rect))


def cmd_px(args):
    emit(px_lines(args.shot_dir, args.target, args.x, args.y))


def cmd_rect(args):
    emit(rect_lines(args.shot_dir, args.target, args.rect, args.dump))


def cmd_preview(args):
    _, line = write_preview(args.shot_dir, args.target, args.map, args.exposure, args.out)
    print(line)


def cmd_diff(args):
    emit(diff_lines(args.shot_a, args.shot_b, args.target))


def cmd_temporal(args):
    emit(temporal_lines(args.shot_dir, args.target))


def main():
    # Manifests hold UTF-8 text (shot recipes, warnings) that a redirected Windows stdout may not encode.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")

    parser = argparse.ArgumentParser(description="Query a FrameCapture session.")
    sub = parser.add_subparsers(dest="command", required=True)

    summary = sub.add_parser("summary", help="one table over every shot")
    summary.add_argument("capture_dir")
    summary.set_defaults(func=cmd_summary)

    stats = sub.add_parser("stats", help="per-channel statistics for one target")
    stats.add_argument("shot_dir")
    stats.add_argument("--target")
    stats.add_argument("--rect")
    stats.set_defaults(func=cmd_stats)

    px = sub.add_parser("px", help="exact value at one pixel")
    px.add_argument("shot_dir")
    px.add_argument("target")
    px.add_argument("x", type=int)
    px.add_argument("y", type=int)
    px.set_defaults(func=cmd_px)

    rect = sub.add_parser("rect", help="statistics over a rectangle")
    rect.add_argument("shot_dir")
    rect.add_argument("target")
    rect.add_argument("rect")
    rect.add_argument("--dump", action="store_true")
    rect.set_defaults(func=cmd_rect)

    preview = sub.add_parser("preview", help="turn a float target into a viewable PNG")
    preview.add_argument("shot_dir")
    preview.add_argument("target")
    preview.add_argument("--map", choices=list(MAP_MODES), default="auto")
    preview.add_argument("--exposure", type=float, default=1.0)
    preview.add_argument("--out")
    preview.set_defaults(func=cmd_preview)

    diff = sub.add_parser("diff", help="manifest and image difference between two shots")
    diff.add_argument("shot_a")
    diff.add_argument("shot_b")
    diff.add_argument("--target")
    diff.set_defaults(func=cmd_diff)

    temporal = sub.add_parser("temporal", help="frame-to-frame stability of a multi-frame shot")
    temporal.add_argument("shot_dir")
    temporal.add_argument("target")
    temporal.set_defaults(func=cmd_temporal)

    args = parser.parse_args()
    try:
        args.func(args)
    except CaptureError as exc:
        sys.exit(str(exc))


if __name__ == "__main__":
    main()
