"""Small capture directories in the layout the engine writes, for tests that have no GPU."""

import json
from pathlib import Path

import numpy as np
from PIL import Image

# name -> (dtype, channels, bytesPerPixel, file suffix)
FORMATS = {
    "R8G8B8A8_UNORM": ("uint8", 4, 4, "rgba8"),
    "R16G16B16A16_SFLOAT": ("float16", 4, 8, "rgba16f"),
    "D32_SFLOAT": ("float32", 1, 4, "d32f"),
    "A2B10G10R10_UNORM_PACK32": ("uint32", 4, 4, "a2b10g10r10"),
}

WIDTH, HEIGHT = 64, 36


def _settings(exposure, view):
    return {
        "renderPath": {"selected": "Raster", "effective": "Raster"},
        "antialiasing": {"selected": "TAA", "effective": "TAA"},
        "debugView": {"requested": 0, "effective": 0, "name": view},
        "exposure": exposure,
        "bloom": {"enabled": False, "intensity": 0.05, "threshold": 1.0},
    }


def _decoded(fmt, array):
    if fmt == "A2B10G10R10_UNORM_PACK32":
        packed = array[:, :, 0].astype(np.uint64)
        return np.stack([(packed & 0x3FF) / 1023.0, ((packed >> 10) & 0x3FF) / 1023.0,
                         ((packed >> 20) & 0x3FF) / 1023.0, ((packed >> 30) & 0x3) / 3.0], axis=2)
    values = array.astype(np.float64)
    return values / 255.0 if fmt == "R8G8B8A8_UNORM" else values


def _stats(fmt, array):
    values = _decoded(fmt, array)
    finite = np.where(np.isfinite(values), values, np.nan)
    channels = values.shape[2]
    peak = np.nanmax(finite, axis=2)
    return {
        "channels": ["r", "g", "b", "a"][:channels],
        "min": [float(np.nanmin(finite[:, :, c])) for c in range(channels)],
        "max": [float(np.nanmax(finite[:, :, c])) for c in range(channels)],
        "mean": [float(np.nanmean(finite[:, :, c])) for c in range(channels)],
        "nanCount": int(np.isnan(values).sum()),
        "infCount": int(np.isinf(values).sum()),
        "fracAbove1": float(np.mean(peak > 1.0)),
        "fracAbove10": float(np.mean(peak > 10.0)),
        "fracAbove100": float(np.mean(peak > 100.0)),
    }


def _target(directory, name, fmt, frames, color_space):
    dtype, channels, bytes_per_pixel, suffix = FORMATS[fmt]
    height, width = frames[0].shape[:2]
    multi = len(frames) > 1
    files = {}
    for index, array in enumerate(frames):
        frame_suffix = ".%03d" % index if multi else ""
        raw = "%s.%s%s.bin" % (name, suffix, frame_suffix)
        array.astype(dtype).tofile(directory / raw)
        files["raw"] = raw
        if fmt == "R8G8B8A8_UNORM":
            png = "%s%s.png" % (name, frame_suffix)
            Image.fromarray(array.astype(np.uint8), "RGBA").save(directory / png)
            files["png"] = png
    if multi:
        files["pattern"] = "%s.%s.%%03d.bin" % (name, suffix)
    return {
        "name": name,
        "graphName": name,
        "files": files,
        "extent": [width, height],
        "resolution": "Output",
        "mipLevel": 0,
        "aspect": "depth" if fmt == "D32_SFLOAT" else "color",
        "format": {"vk": 0, "name": fmt, "dtype": dtype, "channels": channels, "bytesPerPixel": bytes_per_pixel},
        "rowPitchBytes": width * bytes_per_pixel,
        "colorSpace": color_space,
        "stats": _stats(fmt, frames[-1]),
    }


