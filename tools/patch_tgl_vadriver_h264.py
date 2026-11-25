#!/usr/bin/env python3
"""Patch stock WhateverXe-Graphics v0.4.4 AppleIntelTGLGraphicsVADriver.bundle (macOS Tahoe 25G332).

Modes:
  - hw-encode-fix (default): Keeps low-latency VDENC (feature=0x400) disabled
    to avoid VCS0 hangs while fixing MEDIA_SURFACE_STATE.DW2 double-pitching at
    0x5f048 so standard H.264 hardware encode (feature=0x4000) works cleanly.
  - sw-fallback: Keeps feature=0x400 disabled and additionally disables
    standard H.264 hardware encode (feature=0x4000) so VideoToolbox uses
    software H.264 encoding while preserving H.264 decode and HEVC.
"""

import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys


STOCK_VADRIVER_SHA256 = "8d4303ea9c502191ebd725a31af25277cad6105857b1ba21a6500dff9c47437a"
STOCK_TGLVA_SHA256 = "18e32dd5fd02d192ab3e13961239f304e1d7e1bf9855c6b8bf1137aa51cb7a4e"
EXPECTED_VADRIVER_UUID = "00204000-0263-4092-B483-CC2544D26350"
EXPECTED_TGLVA_UUID = "733A6AAB-D421-3F07-8CC4-93F5E5A60221"

VA1_LL_GUARD_PATCHES = [
    (
        "Servicing caps converter root-count guard 35 -> 34",
        0xa91,
        bytes.fromhex("837f1023"),
        bytes.fromhex("837f1022"),
    ),
    (
        "Servicing caps converter sizing loop 35 -> 34",
        0xaa3,
        bytes.fromhex("4883fa23"),
        bytes.fromhex("4883fa22"),
    ),
    (
        "Servicing caps converter copy loop 35 -> 34",
        0xb17,
        bytes.fromhex("4883fe23"),
        bytes.fromhex("4883fe22"),
    ),
    (
        "CreateContext(kind=0x20, feature=0x400) reject",
        0x14382,
        bytes.fromhex("483d000400000f8479030000"),
        bytes.fromhex("483d000400000f84be0b0000"),
    ),
    (
        "CapabilityBuilder calloc count 0x23 -> 0x22",
        0x2bc01,
        bytes.fromhex("bf23000000"),
        bytes.fromhex("bf22000000"),
    ),
    (
        "CapabilityBuilder rootCount 0x23 -> 0x22",
        0x2bc26,
        bytes.fromhex("c7431023000000"),
        bytes.fromhex("c7431022000000"),
    ),
    (
        "CapabilityBuilder Group 1 head -> trampoline 0x1100",
        0x2bd0c,
        bytes.fromhex("4d893424498b37"),
        bytes.fromhex("e9ef53fdff6690"),
    ),
    (
        "Cave trampoline at 0x1100 skipping index 7 (0x78c590)",
        0x1100,
        bytes(25),
        bytes.fromhex("4183fd0775074983c71041ffc54d893424498b37e9faab0200"),
    ),
]

