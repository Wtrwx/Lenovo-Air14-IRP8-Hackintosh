#!/usr/bin/env python3
"""Generate a display override for the verified E6F:1416 DisplayID layout."""
import argparse
import plistlib
from pathlib import Path


def correct_checksums(raw):
    """Keep panel identity/timings intact and correct only two checksum bytes."""
    if len(raw) != 256 or raw[:8] != b"\x00\xff\xff\xff\xff\xff\xff\x00":
        raise ValueError("Expected a 256-byte EDID with a valid base header")
    if int.from_bytes(raw[8:10], "big") != 0x0E6F or int.from_bytes(raw[10:12], "little") != 0x1416:
        raise ValueError("This tool only supports the verified E6F:1416 panel")
    if raw[126] != 1 or sum(raw[:128]) % 256 or sum(raw[128:]) % 256:
        raise ValueError("Expected one extension and valid outer EDID block checksums")
    # DisplayID 1.3: extension tag, version, payload length, product type,
    # extension count. The 121-byte payload places its checksum at byte 254.
    if raw[128:133] != bytes([0x70, 0x13, 121, 0, 0]):
        raise ValueError("Unrecognized DisplayID layout; no output generated")
    corrected = bytearray(raw)
    corrected[254] = (-sum(corrected[129:254])) & 0xFF
    corrected[255] = (-sum(corrected[128:255])) & 0xFF
    if sum(corrected[129:255]) % 256 or sum(corrected[128:]) % 256:
        raise ValueError("Checksum verification failed")
    return bytes(corrected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--edid", type=Path, required=True, help="Local raw EDID; never uploaded")
    parser.add_argument("--base-override", type=Path, help="Optional existing plist whose scale settings are retained")
    parser.add_argument("--output", type=Path, required=True, help="New candidate plist; existing files are refused")
    args = parser.parse_args()
    try:
        raw = args.edid.read_bytes()
        corrected = correct_checksums(raw)
        override = plistlib.loads(args.base_override.read_bytes()) if args.base_override else {}
        if not isinstance(override, dict):
            raise ValueError("The base override must be a dictionary")
        for name, expected in [("DisplayVendorID", 0x0E6F), ("DisplayProductID", 0x1416)]:
            if name in override and override[name] != expected:
                raise ValueError("Base override belongs to a different panel")
            override[name] = expected
        override["IODisplayEDID"] = corrected
        candidate = plistlib.dumps(override)
        with args.output.open("xb") as stream:
            stream.write(candidate)
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        parser.exit(1, f"error: {error}\n")
    changes = [i for i, (before, after) in enumerate(zip(raw, corrected)) if before != after]
    print(f"Wrote candidate override; changed EDID offsets: {changes}")
    print("No system display or EFI configuration was changed.")


if __name__ == "__main__":
    main()
