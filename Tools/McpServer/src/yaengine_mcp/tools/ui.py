"""Editor UI tools: list windows, read the widgets of one, act on them and screenshot the editor."""

import asyncio
import json
from typing import Annotated, Literal, Optional, Union

from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.utilities.types import Image
from pydantic import Field

from ..formatting import capped_lines
from ..images import MAX_LONG_SIDE, SIZE_RULE, encode_for_client
from ..instances import EngineError, InstanceManager
from . import tool

# A request waits on editor frames, and the first one also starts the editor's automation engine.
UI_TIMEOUT = 120.0
DEFAULT_SCREENSHOT_SIZE = 1600

REF_SYNTAX = (
    "Paths use Dear ImGui Test Engine reference syntax: '/'-separated segments starting with the window name, each "
    "the widget's ImGui label exactly as ui_tree lists it (icon glyphs and hidden '##id' suffixes included), '/' "
    "inside a label escaped as '\\/', and '**/' matching any depth, e.g. 'Outliner/**/Ground'."
)


def _quoted(text) -> str:
    return json.dumps(text, ensure_ascii=False)


def format_windows(result: dict) -> str:
    windows = result.get("windows") or []
    lines = [f"{len(windows)} windows (name, then visible or hidden, focused, collapsed, docked)"]
    for window in windows:
        words = ["visible" if window.get("visible") else "hidden"]
        words.extend(word for word in ("focused", "collapsed", "docked") if window.get(word))
        lines.append(f"{_quoted(window.get('name', ''))} {' '.join(words)}")
    return "\n".join(lines)


def format_tree(window: str, result: dict) -> str:
    items = result.get("items") or []
    head = [f"{len(items)} items in {_quoted(window)} (type path [= value] [flags])"]
    lines = []
    for item in items:
        path = item.get("path")
        text = item.get("type", "item") + " "
        text += _quoted(path) if path is not None else f"(no path; label {_quoted(item.get('label', ''))})"

        flags = item.get("flags") or {}
        # A checkbox's value is its checked flag, listed below
        if "value" in item and flags.get("checked") is None:
            text += " = " + _quoted(item["value"])

        words = ["disabled"] if flags.get("disabled") else []
        if flags.get("checked") is not None:
            words.append("checked" if flags["checked"] else "unchecked")
        if flags.get("open") is not None:
            words.append("open" if flags["open"] else "closed")
        if words:
            text += " [" + ", ".join(words) + "]"
        lines.append(text)
    return capped_lines(head, lines, "pass max_depth, or close headers and tree nodes, to list fewer items")


