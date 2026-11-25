#!/usr/bin/env python3
"""
Inject verified 2026-01-28 / 2024-01-31 AX211/Gen3 firmware blobs from
IntelBluetoothFirmware 2.5.3 into IntelBTUnified v1.0.1 without altering
load commands, Info.plist, symbols, or _vnode_pagein page-eviction code.
"""

import hashlib
import os
import struct
import subprocess
import sys
import zlib

TARGETS = (
    "ibt-0040-0041.ddc",
    "ibt-0040-0041.sfi",
    "ibt-0040-1020.sfi",
    "ibt-0041-0041.sfi",
    "ibt-1040-0041.sfi",
    "ibt-1040-1020.sfi",
)

EXPECTED_UNPATCHED_IBTU_SHA = (
    "880e40c4797f9ef381637ce5416d3aebc4c323ba53bde2d1513c1c10419d062d"
)


def parse_sections(macho_path: str):
    ot = subprocess.check_output(["otool", "-l", macho_path], text=True)
    secs = []
    cur = {}
    for line in ot.splitlines():
        line = line.strip()
        if line.startswith("sectname "):
            cur = {"sect": line.split()[1]}
        elif line.startswith("addr "):
            cur["addr"] = int(line.split()[1], 16)
        elif line.startswith("size "):
            cur["size"] = int(line.split()[1], 16)
        elif line.startswith("offset "):
            cur["off"] = int(line.split()[1])
            secs.append(cur)
    return ot, secs


def v2f(secs, va: int):
    for s in secs:
        if s["addr"] <= va < s["addr"] + s["size"]:
            return s["off"] + (va - s["addr"])
    return None


def parse_syms(macho_path: str):
    nm_out = subprocess.check_output(["nm", "-n", macho_path], text=True)
    syms = []
    for line in nm_out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms.append((int(parts[0], 16), parts[1], parts[2]))
    return nm_out, syms


def load_ibf_blobs(ibf_path: str):
    _, secs = parse_sections(ibf_path)
    _, syms = parse_syms(ibf_path)
    with open(ibf_path, "rb") as f:
        blob = f.read()

    fa = fn = None
    for i, (addr, _, name) in enumerate(syms):
        if name == "_fwList":
            fa = addr
            fn = syms[i + 1][0]
            break
    assert fa is not None and fn is not None, "_fwList not found in IBF"
    fo = v2f(secs, fa)
    assert fo is not None

    out = {}
    for off in range(0, fn - fa, 24):
        if off + 24 > fn - fa:
            break
        n_va, d_va, sz = struct.unpack_from("<QQQ", blob, fo + off)
        if n_va == 0:
            break
        nfo = v2f(secs, n_va)
        dfo = v2f(secs, d_va)
        assert nfo is not None and dfo is not None
        fw_name = blob[nfo : blob.find(b"\x00", nfo)].decode("ascii")
        comp = blob[dfo : dfo + sz]
        dec = zlib.decompress(comp)
        out[fw_name] = {
            "comp": comp,
            "csz": sz,
            "usz": len(dec),
            "dec_sha": hashlib.sha256(dec).hexdigest(),
        }
    return out


