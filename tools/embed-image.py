#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import sys
from PIL import Image, ImageOps

with Image.open(sys.argv[1]) as source:
    image = ImageOps.exif_transpose(source).convert("RGBA")
    image.thumbnail((128, 80), Image.Resampling.LANCZOS)
    pixels = image.tobytes()
with open(sys.argv[2], "w", encoding="ascii") as out:
    out.write("/* Generated image; no decoder is needed at runtime. */\n")
    out.write(f"#define LOGO_WIDTH {image.width}\n#define LOGO_HEIGHT {image.height}\n")
    out.write("static const unsigned char logo_pixels[] = {\n")
    for start in range(0, len(pixels), 24):
        out.write(",".join(str(value) for value in pixels[start:start + 24]) + ",\n")
    out.write("};\n")
