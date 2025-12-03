#!/usr/bin/env python3
"""
spritegen.py

Convert a small 8-bit style PNG into a shape.h for glitch:
- Downsamples to 4 rows high
- Uses ASCII density chars to approximate shading
- Emits SHAPE1..SHAPE4 and SHAPE*_S1..S3 macros

Usage:
  python3 spritegen.py input.png shape_name > shape.h

Then run:
  ./install.sh
  ./glitch
"""

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("This script requires Pillow: pip install pillow", file=sys.stderr)
    sys.exit(1)

# Characters from lightest to darkest
DENSITY_CHARS = " .:-=+*#%@"

def to_ascii_line(pixels, width):
    """
    Convert a 1D list of grayscale pixels [0..255] to a fixed-width ASCII line.
    """
    line_chars = []
    steps = len(DENSITY_CHARS) - 1
    for v in pixels:
        idx = int(v / 255 * steps)
        line_chars.append(DENSITY_CHARS[idx])
    line = "".join(line_chars)
    # Ensure no quotes/backslashes break C strings
    return line.replace("\\", "\\\\").replace('"', '\\"')

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} input.png SHAPE_NAME [width]", file=sys.stderr)
        sys.exit(1)

    input_path = Path(sys.argv[1])
    shape_name = sys.argv[2]
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 36   # fits inside SHAPE_COLS=40
    height = 4  # must match glitch's row count

    if not input_path.is_file():
        print(f"Input file not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    # Load and convert to grayscale
    img = Image.open(input_path).convert("L")
    img = img.resize((width, height), Image.NEAREST)

    rows = []
    for y in range(height):
        row_pixels = [img.getpixel((x, y)) for x in range(width)]
        rows.append(to_ascii_line(row_pixels, width))

    # Emit a shape.h for this sprite
    # We will just use S1/S2/S3 as aliases for now; you can customize later
    print("#ifndef GLITCH_SHAPE_H")
    print("#define GLITCH_SHAPE_H")
    print()
    print(f"/* Auto-generated from {input_path.name} as shape '{shape_name}' */")
    print()

    print(f'#define SHAPE1    "{rows[0]}"')
    print(f'#define SHAPE2    "{rows[1]}"')
    print(f'#define SHAPE3    "{rows[2]}"')
    print(f'#define SHAPE4    "{rows[3]}"')
    print()

    print("#define SHAPE1_S1 SHAPE1")
    print("#define SHAPE1_S2 SHAPE1")
    print("#define SHAPE1_S3 SHAPE1")
    print()
    print("#define SHAPE2_S1 SHAPE2")
    print("#define SHAPE2_S2 SHAPE2")
    print("#define SHAPE2_S3 SHAPE2")
    print()
    print("#define SHAPE3_S1 SHAPE3")
    print("#define SHAPE3_S2 SHAPE3")
    print("#define SHAPE3_S3 SHAPE3")
    print()
    print("#define SHAPE4_S1 SHAPE4")
    print("#define SHAPE4_S2 SHAPE4")
    print("#define SHAPE4_S3 SHAPE4")
    print()
    print("#endif")

if __name__ == "__main__":
    main()