def register(server: MCPServer, manager: InstanceManager) -> None:
    @tool(
        server,
        description=(
            "List the Dear ImGui windows of the attached YAEngine editor: its panels (Outliner, Details, Render "
            "Settings, Viewport, AI Agent, ...), the main menu bar '##MainMenuBar', the dockspace host and every "
            "popup, menu or combo list that is open ('##Popup_...', '##Menu_...', '##Combo_...'). One line per "
            "window: its name in JSON quotes, then visible or hidden (hidden: closed, a background dock tab or "
            "collapsed), and focused, collapsed, docked when they apply. The name is the first segment of the "
            "paths ui_tree lists and ui_do takes."
        ),
    )
    async def ui_windows() -> str:
        return format_windows(await manager.request("ui.windows"))

    @tool(
        server,
        description=(
            "List the widgets one window of the attached editor draws right now, in drawing order. One line per "
            "item: its type (button, checkbox, input for drags, sliders and text fields, combo, header, tree, menu, "
            "menuitem, radio, or item), its path for ui_do in JSON quotes, '= value' with the text the widget "
            "displays where it can be read (a combo's current entry, a drag's number), and flags: disabled, "
            "checked or unchecked, open or closed. Widgets inside a closed header or tree node are not drawn, so "
            "they are not listed until ui_do opens it. " + REF_SYNTAX + " An item without a path cannot be "
            "addressed. A hidden window fails with the ui_do call that shows it."
        ),
    )
    async def ui_tree(
        window: Annotated[str, Field(description="Window name exactly as ui_windows lists it, e.g. Render Settings.")],
        max_depth: Annotated[
            Optional[int], Field(ge=1, description="Only items at most this many ID levels below the window.")
        ] = None,
    ) -> str:
        params = {"window": window}
        if max_depth is not None:
            params["maxDepth"] = max_depth
        return format_tree(window, await manager.request("ui.tree", params, timeout=UI_TIMEOUT))

    @tool(
        server,
        description=(
            "Act on one widget of the attached editor the way a user would, with simulated mouse and keyboard input "
            "that only the editor UI receives: no OS input is sent and the real cursor does not move. Actions: "
            "click (value: left, the default, right, middle or double); check and uncheck (checkboxes, checkable "
            "menu items); set (drags, sliders and text fields: types value, a number or a string, and presses "
            "Enter); open and close (headers, tree nodes, menus); select (a combo entry: path is '<combo "
            "path>/<entry label>', e.g. 'Render Settings/Camera/Tonemapper/AgX'); menu (a menu path starting at the "
            "window holding the menu, e.g. '##MainMenuBar/View/Performance', or an open context menu from "
            "ui_windows such as '##Popup_1A2B3C4D/Delete'). A right click on an item opens its context menu. "
            "Open popups and menus that do not hold the item are closed first, as a click outside them would. "
            + REF_SYNTAX + " A background dock tab holding the item is brought to the front. Items whose label ends "
            "with '...' open native file dialogs that would block the editor and are refused; the error names the "
            "editor action (editor_run) that does the same with a path. Returns what was done; fails when the item "
            "is missing, disabled or of the wrong kind, or with 'busy' while another ui_tree or ui_do or a capture "
            "shot runs. Saving is not undoable: prefer editor_run scene.save with an explicit path over clicking Save."
        ),
    )
    async def ui_do(
        path: Annotated[str, Field(description="Item path from ui_tree, e.g. Render Settings/SSR.")],
        action: Annotated[
            Literal["click", "check", "uncheck", "set", "open", "close", "select", "menu"],
            Field(description="What to do with the item."),
        ],
        value: Annotated[
            Optional[Union[float, str]],
            Field(description="For set: the number or text to type. For click: left, right, middle or double."),
        ] = None,
    ) -> str:
        params = {"path": path, "action": action}
        if value is not None:
            params["value"] = value
        result = await manager.request("ui.do", params, timeout=UI_TIMEOUT)
        detail = result.get("detail", "")
        if not result.get("ok"):
            raise EngineError(f"ui_do did not complete: {detail}")
        return detail

    @tool(
        server,
        description=(
            "Screenshot the attached editor exactly as it was last presented, all panels, menus and popups "
            "included, or one window cropped out of it, and return the image downscaled to max_size on the long "
            f"side ({SIZE_RULE}). The full-size PNG is written to Captures/mcp/ui next to the editor executable, where the newest "
            "64 are kept; its path and size are stated in the result. The 3D viewport shows whatever the viewport "
            "renders; capture_shot gives render targets at full precision."
        ),
    )
    async def ui_screenshot(
        window: Annotated[
            Optional[str], Field(description="Window name from ui_windows. Default: the whole editor window.")
        ] = None,
        max_size: Annotated[
            int, Field(ge=16, le=MAX_LONG_SIDE, description="Long side of the returned image in pixels.")
        ] = DEFAULT_SCREENSHOT_SIZE,
    ) -> list:
        params = {"window": window} if window else {}
        result = await manager.request("ui.screenshot", params, timeout=UI_TIMEOUT)
        path = result.get("path", "")

        def shrink():
            from PIL import Image as PILImage

            with PILImage.open(path) as png:
                return png.size, encode_for_client(png, max_size)

        try:
            full, encoded = await asyncio.to_thread(shrink)
        except OSError as exc:
            raise EngineError(f"cannot read the screenshot {path}: {exc}") from exc

        text = "%s (%dx%d, shown at %s)" % (path, full[0], full[1], encoded.describe())
        return [text, Image(data=encoded.data, format=encoded.format)]
