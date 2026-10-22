"""Pack the supplied 48x72 portrait into native PSX RGB5551, without resizing."""

import argparse
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with Image.open(args.source) as image:
        if image.size != (48, 72):
            parser.error("the blessing portrait must be exactly 48x72 pixels")
        rgba = image.convert("RGBA")
        pixels = (rgba.get_flattened_data() if hasattr(rgba, "get_flattened_data")
                  else rgba.getdata())
        words = [
            ((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000)
            if a >= 128 else 0
            for r, g, b, a in pixels
        ]
    rows = [", ".join(f"0x{word:04X}" for word in words[i:i + 12]) + ","
            for i in range(0, len(words), 12)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("// clang-format off\n" + "\n".join(rows) + "\n")


if __name__ == "__main__":
    main()
