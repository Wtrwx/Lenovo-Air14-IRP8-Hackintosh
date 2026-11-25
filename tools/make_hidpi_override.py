#!/usr/bin/env python3
"""Generate a scale-only macOS display override; never replace EDID/timings.

Emits zero-collision 16-byte HiDPI (0x200000) scale-resolutions entries by
default, plus optional non-colliding 8-byte LoDPI entries (--lodpi). Avoids
emitting a LoDPI entry at the same backing width/height as a HiDPI entry
(which causes IOFramebuffer/WindowServer to hide the HiDPI mode) and warns
above 32 entries to avoid macOS MPDisplayMgr O(N^2) System Settings freezes.
"""
import argparse
from pathlib import Path
import plistlib
import struct
import sys


def identifier(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFFFF:
        raise argparse.ArgumentTypeError('display ID must fit in 16 bits')
    return value


def resolution(text: str) -> tuple[int, int]:
    try:
        width, height = map(int, text.lower().split('x'))
    except ValueError:
        raise argparse.ArgumentTypeError('use WIDTHxHEIGHT')
    if not (320 <= width <= 7680 and 200 <= height <= 4320):
        raise argparse.ArgumentTypeError('resolution out of range')
    return width, height


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vendor', required=True, type=identifier)
    parser.add_argument('--product', required=True, type=identifier)
    parser.add_argument('--logical', action='append', required=True, type=resolution,
                        help='Logical HiDPI resolution WxH (creates 2W x 2H backing entry with 0x200000 flag)')
    parser.add_argument('--lodpi', action='append', default=[], type=resolution,
                        help='Optional non-HiDPI mode WxH (must not equal any 2*logical backing resolution)')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()

    hidpi_backing = {(w * 2, h * 2): (w, h) for w, h in dict.fromkeys(args.logical)}
    lodpi_list = list(dict.fromkeys(args.lodpi))
    for w, h in lodpi_list:
        if (w, h) in hidpi_backing:
            lw, lh = hidpi_backing[(w, h)]
            raise SystemExit(
                f'Refusing colliding LoDPI entry {w}x{h}: collides with HiDPI logical {lw}x{lh} '
                f'(backing {w}x{h}); IOFramebuffer would strip 0x00200000 and break HiDPI.'
            )

    scales = [struct.pack('>II', w, h) for w, h in lodpi_list]
    for w, h in dict.fromkeys(args.logical):
        scales.append(struct.pack('>IIII', w * 2, h * 2, 1, 0x200000))

    if len(scales) > 32:
        print(
            f'WARNING: {len(scales)} scale-resolutions entries exceeds 32; macOS MPDisplayMgr '
            'performs O(N^2) WindowServer IPCs and may freeze System Settings -> Displays.',
            file=sys.stderr,
        )

    payload = {
        'DisplayVendorID': args.vendor,
        'DisplayProductID': args.product,
        'scale-resolutions': scales,
    }
    # Exclusive creation protects an existing override or backup.
    with args.output.open('xb') as stream:
        plistlib.dump(payload, stream)
    print(args.output)


if __name__ == '__main__':
    main()
