"""
Generate a BRDF LUT (split-sum integration) PNG for IBL specular.
Uses GPU compute shader via moderngl for fast execution.

Matches the engine's PBR functions in Core/Shaders/pbr.glsl:
  - GGX importance sampling with alpha = roughness^2
  - Geometry Schlick-GGX with k = alpha / 2 (IBL variant)
  - Fresnel Schlick decomposition: scale = sum(weight * (1-Fc)), bias = sum(weight * Fc)

Output: 512x512 PNG, R = scale, G = bias, linear bytes [0..255].
X axis = NdotV (0..1), Y axis = roughness (0..1).
Vulkan orientation: row 0 (top) = UV.y=0 = roughness 0.
"""

import struct
import zlib
import time
import moderngl

LUT_SIZE = 512
NUM_SAMPLES = 1024

COMPUTE_SHADER = """
#version 430

layout(local_size_x = 16, local_size_y = 16) in;
layout(std430, binding = 0) buffer OutputBuffer { vec2 data[]; };

const int LUT_SIZE = """ + str(LUT_SIZE) + """;
const int NUM_SAMPLES = """ + str(NUM_SAMPLES) + """;
const float PI = 3.14159265359;

float radicalInverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), radicalInverse(i));
}

float geometrySchlickGGX(float alpha, float cosTheta)
{
    float k = alpha / 2.0;
    float denom = max(cosTheta, 0.0) * (1.0 - k) + k;
    return max(cosTheta, 0.0) / max(denom, 1e-7);
}

float geometrySmith(float alpha, float NdotV, float NdotL)
{
    return geometrySchlickGGX(alpha, NdotV) * geometrySchlickGGX(alpha, NdotL);
}

vec2 integrateBRDF(float NdotV, float roughness)
{
    NdotV = max(NdotV, 0.001);

    float alpha = roughness * roughness;
    alpha = max(alpha, 1e-4);

    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

    float scale = 0.0;
    float bias = 0.0;

    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        vec2 Xi = hammersley(uint(i), uint(NUM_SAMPLES));

        float a = alpha;
        float phi = 2.0 * PI * Xi.x;
        float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

        vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

        float VdotH = dot(V, H);
        if (VdotH <= 0.0) continue;

        vec3 L = 2.0 * VdotH * H - V;
        float NdotL = L.z;
        float NdotH = H.z;

        if (NdotL <= 0.0) continue;

        float G = geometrySmith(alpha, NdotV, NdotL);
        float G_Vis = G * VdotH / max(NdotH * NdotV, 1e-7);

        float t = 1.0 - VdotH;
        float t2 = t * t;
        float Fc = t2 * t2 * t;

        scale += G_Vis * (1.0 - Fc);
        bias += G_Vis * Fc;
    }

    return vec2(scale, bias) / float(NUM_SAMPLES);
}

void main()
{
    uvec2 pos = gl_GlobalInvocationID.xy;
    if (pos.x >= LUT_SIZE || pos.y >= LUT_SIZE) return;

    float NdotV = (float(pos.x) + 0.5) / float(LUT_SIZE);
    // Vulkan: no Y-flip, row 0 (top) = UV.y=0 = roughness 0
    float roughness = (float(pos.y) + 0.5) / float(LUT_SIZE);

    vec2 result = integrateBRDF(NdotV, roughness);
    data[pos.y * LUT_SIZE + pos.x] = result;
}
"""


def write_png(filename, width, height, pixels):
    """Write an RGB PNG file. pixels is bytes of (r, g, b) per pixel."""

    def chunk(chunk_type, data):
        c = chunk_type + data
        crc = zlib.crc32(c) & 0xFFFFFFFF
        return struct.pack('>I', len(data)) + c + struct.pack('>I', crc)

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: None
        offset = y * width * 3
        raw.extend(pixels[offset:offset + width * 3])

    signature = b'\x89PNG\r\n\x1a\n'
    ihdr_data = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    ihdr = chunk(b'IHDR', ihdr_data)
    idat = chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    iend = chunk(b'IEND', b'')

    with open(filename, 'wb') as f:
        f.write(signature + ihdr + idat + iend)


def main():
    print(f"Generating {LUT_SIZE}x{LUT_SIZE} BRDF LUT ({NUM_SAMPLES} samples) via GPU compute...")
    t0 = time.time()

    ctx = moderngl.create_standalone_context()
    print(f"  GPU: {ctx.info['GL_RENDERER']}")

    buf = ctx.buffer(reserve=LUT_SIZE * LUT_SIZE * 8)  # vec2 = 2 floats = 8 bytes
    buf.bind_to_storage_buffer(0)

    compute = ctx.compute_shader(COMPUTE_SHADER)
    groups = (LUT_SIZE + 15) // 16
    compute.run(groups, groups)
    ctx.finish()

    elapsed = time.time() - t0
    print(f"  GPU compute: {elapsed:.3f}s")

    raw_data = struct.unpack(f'{LUT_SIZE * LUT_SIZE * 2}f', buf.read())

    pixels = bytearray(LUT_SIZE * LUT_SIZE * 3)
    for i in range(LUT_SIZE * LUT_SIZE):
        s = raw_data[i * 2 + 0]
        b = raw_data[i * 2 + 1]
        pixels[i * 3 + 0] = max(0, min(255, int(s * 255.0 + 0.5)))
        pixels[i * 3 + 1] = max(0, min(255, int(b * 255.0 + 0.5)))
        pixels[i * 3 + 2] = 0

    output_path = "Core/Assets/Textures/BRDFLut.png"
    write_png(output_path, LUT_SIZE, LUT_SIZE, pixels)
    print(f"  Saved to {output_path}")

    # Verification at roughness=0 (row 0)
    print("\nVerification at roughness~0 (row 0):")
    for test_x in [0, 51, 128, 256, 384, 511]:
        idx = test_x
        r = pixels[idx * 3 + 0]
        g = pixels[idx * 3 + 1]
        ndv = (test_x + 0.5) / LUT_SIZE
        print(f"  NdotV={ndv:.3f}: R={r}, G={g}, sum={r + g} (expected ~255)")

    total = time.time() - t0
    print(f"\nTotal: {total:.3f}s")


if __name__ == "__main__":
    main()