def final_image(width=WIDTH, height=HEIGHT, shift=0):
    y, x = np.mgrid[0:height, 0:width]
    image = np.zeros((height, width, 4), np.uint8)
    image[:, :, 0] = (x * 255 // max(1, width - 1) + shift) % 256
    image[:, :, 1] = y * 255 // max(1, height - 1)
    image[:, :, 2] = 128
    image[:, :, 3] = 255
    return image


def resolved_image(scale=1.0, offset=0.0):
    """Linear HDR ramp from 0 to 20 across x, with one NaN at (0, 0) red and one +Inf at (1, 0) green."""
    x = np.linspace(0.0, 20.0, WIDTH, dtype=np.float32)
    image = np.zeros((HEIGHT, WIDTH, 4), np.float32)
    image[:, :, 0] = x[None, :] * scale + offset
    image[:, :, 1] = 0.5 * scale + offset
    image[:, :, 2] = 0.25
    image[:, :, 3] = 1.0
    image[0, 0, 0] = np.nan
    image[0, 1, 1] = np.inf
    return image


def write_shot(directory, *, name="shot", index=0, exposure=1.0, view="Off", frames=1, final_size=None,
               warnings=(), requested_by="", after_pass=None):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    final_width, final_height = final_size or (WIDTH, HEIGHT)

    y = np.linspace(0.0, 1.0, HEIGHT, dtype=np.float32)
    depth = np.repeat(y[:, None], WIDTH, axis=1)[:, :, None]
    normal = np.full((HEIGHT, WIDTH, 1), (1023 << 0) | (512 << 10) | (0 << 20) | (3 << 30), np.uint32)

    finals = [final_image(final_width, final_height, shift=i) for i in range(frames)]
    # Every other frame brighter, so a temporal run sees a period-2 pattern.
    resolveds = [resolved_image(scale=exposure, offset=0.5 * (i % 2)) for i in range(frames)]

    targets = [
        _target(directory, "final", "R8G8B8A8_UNORM", finals, "ldr_display"),
        _target(directory, "resolved", "R16G16B16A16_SFLOAT", resolveds, "linear_hdr"),
        _target(directory, "mainDepth", "D32_SFLOAT", [depth], "data"),
        _target(directory, "gbuffer1", "A2B10G10R10_UNORM_PACK32", [normal], "data"),
    ]
    manifest = {"shot": {"index": index, "name": name, "requestedBy": requested_by}}
    if after_pass:
        manifest["capturedAfterPass"] = after_pass
    manifest |= {
        "timing": {"globalFrameIndex": 100 + index, "warmupFrames": 60, "frameInShot": frames - 1,
                   "framesCaptured": frames},
        "resolution": {"renderExtent": [WIDTH, HEIGHT], "outputExtent": [final_width, final_height],
                       "upscaleRatio": 1.0},
        "scene": "Assets/Scenes/test.scene",
        "settings": _settings(exposure, view),
        "targets": targets,
        "warnings": list(warnings),
    }
    # Raw UTF-8 like the engine writes, not \u escapes.
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")
    return directory


def write_capture(root):
    """A session of three shots: raster, bright (other exposure, one warning), multi (3 frames)."""
    root = Path(root)
    shots = {
        "raster": write_shot(root / "000_raster", name="raster", index=0),
        "bright": write_shot(root / "001_bright", name="bright", index=1, exposure=2.0,
                             warnings=["target 'pt_noisy' skipped: layout UNDEFINED"]),
        "multi": write_shot(root / "002_multi", name="multi", index=2, frames=3),
    }
    session = {
        "tool": "FrameCapture",
        "version": 1,
        "startedUtc": "2026-09-13T12:00:00Z",
        "commandLine": "RacingDemo.exe --capture test",
        "build": {"config": "Release", "editor": True, "dlssEnabled": False},
        "gpu": {"name": "Test GPU", "driver": "1.0.0", "vulkanApi": "1.4.0"},
        "scene": "Assets/Scenes/test.scene",
        "shots": [{"index": i, "name": name, "dir": path.name, "status": "ok"}
                  for i, (name, path) in enumerate(shots.items())],
        "status": "ok",
    }
    (root / "session.json").write_text(json.dumps(session, indent=2, ensure_ascii=False), encoding="utf-8")
    return {"root": root, **shots}
