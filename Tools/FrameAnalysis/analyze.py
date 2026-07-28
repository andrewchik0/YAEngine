import sys, os, glob
import numpy as np

cap = sys.argv[1]

stages = {}
for line in open(os.path.join(cap, "manifest.txt")):
    parts = line.split()
    if len(parts) == 5:
        name, w, h, fmt, bpp = parts
        stages[name] = (int(w), int(h), int(fmt), int(bpp))

# Keyed on VkFormat, not bytes-per-pixel: R8G8B8A8_UNORM and R16G16_SFLOAT are both 4 bpp
# but neither the element type nor the channel count matches.
FORMATS = {
    97: (np.float16, 4, 1.0),      # R16G16B16A16_SFLOAT
    109: (np.float32, 4, 1.0),     # R32G32B32A32_SFLOAT
    37: (np.uint8, 4, 255.0),      # R8G8B8A8_UNORM
    43: (np.uint8, 4, 255.0),      # R8G8B8A8_SRGB
    83: (np.float16, 2, 1.0),      # R16G16_SFLOAT
    9: (np.uint8, 1, 255.0),       # R8_UNORM
}

def load(name, w, h, fmt):
    if fmt not in FORMATS:
        print("  ! unsupported VkFormat %d for '%s', skipping" % (fmt, name))
        return []

    dtype, channels, scale = FORMATS[fmt]
    files = sorted(glob.glob(os.path.join(cap, name + "_*.bin")))
    frames = []
    for f in files:
        raw = np.fromfile(f, dtype=dtype)
        if scale != 1.0:
            raw = raw.astype(np.float32) / scale
        if raw.size != w * h * channels:
            print("  ! size mismatch in %s: %d vs %d (stale file from an older run?)"
                  % (f, raw.size, w * h * channels))
            continue
        img = raw.reshape(h, w, channels).astype(np.float32)
        frames.append(img[:, :, :3] if channels >= 3 else img)
    return frames

print("=" * 68)
for name, (w, h, fmt, bpp) in stages.items():
    frames = load(name, w, h, fmt)
    if len(frames) < 3:
        print("%-5s: only %d frames, skipping" % (name, len(frames)))
        continue

    a = np.stack(frames)                      # (n, h, w, 3)
    a = np.nan_to_num(a, nan=0.0, posinf=0.0, neginf=0.0)
    lum = a.mean(axis=3)                      # (n, h, w)
    base = float(np.mean(lum)) + 1e-6

    # consecutive-frame difference vs two-frame difference
    d1 = np.mean(np.abs(lum[1:] - lum[:-1]))
    d2 = np.mean(np.abs(lum[2:] - lum[:-2]))

    std = lum.std(axis=0)
    unstable = float(np.mean(std > 0.02 * base))

    print("%-5s  %dx%d  mean_lum=%.4f" % (name, w, h, base))
    print("        frame-to-frame |d| = %.5f  (%.2f%% of mean)" % (d1, 100 * d1 / base))
    print("        two-frame     |d| = %.5f  (%.2f%% of mean)" % (d2, 100 * d2 / base))
    print("        period-2 ratio d2/d1 = %.3f   %s" % (
        d2 / (d1 + 1e-12),
        "<-- ALTERNATING (period 2)" if d2 < 0.5 * d1 else "(no period-2 pattern)"))
    print("        pixels unstable      = %.2f%%" % (100 * unstable))

    # coarse map of where instability lives
    bh, bw = max(1, h // 12), max(1, w // 24)
    tile = std[:bh * 12, :bw * 24].reshape(12, bh, 24, bw).mean(axis=(1, 3))
    tile = tile / (tile.max() + 1e-12)
    ramp = " .:-=+*#%@"
    print("        instability map:")
    for row in tile:
        print("          " + "".join(ramp[min(9, int(v * 9.999))] for v in row))
    print()
