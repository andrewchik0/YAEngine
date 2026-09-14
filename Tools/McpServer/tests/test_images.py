import base64
import io

import numpy as np
import pytest
from PIL import Image

from synthetic_capture import final_image
from yaengine_mcp.images import MAX_BASE64_CHARS, MAX_LONG_SIDE, MIN_PNG_LONG_SIDE, encode_for_client


def noise(width, height):
    pixels = np.random.default_rng(11).integers(0, 256, (height, width, 3), dtype=np.uint8)
    return Image.fromarray(pixels)


def decoded(encoded):
    return Image.open(io.BytesIO(encoded.data))


def test_smooth_image_stays_png_at_the_requested_size():
    encoded = encode_for_client(Image.fromarray(final_image(2048, 1152), "RGBA"), MAX_LONG_SIDE)

    assert (encoded.format, encoded.size, encoded.quality) == ("png", (2048, 1152), None)
    assert encoded.describe() == "2048x1152"
    assert decoded(encoded).mode == "RGB"


@pytest.mark.parametrize(
    "width, height, fmt",
    [
        # Full-resolution noise: a 16.8 MB PNG in base64, still 4.2 MB at 1024 px, so JPEG.
        (2048, 2048, "jpeg"),
        # Downscaling averages the noise out, so a smaller PNG fits (67 MB and 16.8 MB at full size).
        (4096, 4096, "png"),
        (4096, 1024, "png"),
    ],
)
def test_noise_stays_under_the_base64_budget(width, height, fmt):
    encoded = encode_for_client(noise(width, height), MAX_LONG_SIDE)

    measured = len(base64.b64encode(encoded.data))
    assert measured <= MAX_BASE64_CHARS
    assert encoded.format == fmt
    assert max(encoded.size) >= MIN_PNG_LONG_SIDE
    assert decoded(encoded).size == encoded.size


def test_tiny_budget_shrinks_the_jpeg_until_it_fits():
    budget = 6000
    encoded = encode_for_client(noise(256, 256), 256, max_base64=budget)

    assert len(base64.b64encode(encoded.data)) <= budget
    assert encoded.format == "jpeg" and max(encoded.size) < 256
    assert encoded.describe().endswith("as JPEG quality 60")