DW2_PITCH_CONTEXT_OFFSET = 0x5f040
DW2_PITCH_CONTEXT_EXPECTED = bytes.fromhex("8b510825ffff0300c1e00381e20700e0ff09c2895108")
DW2_PITCH_PATCH_OFFSET = 0x5f048
DW2_PITCH_EXPECTED = bytes.fromhex("c1e003")
DW2_PITCH_REPLACEMENT = bytes.fromhex("31c090")


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(cmd: list[str]) -> str:
    proc = subprocess.run(cmd, text=True, capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError(
            f"Command failed ({proc.returncode}): {' '.join(cmd)}\n{proc.stdout}\n{proc.stderr}"
        )
    return proc.stdout.strip()


def get_uuid(binary: Path) -> str:
    out = run(["/usr/bin/dwarfdump", "--uuid", str(binary)])
    for line in out.splitlines():
        if "UUID:" in line:
            return line.split("UUID:", 1)[1].strip().split()[0].upper()
    raise RuntimeError(f"Unable to parse UUID from: {out}")


def linkedit_fileoff(data: bytes) -> int:
    magic, _cpu, _sub, _ft, ncmd, _scmds, _flags, _res = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != 0xfeedfacf:
        raise RuntimeError(f"Unexpected Mach-O magic: {magic:#x}")
    cursor = 32
    for _ in range(ncmd):
        cmd, length = struct.unpack_from("<II", data, cursor)
        if cmd == 0x19:  # LC_SEGMENT_64
            name, _va, _msz, fo, _fsz = struct.unpack_from("<16sQQQQ", data, cursor + 8)
            if name.rstrip(b"\0") == b"__LINKEDIT":
                return fo
        cursor += length
    raise RuntimeError("__LINKEDIT segment not found")


def near_branch(opcode: str, address: int, target: int) -> bytes:
    return bytes.fromhex(opcode) + struct.pack("<i", target - address - len(opcode) // 2 - 4)


def apply_edit(buf: bytearray, label: str, offset: int, expected: bytes, replacement: bytes) -> None:
    if len(expected) != len(replacement):
        raise ValueError(f"[{label}] length mismatch")
    actual = bytes(buf[offset : offset + len(expected)])
    if actual != expected:
        raise RuntimeError(
            f"[{label}] @ {hex(offset)} mismatch: got {actual.hex()}, expected {expected.hex()}"
        )
    buf[offset : offset + len(replacement)] = replacement
    print(f"PATCHED {label} @ {hex(offset)}: {expected.hex()} -> {replacement.hex()}")


def apply_sw_fallback_delta(buf: bytearray) -> None:
    roots = bytearray.fromhex("4183fd0c")  # cmp $12, %r13d
    roots.extend(near_branch("0f84", 0x1100 + len(roots), 0x2bd6b))
    roots.extend(bytes.fromhex("4183fd0775074983c71041ffc54d893424498b37"))
    roots.extend(near_branch("e9", 0x1100 + len(roots), 0x2bd13))
    old_roots = bytes.fromhex("4183fd0775074983c71041ffc54d893424498b37e9faab0200")
    apply_edit(buf, "CapabilityBuilder skip indices 7 and 12", 0x1100,
               old_roots + bytes(len(roots) - len(old_roots)), bytes(roots))

    gate_at = 0x1130
    gate = bytearray.fromhex("498b4608")  # mov 8(%r14), %rax
    for feature in (0x400, 0x4000):
        gate.extend(bytes.fromhex("483d") + struct.pack("<I", feature))
        gate.extend(near_branch("0f84", gate_at + len(gate), 0x14f4c))
    gate.extend(near_branch("e9", gate_at + len(gate), 0x1438e))
    if 0x1100 + len(roots) > gate_at or gate_at + len(gate) > 0x1160:
        raise RuntimeError("Fallback gate outside pinned text cave")
    apply_edit(buf, "CreateContext H.264 0x400/0x4000 reject cave", gate_at, bytes(len(gate)), bytes(gate))
    apply_edit(
        buf,
        "CreateContext H.264 branch -> reject cave",
        0x1437e,
        bytes.fromhex("498b4608483d000400000f84be0b0000"),
        near_branch("e9", 0x1437e, gate_at) + b"\x90" * 11,
    )
    for offset in (0xa94, 0xaa6, 0xb1a, 0x2bc02, 0x2bc29):
        apply_edit(buf, f"Root count 34 -> 33 @ {hex(offset)}", offset, b"\x22", b"\x21")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", type=Path, required=True, help="Stock v0.4.4 AppleIntelTGLGraphicsVADriver.bundle")
    parser.add_argument("--dst", type=Path, required=True, help="Output bundle path")
    parser.add_argument(
        "--mode",
        choices=("hw-encode-fix", "sw-fallback"),
        default="hw-encode-fix",
        help="Patch mode (default: hw-encode-fix)",
    )
    args = parser.parse_args()

    src_va = args.src / "Contents/MacOS/AppleIntelTGLGraphicsVADriver"
    src_tglva = args.src / "Contents/MacOS/TGLVA"
    if sha256_file(src_va) != STOCK_VADRIVER_SHA256:
        raise RuntimeError(f"Source VADriver SHA-256 mismatch: {sha256_file(src_va)}")
    if sha256_file(src_tglva) != STOCK_TGLVA_SHA256:
        raise RuntimeError(f"Source TGLVA SHA-256 mismatch: {sha256_file(src_tglva)}")
    if get_uuid(src_va) != EXPECTED_VADRIVER_UUID:
        raise RuntimeError(f"Unexpected source VADriver UUID: {get_uuid(src_va)}")
    if get_uuid(src_tglva) != EXPECTED_TGLVA_UUID:
        raise RuntimeError(f"Unexpected source TGLVA UUID: {get_uuid(src_tglva)}")

    if args.dst.exists():
        shutil.rmtree(args.dst)
    args.dst.parent.mkdir(parents=True, exist_ok=True)
    run(["/usr/bin/ditto", str(args.src), str(args.dst)])

    dst_va = args.dst / "Contents/MacOS/AppleIntelTGLGraphicsVADriver"
    dst_tglva = args.dst / "Contents/MacOS/TGLVA"
    orig_va_bytes = src_va.read_bytes()
    buf = bytearray(orig_va_bytes)

    for label, off, before, after in VA1_LL_GUARD_PATCHES:
        apply_edit(buf, label, off, before, after)

    if args.mode == "hw-encode-fix":
        ctx = bytes(buf[DW2_PITCH_CONTEXT_OFFSET : DW2_PITCH_CONTEXT_OFFSET + len(DW2_PITCH_CONTEXT_EXPECTED)])
        if ctx != DW2_PITCH_CONTEXT_EXPECTED:
            raise RuntimeError(
                f"MEDIA_SURFACE_STATE.DW2 context mismatch @ {hex(DW2_PITCH_CONTEXT_OFFSET)}: {ctx.hex()}"
            )
        apply_edit(
            buf,
            "MEDIA_SURFACE_STATE.DW2 clear pre-patch pitch",
            DW2_PITCH_PATCH_OFFSET,
            DW2_PITCH_EXPECTED,
            DW2_PITCH_REPLACEMENT,
        )
    else:
        apply_sw_fallback_delta(buf)

    dst_va.write_bytes(bytes(buf))
    if sha256_file(dst_tglva) != STOCK_TGLVA_SHA256:
        raise RuntimeError("TGLVA SHA-256 unexpectedly changed after copy")

    run(["/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", str(dst_tglva)])
    run(["/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", str(dst_va)])
    run(["/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", str(args.dst)])

    src_tglva_bytes = src_tglva.read_bytes()
    dst_tglva_bytes = dst_tglva.read_bytes()
    cut_tglva = linkedit_fileoff(src_tglva_bytes)
    if linkedit_fileoff(dst_tglva_bytes) != cut_tglva or src_tglva_bytes[0x8b0:cut_tglva] != dst_tglva_bytes[0x8b0:cut_tglva]:
        raise RuntimeError("TGLVA section bytes (0x8b0..__LINKEDIT) changed after signing")

    cut_va = linkedit_fileoff(orig_va_bytes)
    dst_va_bytes = dst_va.read_bytes()
    if linkedit_fileoff(dst_va_bytes) != cut_va or dst_va_bytes[0x1000:cut_va] != bytes(buf[0x1000:cut_va]):
        raise RuntimeError("VADriver pre-__LINKEDIT bytes altered unexpectedly during signing")

    run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(args.dst)])
    run(["/usr/bin/xcrun", "dyld_info", "-validate_only", str(dst_va)])
    run(["/usr/bin/xcrun", "dyld_info", "-validate_only", str(dst_tglva)])

    if get_uuid(dst_va) != EXPECTED_VADRIVER_UUID or get_uuid(dst_tglva) != EXPECTED_TGLVA_UUID:
        raise RuntimeError("Mach-O UUID changed unexpectedly")

    print(f"MODE: {args.mode}")
    print(f"FINAL VADriver SHA256={sha256_file(dst_va)}")
    print(f"FINAL TGLVA    SHA256={sha256_file(dst_tglva)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
