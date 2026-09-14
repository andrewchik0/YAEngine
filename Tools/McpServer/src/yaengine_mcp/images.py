"""Images returned to MCP clients: RGB, bounded in pixels and in encoded size."""

import io
import math
from dataclasses import dataclass
from typing import Optional

MAX_LONG_SIDE = 2048
# Per returned image, in base64 characters; MCP clients reject or truncate much larger messages.
MAX_BASE64_CHARS = int(3.5 * 1024 * 1024)
# A PNG over the budget shrinks down to this long side before JPEG takes over.
MIN_PNG_LONG_SIDE = 1024
JPEG_QUALITIES = (90, 75, 60)

SIZE_RULE = "smaller, or JPEG, when the encoded image would exceed about 3.5 MB"


def base64_length(byte_count: int) -> int:
    return (byte_count + 2) // 3 * 4


@dataclass(frozen=True)
class EncodedImage:
    data: bytes
    format: str
    size: tuple
    quality: Optional[int] = None

    def describe(self) -> str:
        text = "%dx%d" % tuple(self.size)
        return text if self.quality is None else f"{text} as JPEG quality {self.quality}"


def encode_for_client(image, long_side: int, max_base64: int = MAX_BASE64_CHARS) -> EncodedImage:
    """RGB copy downscaled to long_side, then made smaller and finally JPEG until its base64 fits max_base64.

    Alpha is dropped: several 8-bit targets keep data there.
    """
    from PIL import Image as PILImage

    rgb = image.convert("RGB")

    def resized(side: int):
        shown = rgb.copy()
        shown.thumbnail((side, side), PILImage.Resampling.LANCZOS)
        return shown

    def fits(data: bytes) -> bool:
        return base64_length(len(data)) <= max_base64

    shown = resized(long_side)
    data = _encode(shown, "PNG")
    while not fits(data) and max(shown.size) > MIN_PNG_LONG_SIDE:
        shown = resized(max(MIN_PNG_LONG_SIDE, _smaller_side(shown, data, max_base64)))
        data = _encode(shown, "PNG")
    if fits(data):
        return EncodedImage(data, "png", shown.size)

    for quality in JPEG_QUALITIES:
        data = _encode(shown, "JPEG", quality=quality)
        if fits(data):
            return EncodedImage(data, "jpeg", shown.size, quality)
    while not fits(data) and max(shown.size) > 1:
        shown = resized(_smaller_side(shown, data, max_base64))
        data = _encode(shown, "JPEG", quality=quality)
    return EncodedImage(data, "jpeg", shown.size, quality)


def _smaller_side(shown, data: bytes, max_base64: int) -> int:
    # The encoded size grows roughly with the pixel count, so the side scales with its square root.
    current = max(shown.size)
    factor = math.sqrt(max_base64 / base64_length(len(data))) * 0.95
    return max(1, min(current - 1, int(current * factor)))


def _encode(image, fmt: str, **options) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format=fmt, **options)
    return buffer.getvalue()
