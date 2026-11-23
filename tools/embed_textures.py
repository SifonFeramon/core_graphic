"""Bake sRGB source textures into C++ arrays with mip filtering in linear light."""
from pathlib import Path
import struct
import sys

from PIL import Image

SIZE = 512


def decode(value):
    value /= 255
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def encode(value):
    value = min(1.0, max(0.0, value))
    value = 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055
    return round(value * 255)


def mip_levels(image, size=SIZE):
    """Retain float linear channels throughout the complete mip chain."""
    table = [decode(i) for i in range(256)]
    channels = []
    for source in image.convert("RGB").split():
        channel = Image.new("F", source.size)
        channel.putdata([table[value] for value in source.getdata()])
        channels.append(channel.resize((size, size), Image.Resampling.LANCZOS))
    while True:
        pixels = bytearray()
        for rgb in zip(*(channel.getdata() for channel in channels)):
            pixels.extend((*map(encode, rgb), 255))
        yield bytes(pixels)
        if size == 1:
            break
        size //= 2
        channels = [channel.resize((size, size), Image.Resampling.BOX) for channel in channels]


def main():
    petal, oak, plaster, destination = map(Path, sys.argv[1:])
    with destination.open("w", encoding="ascii") as output:
        output.write(f"#include <cstdint>\nextern const int petalTextureSize = {SIZE};\n")
        for name, source in (("petal", petal), ("oak", oak), ("plaster", plaster)):
            output.write(f"extern const std::uint32_t {name}Pixels[] = {{\n")
            with Image.open(source) as image:
                for pixels in mip_levels(image):
                    for start in range(0, len(pixels), 64):
                        words = struct.iter_unpack("<I", pixels[start:start + 64])
                        output.write(",".join(f"0x{value:08x}" for value, in words) + ",\n")
            output.write("};\n")


if __name__ == "__main__":
    main()
