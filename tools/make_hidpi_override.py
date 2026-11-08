#!/usr/bin/env python3
"""Generate a scale-only macOS display override; never replace EDID/timings.

The 2x scale-resolutions data layout follows xzhih/one-key-hidpi's
create_res format: https://github.com/xzhih/one-key-hidpi
This tool only writes a candidate file; it does not install or change a mode.
"""
import argparse
from pathlib import Path
import plistlib
import struct


def identifier(text):
    value = int(text, 0)
    if not 0 <= value <= 0xFFFF:
        raise argparse.ArgumentTypeError('display ID must fit in 16 bits')
    return value


def resolution(text):
    try:
        width, height = map(int, text.lower().split('x'))
    except ValueError:
        raise argparse.ArgumentTypeError('use WIDTHxHEIGHT')
    if not (320 <= width <= 7680 and 200 <= height <= 4320):
        raise argparse.ArgumentTypeError('logical resolution out of range')
    return width, height


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vendor', required=True, type=identifier)
    parser.add_argument('--product', required=True, type=identifier)
    parser.add_argument('--logical', action='append', required=True, type=resolution)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    scales = []
    for width, height in dict.fromkeys(args.logical):
        scales.extend((struct.pack('>III', width * 2, height * 2, 1),
                       struct.pack('>IIII', width * 2, height * 2, 1, 0x200000)))
    payload = {'DisplayVendorID': args.vendor, 'DisplayProductID': args.product,
               'scale-resolutions': scales}
    # Exclusive creation protects an existing override or backup.
    with args.output.open('xb') as stream:
        plistlib.dump(payload, stream)
    print(args.output)


if __name__ == '__main__':
    main()