def load_ibtu_fwlist(ibtu_path: str):
    ot, secs = parse_sections(ibtu_path)
    nm_out, syms = parse_syms(ibtu_path)
    with open(ibtu_path, "rb") as f:
        blob = bytearray(f.read())

    fa = fn = None
    for i, (addr, _, name) in enumerate(syms):
        if name == "_fwList":
            fa = addr
            fn = syms[i + 1][0]
            break
    assert fa is not None and fn is not None, "_fwList not found in IBTU"
    fo = v2f(secs, fa)
    assert fo is not None

    sym_by_addr = {}
    for i, (addr, _, name) in enumerate(syms):
        alloc_sz = (syms[i + 1][0] - addr) if i + 1 < len(syms) else 0
        sym_by_addr[addr] = (name, alloc_sz)

    entries = []
    by_dva = {}
    for idx, off in enumerate(range(0, fn - fa, 32)):
        n_va, d_va, csz, usz = struct.unpack_from("<QQQQ", blob, fo + off)
        nfo = v2f(secs, n_va)
        dfo = v2f(secs, d_va)
        assert nfo is not None and dfo is not None
        fw_name = blob[nfo : blob.find(b"\x00", nfo)].decode("ascii")
        dec = zlib.decompress(bytes(blob[dfo : dfo + csz]))
        assert len(dec) == usz, f"Pre-patch usz mismatch on {fw_name}"
        alloc_sz = sym_by_addr[d_va][1]
        by_dva.setdefault(d_va, []).append(fw_name)
        entries.append(
            {
                "idx": idx,
                "name": fw_name,
                "list_fo": fo + off,
                "d_va": d_va,
                "dfo": dfo,
                "csz": csz,
                "usz": usz,
                "alloc_sz": alloc_sz,
                "dec_sha": hashlib.sha256(dec).hexdigest(),
            }
        )
    return blob, ot, nm_out, entries, by_dva


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <target_IntelBTUnified_macho> <source_IBF_253_macho>", file=sys.stderr)
        sys.exit(2)

    ibtu_path, ibf_path = sys.argv[1], sys.argv[2]
    with open(ibtu_path, "rb") as f:
        orig_raw = f.read()
    orig_sha = hashlib.sha256(orig_raw).hexdigest()
    assert orig_sha == EXPECTED_UNPATCHED_IBTU_SHA, (
        f"Unexpected source IntelBTUnified SHA-256: {orig_sha}"
    )

    ibf_blobs = load_ibf_blobs(ibf_path)
    blob, orig_ot, orig_nm, entries, by_dva = load_ibtu_fwlist(ibtu_path)
    assert "LC_CODE_SIGNATURE" not in orig_ot, "Unexpected LC_CODE_SIGNATURE in IBTU"
    assert len(entries) == 85, f"Expected 85 _fwList entries, got {len(entries)}"

    orig_dec_shas = {e["name"]: e["dec_sha"] for e in entries}

    entry_map = {e["name"]: e for e in entries}
    for name in TARGETS:
        assert name in entry_map, f"Missing target {name} in IBTU"
        assert name in ibf_blobs, f"Missing source {name} in IBF 2.5.3"
        e = entry_map[name]
        src = ibf_blobs[name]
        assert len(by_dva[e["d_va"]]) == 1, f"Shared data_va for {name}: {by_dva[e['d_va']]}"
        assert src["csz"] <= e["alloc_sz"], (
            f"{name} new_csz {src['csz']} > alloc_sz {e['alloc_sz']}"
        )

        # Write compressed firmware into existing __TEXT,__const slot and zero-pad remainder
        dfo = e["dfo"]
        blob[dfo : dfo + src["csz"]] = src["comp"]
        pad_len = e["alloc_sz"] - src["csz"]
        if pad_len > 0:
            blob[dfo + src["csz"] : dfo + e["alloc_sz"]] = b"\x00" * pad_len

        # Update _fwList[idx].compressed_size and .uncompressed_size
        struct.pack_into("<QQ", blob, e["list_fo"] + 16, src["csz"], src["usz"])
        print(
            f"Patched [{e['idx']:2d}] {name:18s}: csz {e['csz']}->{src['csz']} "
            f"(alloc {e['alloc_sz']}), usz {e['usz']}->{src['usz']}"
        )

    assert len(blob) == len(orig_raw), "Binary size changed!"
    tmp_out = ibtu_path + ".tmp"
    with open(tmp_out, "wb") as f:
        f.write(blob)
    os.chmod(tmp_out, 0o755)
    os.replace(tmp_out, ibtu_path)

    # Exhaustive post-patch verification
    blob2, new_ot, new_nm, entries2, _ = load_ibtu_fwlist(ibtu_path)
    assert len(blob2) == len(orig_raw)
    assert new_ot == orig_ot, "otool -l load commands changed!"
    assert new_nm == orig_nm, "nm -n symbol table changed!"
    assert len(entries2) == 85

    for e in entries2:
        name = e["name"]
        if name in TARGETS:
            assert e["dec_sha"] == ibf_blobs[name]["dec_sha"], f"Patched mismatch: {name}"
            if name in ("ibt-0040-0041.sfi", "ibt-0041-0041.sfi", "ibt-1040-0041.sfi"):
                dec = zlib.decompress(bytes(blob2[e["dfo"] : e["dfo"] + e["csz"]]))
                dt_hex = dec[20:24].hex()
                assert dt_hex == "28012620", f"Unexpected build date on {name}: {dt_hex}"
        else:
            assert e["dec_sha"] == orig_dec_shas[name], f"Untouched entry modified: {name}"

    new_sha = hashlib.sha256(blob2).hexdigest()
    print("Verified 85/85 zlib entries (6/6 updated match IBF-2.5.3, 79/79 untouched identical).")
    print(f"Patched IntelBTUnified SHA-256: {new_sha}")


if __name__ == "__main__":
    main()
