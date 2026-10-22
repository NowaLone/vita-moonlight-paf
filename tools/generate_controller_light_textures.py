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


def light_bar(x, y):
    # DS4 reference: almost straight top, rounded ends and a deeper lower center.
    x = abs(x + 0.5 - 100)
    top = 6
    bottom = 32 - 14 * (min(x / 82, 1) ** 1.6)
    # Rounded box distance with the lower edge following the lens taper.
    dx = x - 77
    dy = abs(y + 0.5 - (top + bottom) / 2) - (bottom - top) / 2 + 5
    distance = math.hypot(max(dx, 0), max(dy, 0)) + min(max(dx, dy), 0) - 5
    return 0.70 * (0.5 - 0.5 * math.tanh(distance / 0.65)) + 0.16 * math.exp(-distance * distance / 4.5)


def edge(x, y):
    along = math.sin(math.pi * (y + 0.5) / 128) ** 1.8
    inward = x / 15
    return along * (0.50 * math.exp(-inward * inward / 0.012) +
                    0.19 * math.exp(-inward * inward / 0.16))


def main():
    write_png(TEXTURES / 'controller_light_ds4.png', 200, 36,
              lambda x, y: light_bar(199 - x, 35 - y))
    write_png(TEXTURES / 'controller_light_left.png', 16, 128, edge)
    write_png(TEXTURES / 'controller_light_right.png', 16, 128,
              lambda x, y: edge(15 - x, y))


if __name__ == '__main__':
    main()
