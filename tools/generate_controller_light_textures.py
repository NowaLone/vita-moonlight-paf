"""Author the small white/alpha source masks used by the PAF RGB overlay.

Standard library only; does not compile CXML or build/package the application.
Geometry is project artwork, not a measured Sony UI reference.
"""
import math
from pathlib import Path
import struct
import zlib

TEXTURES = Path(__file__).resolve().parents[1] / 'cxml' / 'texture'


def write_png(path, width, height, alpha):
    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data +
                struct.pack('>I', zlib.crc32(kind + data)))

    pixels = bytearray()
    for y in range(height):
        pixels.append(0)  # PNG filter: None
        for x in range(width):
            value = round(255 * max(0, min(1, alpha(x, y))))
            pixels.extend((255, 255, 255, value))
    header = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) +
                     chunk(b'IDAT', zlib.compress(pixels, 9)) + chunk(b'IEND', b''))


def hill(x, y):
    x = abs(x + 0.5 - 100)
    bottom_y = 36 - y - 0.5
    if x >= 78:
        distance = math.hypot(x - 78, bottom_y)
        return 0.55 * math.exp(-distance * distance / 18)
    cap = 16 * (1 - (x / 78) ** 2) ** 0.6
    distance = bottom_y - cap
    if distance <= 0:
        return 0.58 + 0.28 * math.exp(-distance * distance / 2)
    return 0.55 * math.exp(-distance * distance / 18)


def edge(x, y):
    along = math.sin(math.pi * (y + 0.5) / 128) ** 1.8
    inward = x / 15
    return along * (0.50 * math.exp(-inward * inward / 0.012) +
                    0.19 * math.exp(-inward * inward / 0.16))


def main():
    write_png(TEXTURES / 'controller_light_ds4.png', 200, 36, hill)
    write_png(TEXTURES / 'controller_light_left.png', 16, 128, edge)
    write_png(TEXTURES / 'controller_light_right.png', 16, 128,
              lambda x, y: edge(15 - x, y))


if __name__ == '__main__':
    main()
