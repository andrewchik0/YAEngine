"""Frame capture tools: list capturable targets, take a shot, then view and measure what it wrote."""

import asyncio
import importlib.util
import logging
import os
from typing import Annotated, Literal, Optional

from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.utilities.types import Image
from pydantic import Field

from .. import paths
from ..images import MAX_LONG_SIDE, SIZE_RULE, encode_for_client
from ..instances import EngineError, InstanceManager
from . import tool

log = logging.getLogger(__name__)

ANALYSIS_SCRIPT = paths.REPO_ROOT / "Tools" / "FrameAnalysis" / "capture.py"
# A shot waits inside the editor for the extent to settle, the warmup and any accumulation.
SHOT_TIMEOUT = 900.0
SHOT_IMAGE_LONG_SIDE = 1280
MAX_SHOT_IMAGES = 4

_analysis_module = None


def analysis():
    """Tools/FrameAnalysis/capture.py of the repository this server belongs to."""
    global _analysis_module
    if _analysis_module is None:
        if not ANALYSIS_SCRIPT.is_file():
            raise EngineError(f"{ANALYSIS_SCRIPT} is missing; capture analysis needs it.")
        spec = importlib.util.spec_from_file_location("yaengine_frame_analysis", ANALYSIS_SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        _analysis_module = module
    return _analysis_module


async def _analyse(work):
    """Runs file and numpy work off the event loop; problems with the files on disk become tool errors."""
    module = analysis()
    try:
        return await asyncio.to_thread(work, module)
    except module.CaptureError as exc:
        raise EngineError(str(exc)) from exc
    except OSError as exc:
        raise EngineError(f"cannot read the capture: {exc}") from exc
    except KeyError as exc:
        raise EngineError(f"the capture files lack the expected entry {exc}") from exc
    except ValueError as exc:
        raise EngineError(f"cannot read the capture: {exc}") from exc


def _parse_rect(text: Optional[str]) -> Optional[tuple]:
    if not text:
        return None
    parts = text.split(",")
    try:
        if len(parts) != 4:
            raise ValueError
        return tuple(int(part) for part in parts)
    except ValueError:
        raise EngineError(f"rect must be four integers x,y,w,h; got {text!r}") from None


def _number(value) -> str:
    return "null" if value is None else "%.4g" % value


def _channels(values) -> str:
    return "[" + ",".join(_number(value) for value in values or []) + "]"


def format_target_stats(target: dict) -> str:
    """One line from the statistics the engine stored in the manifest."""
    stats = target.get("stats") or {}
    return "  %s %s %s %s min=%s max=%s mean=%s nan=%s inf=%s over1=%s" % (
        target["name"], target["format"]["name"], "x".join(str(v) for v in target["extent"]),
        target.get("colorSpace", "?"), _channels(stats.get("min")), _channels(stats.get("max")),
        _channels(stats.get("mean")), stats.get("nanCount", 0), stats.get("infCount", 0),
        _number(stats.get("fracAbove1", 0.0)))


def format_targets(result: dict) -> str:
    lines = ["targets (name format extent resolution):"]
    for target in result.get("targets") or []:
        extent = "x".join(str(v) for v in target.get("extent") or [])
        unmanaged = "" if target.get("managed", True) else " imported"
        lines.append(f"  {target['name']} {target['format']} {extent} {target['resolution']}{unmanaged}")
    lines.append("aliases (name -> graph resources it captures right now):")
    for alias in result.get("aliases") or []:
        resolved = ",".join(alias.get("resolvesTo") or []) or "(nothing in this state)"
        lines.append(f"  {alias['name']} -> {resolved}")
    lines.append("debug views (view=<slug> or view=<id>):")
    for view in result.get("debugViews") or []:
        lines.append(f"  {view['id']} {view['slug']} ({view['name']})")
    passes = result.get("passes") or []
    if not passes:
        lines.append("passes: (none)")
        return "\n".join(lines)
    lines.append("passes in execution order (after=<name or alt name> in a shot captures right after one):")
    for render_pass in passes:
        outputs = ",".join([*(render_pass.get("colorOutputs") or []), *(render_pass.get("storageOutputs") or [])])
        if render_pass.get("depthOutput"):
            outputs = ",".join(filter(None, [outputs, "depth:" + render_pass["depthOutput"]]))
        alt = f" [{render_pass['altName']}]" if render_pass.get("altName") else ""
        disabled = "" if render_pass.get("enabled", True) else " (disabled now)"
        lines.append(f"  {render_pass['executionIndex']} {render_pass['name']}{alt} -> {outputs or '(no image outputs)'}{disabled}")
    return "\n".join(lines)


def _size(size) -> str:
    return "%dx%d" % tuple(size)


def _shot_report(module, directory: str) -> tuple:
    from PIL import Image as PILImage

    try:
        if not directory:
            raise module.CaptureError("no directory")
        manifest = module.load_manifest(directory)
    except module.CaptureError:
        return ["no manifest was written, so there is nothing to show"], []

    lines = []
    settings = manifest.get("settings") or {}
    resolution = manifest.get("resolution") or {}
    frames = (manifest.get("timing") or {}).get("framesCaptured", 1)
    after = " after=%s" % manifest["capturedAfterPass"] if "capturedAfterPass" in manifest else ""
    lines.append("shot %s: path=%s aa=%s view=%s exposure=%s render=%s output=%s frames=%s%s" % (
        manifest["shot"]["name"],
        (settings.get("renderPath") or {}).get("effective"),
        (settings.get("antialiasing") or {}).get("effective"),
        (settings.get("debugView") or {}).get("name"),
        _number(settings.get("exposure")),
        _size(resolution.get("renderExtent") or [0, 0]),
        _size(resolution.get("outputExtent") or [0, 0]),
        frames,
        after))
    lines.append("targets%s:" % (" (stats of the last frame)" if frames > 1 else ""))
    lines.extend(format_target_stats(target) for target in manifest["targets"])

    viewable = [t for t in manifest["targets"] if (t.get("files") or {}).get("png")]
    viewable.sort(key=lambda t: t["name"] != "final")
    images = []
    for target in viewable[:MAX_SHOT_IMAGES]:
        path = os.path.join(directory, target["files"]["png"])
        with PILImage.open(path) as png:
            full = png.size
            encoded = encode_for_client(png, SHOT_IMAGE_LONG_SIDE)
        lines.append(f"image {target['name']}: {path} ({_size(full)}, shown at {encoded.describe()})")
        images.append(Image(data=encoded.data, format=encoded.format))
    skipped = [t["name"] for t in viewable[MAX_SHOT_IMAGES:]]
    if skipped:
        lines.append("not shown (use capture_view): " + ", ".join(skipped))
    return lines, images


def register(server: MCPServer, manager: InstanceManager) -> None:
    @tool(
        server,
        description=(
            "List what the attached YAEngine editor can capture: every render graph target with its format, "
            "extent and resolution class (render or output), the aliases and groups with the resources they "
            "resolve to right now (final, resolved, prev_resolved, taa, pt_noisy, pt_accum, gbuffer, pt, default, "
            "all), the debug views usable as view=<slug>, and the render graph passes in execution order with the "
            "images each one writes and whether it runs in the current state (usable as after=<pass> in capture_shot)."
        ),
    )
    async def capture_targets() -> str:
        return format_targets(await manager.request("capture.targets"))

    @tool(
        server,
        description=(
            "Capture one frame of the attached YAEngine editor to disk and show it. The editor stays interactive; "
            "every setting the shot overrides (render settings, debug view, camera pose, gizmos) is restored "
            "afterwards. 'shot' uses the --shot grammar: semicolon separated key=value pairs, all optional: "
            "name, targets (comma separated names or aliases; default final,resolved), after (a render graph pass "
            "name or alt name from capture_targets: dump right after that pass instead of at the end of the frame; "
            "targets then default to that pass's outputs and may only name them; fails when the pass does not run "
            "in the current state), path (raster|pt), "
            "aa (none|taa|dlaa|dlss-quality|dlss-balanced|dlss-perf|dlss-ultraperf), view (debug view slug or id), "
            "bounces (1..8), clamp, devresolve (0|1), accum (wait for N path traced samples), exposure, "
            "autoexposure (0|1), tonemap (aces|agx), bloom (0|1), warmup (frames, default 60), frames (consecutive "
            "frames to dump, default 1), camera (x,y,z), look (x,y,z look-at point) or yaw/pitch (degrees). "
            "Example: 'view=normals;exposure=2;camera=12,3,-40;look=20,0.5,-30'. Returns status (ok, partial or "
            "failed), warnings, the absolute shot directory, one stats line per target and the images of 'final' "
            f"and other 8-bit targets (RGB, downscaled to 1280 px on the long side, {SIZE_RULE}). Shots go to "
            "Captures/mcp/NNN_<name> next to the editor executable; before each shot all but the newest 64 shot "
            "directories there are deleted. Fails with 'busy' "
            "while another shot or a command line capture session runs. Can take minutes with a high accum: this "
            f"call waits up to {SHOT_TIMEOUT:g} s, and when it gives up the shot keeps running in the editor, so wait "
            "until engine_status reports captureBusy false before starting another shot."
        ),
    )
    async def capture_shot(
        shot: Annotated[str, Field(description="Shot recipe in the --shot grammar; empty captures the current view.")] = "",
        name: Annotated[
            Optional[str],
            Field(description="Directory name suffix; overrides a name= key. Characters other than ASCII letters, digits, - and _ become _ (one _ per byte of a non-ASCII character)."),
        ] = None,
    ) -> list:
        params = {"shot": shot}
        if name:
            params["name"] = name
        result = await manager.request("capture.shot", params, timeout=SHOT_TIMEOUT)

        directory = result.get("directory", "")
        lines = [f"status={result.get('status')} directory={directory}"]
        lines.extend(f"warning: {warning}" for warning in result.get("warnings") or [])
        images = []
        try:
            report, images = await _analyse(lambda module: _shot_report(module, directory))
            lines.extend(report)
        except Exception as exc:
            # The shot is finished either way; its status and directory stay useful without the analysis.
            if not isinstance(exc, EngineError):
                log.exception("analysing the shot in %s failed", directory)
            lines.append(f"cannot analyse the shot: {exc}")
        return ["\n".join(lines), *images]

    @tool(
        server,
        description=(
            "Render one captured target of a shot directory as a viewable PNG and return it. Float targets need a "
            "mapping: linear (clamp to 0..1), auto (scale by the 99th percentile), log (log10 of the brightest "
            "channel over -4..4, the engine's HDR Magnitude scale) or falsecolor (one hue per decade). 'exposure' "
            "multiplies the values first. The full-size PNG is written into the shot directory and its path is "
            f"stated in the result; the returned image is downscaled to max_size on the long side ({SIZE_RULE})."
        ),
    )
    async def capture_view(
        shot_dir: Annotated[str, Field(description="Absolute path of a shot directory, as returned by capture_shot.")],
        target: Annotated[Optional[str], Field(description="Target name from the manifest. Default: the first target.")] = None,
        map: Annotated[Literal["linear", "log", "auto", "falsecolor"], Field(description="Value to color mapping.")] = "auto",
        exposure: Annotated[float, Field(description="Multiplier applied before the mapping.")] = 1.0,
        rect: Annotated[Optional[str], Field(description="Crop x,y,w,h in pixels before mapping.")] = None,
        max_size: Annotated[
            int, Field(ge=16, le=MAX_LONG_SIDE, description="Long side of the returned image in pixels.")
        ] = 1280,
    ) -> list:
        crop = _parse_rect(rect)

        def work(module):
            from PIL import Image as PILImage

            manifest = module.load_manifest(shot_dir)
            chosen = module.find_target(manifest, target)
            image = module.crop(module.load_target(shot_dir, chosen), crop)
            pixels = PILImage.fromarray(module.preview_pixels(image, map, exposure))
            suffix = "" if crop is None else ".rect_%d_%d_%d_%d" % crop
            path = os.path.join(shot_dir, "%s.%s%s.preview.png" % (chosen["name"], map, suffix))
            pixels.save(path)
            encoded = encode_for_client(pixels, max_size)
            text = "%s %s exposure=%g%s -> %s (%s, shown at %s)" % (
                chosen["name"], map, exposure, "" if crop is None else " rect=%s" % rect, path,
                _size(pixels.size), encoded.describe())
            return text, encoded

        text, encoded = await _analyse(work)
        return [text, Image(data=encoded.data, format=encoded.format)]

    @tool(
        server,
        description=(
            "Per-channel statistics of one captured target, computed from the raw file: pixel, NaN and Inf "
            "counts, min, 1st/50th percentile, mean, 99th/99.9th percentile, max, and the fraction of pixels whose "
            "brightest channel exceeds 1, 10 and 100. Depth is reversed-Z (1 near, 0 far)."
        ),
    )
    async def capture_stats(
        shot_dir: Annotated[str, Field(description="Absolute path of a shot directory, as returned by capture_shot.")],
        target: Annotated[Optional[str], Field(description="Target name from the manifest. Default: the first target.")] = None,
        rect: Annotated[Optional[str], Field(description="Restrict to x,y,w,h in pixels.")] = None,
    ) -> str:
        _parse_rect(rect)
        return await _analyse(lambda module: "\n".join(module.stats_lines(shot_dir, target, rect)))

    @tool(
        server,
        description=(
            "Compare two shot directories: the render settings that differ between their manifests, then the "
            "per-channel difference (B minus A) of one target with a coarse map of where it lives. Targets of "
            "different extents are not diffed."
        ),
    )
    async def capture_diff(
        shot_a: Annotated[str, Field(description="Absolute path of the first shot directory.")],
        shot_b: Annotated[str, Field(description="Absolute path of the second shot directory.")],
        target: Annotated[Optional[str], Field(description="Target to diff. Default: the first target of shot_a.")] = None,
    ) -> str:
        return await _analyse(lambda module: "\n".join(module.diff_lines(shot_a, shot_b, target)))
