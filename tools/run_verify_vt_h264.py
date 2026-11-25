#!/usr/bin/env python3
"""Run self-contained VideoToolbox H.264 encode/decode quality and QP verification."""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess


LIVE_BIN = Path("/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsVADriver")


class BitReader:
    def __init__(self, data: bytes):
        self.data = data
        self.bitpos = 0

    def u(self, n: int) -> int:
        val = 0
        for _ in range(n):
            byte_idx = self.bitpos >> 3
            if byte_idx >= len(self.data):
                raise EOFError("Bitstream truncated")
            bit = (self.data[byte_idx] >> (7 - (self.bitpos & 7))) & 1
            val = (val << 1) | bit
            self.bitpos += 1
        return val

    def ue(self) -> int:
        zeros = 0
        while self.u(1) == 0:
            zeros += 1
            if zeros > 31:
                raise ValueError("Invalid Exp-Golomb code")
        if zeros == 0:
            return 0
        return (1 << zeros) - 1 + self.u(zeros)

    def se(self) -> int:
        code = self.ue()
        if code == 0:
            return 0
        mag = (code + 1) >> 1
        return mag if (code & 1) else -mag


def strip_emulation_prevention(nal: bytes) -> bytes:
    out = bytearray()
    zeros = 0
    for b in nal:
        if zeros == 2 and b == 3:
            zeros = 0
            continue
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def split_annexb_nals(data: bytes) -> list[bytes]:
    starts = []
    i = 0
    n = len(data)
    while i + 3 < n:
        if data[i] == 0 and data[i + 1] == 0:
            if data[i + 2] == 1:
                starts.append((i, i + 3))
                i += 3
                continue
            if i + 3 < n and data[i + 2] == 0 and data[i + 3] == 1:
                starts.append((i, i + 4))
                i += 4
                continue
        i += 1
    nals = []
    for idx, (_s, payload_start) in enumerate(starts):
        end = starts[idx + 1][0] if idx + 1 < len(starts) else n
        if payload_start < end:
            nals.append(data[payload_start:end])
    return nals


def skip_scaling_list(br: BitReader, size: int) -> None:
    last_scale = 8
    next_scale = 8
    for _ in range(size):
        if next_scale != 0:
            delta_scale = br.se()
            next_scale = (last_scale + delta_scale + 256) % 256
        last_scale = next_scale if next_scale != 0 else last_scale


def parse_sps(rbsp: bytes) -> dict:
    br = BitReader(rbsp[1:])
    profile_idc = br.u(8)
    _constraints = br.u(8)
    level_idc = br.u(8)
    sps_id = br.ue()

    chroma_format_idc = 1
    separate_colour_plane_flag = 0
    if profile_idc in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        chroma_format_idc = br.ue()
        if chroma_format_idc == 3:
            separate_colour_plane_flag = br.u(1)
        _bit_depth_luma_m8 = br.ue()
        _bit_depth_chroma_m8 = br.ue()
        _qpprime_y = br.u(1)
        seq_scaling_matrix_present = br.u(1)
        if seq_scaling_matrix_present:
            cnt = 12 if chroma_format_idc == 3 else 8
            for i in range(cnt):
                if br.u(1):
                    skip_scaling_list(br, 16 if i < 6 else 64)

    chroma_array_type = 0 if separate_colour_plane_flag else chroma_format_idc
    log2_max_frame_num_m4 = br.ue()
    pic_order_cnt_type = br.ue()
    log2_max_poc_lsb_m4 = 0
    delta_poc_always_zero = 0
    if pic_order_cnt_type == 0:
        log2_max_poc_lsb_m4 = br.ue()
    elif pic_order_cnt_type == 1:
        delta_poc_always_zero = br.u(1)
        _off_non_ref = br.se()
        _off_top_bot = br.se()
        num_ref = br.ue()
        for _ in range(num_ref):
            br.se()

    max_num_ref_frames = br.ue()
    _gaps = br.u(1)
    pic_width_mbs_m1 = br.ue()
    pic_height_map_units_m1 = br.ue()
    frame_mbs_only_flag = br.u(1)

    return {
        "sps_id": sps_id,
        "profile_idc": profile_idc,
        "level_idc": level_idc,
        "chroma_array_type": chroma_array_type,
        "separate_colour_plane_flag": separate_colour_plane_flag,
        "log2_max_frame_num": log2_max_frame_num_m4 + 4,
        "pic_order_cnt_type": pic_order_cnt_type,
        "log2_max_poc_lsb": log2_max_poc_lsb_m4 + 4,
        "delta_poc_always_zero": delta_poc_always_zero,
        "max_num_ref_frames": max_num_ref_frames,
        "frame_mbs_only_flag": frame_mbs_only_flag,
        "coded_width": (pic_width_mbs_m1 + 1) * 16,
        "coded_height": (pic_height_map_units_m1 + 1) * 16 * (1 if frame_mbs_only_flag else 2),
    }


def parse_pps(rbsp: bytes) -> dict:
    br = BitReader(rbsp[1:])
    pps_id = br.ue()
    sps_id = br.ue()
    entropy_coding_mode_flag = br.u(1)
    bottom_field_pic_order = br.u(1)
    num_slice_groups_m1 = br.ue()
    if num_slice_groups_m1 > 0:
        raise ValueError("FMO slice groups unsupported in parser")
    num_ref_idx_l0_default_m1 = br.ue()
    num_ref_idx_l1_default_m1 = br.ue()
    weighted_pred_flag = br.u(1)
    weighted_bipred_idc = br.u(2)
    pic_init_qp_minus26 = br.se()
    _pic_init_qs_minus26 = br.se()
    chroma_qp_index_offset = br.se()
    _deblocking = br.u(1)
    _constrained_intra = br.u(1)
    redundant_pic_cnt_present_flag = br.u(1)
    return {
        "pps_id": pps_id,
        "sps_id": sps_id,
        "cabac": bool(entropy_coding_mode_flag),
        "bottom_field_pic_order": bottom_field_pic_order,
        "num_ref_idx_l0_default_m1": num_ref_idx_l0_default_m1,
        "num_ref_idx_l1_default_m1": num_ref_idx_l1_default_m1,
        "weighted_pred_flag": weighted_pred_flag,
        "weighted_bipred_idc": weighted_bipred_idc,
        "pic_init_qp_minus26": pic_init_qp_minus26,
        "chroma_qp_index_offset": chroma_qp_index_offset,
        "redundant_pic_cnt_present_flag": redundant_pic_cnt_present_flag,
    }


def parse_slice(rbsp: bytes, sps_map: dict, pps_map: dict) -> dict:
    nal_hdr = rbsp[0]
    nal_ref_idc = (nal_hdr >> 5) & 3
    nal_type = nal_hdr & 0x1f
    br = BitReader(rbsp[1:])

    _first_mb = br.ue()
    raw_slice_type = br.ue()
    st = raw_slice_type % 5
    slice_name = {0: "P", 1: "B", 2: "I", 3: "SP", 4: "SI"}[st]
    pps_id = br.ue()
    pps = pps_map[pps_id]
    sps = sps_map[pps["sps_id"]]

    if sps["separate_colour_plane_flag"]:
        br.u(2)
    _frame_num = br.u(sps["log2_max_frame_num"])
    field_pic_flag = 0
    if not sps["frame_mbs_only_flag"]:
        field_pic_flag = br.u(1)
        if field_pic_flag:
            _bottom_field = br.u(1)
    if nal_type == 5:
        _idr_pic_id = br.ue()
    if sps["pic_order_cnt_type"] == 0:
        _poc_lsb = br.u(sps["log2_max_poc_lsb"])
        if pps["bottom_field_pic_order"] and not field_pic_flag:
            br.se()
    elif sps["pic_order_cnt_type"] == 1 and not sps["delta_poc_always_zero"]:
        br.se()
        if pps["bottom_field_pic_order"] and not field_pic_flag:
            br.se()
    if pps["redundant_pic_cnt_present_flag"]:
        br.ue()
    if st == 1:
        br.u(1)

    l0_active_m1 = pps["num_ref_idx_l0_default_m1"]
    l1_active_m1 = pps["num_ref_idx_l1_default_m1"]
    if st in (0, 1, 3):
        if br.u(1):
            l0_active_m1 = br.ue()
            if st == 1:
                l1_active_m1 = br.ue()

    if st not in (2, 4):
        if br.u(1):
            while True:
                idc = br.ue()
                if idc == 3:
                    break
                br.ue()
        if st == 1 and br.u(1):
            while True:
                idc = br.ue()
                if idc == 3:
                    break
                br.ue()

    if (pps["weighted_pred_flag"] and st in (0, 3)) or (pps["weighted_bipred_idc"] == 1 and st == 1):
        br.ue()
        if sps["chroma_array_type"] != 0:
            br.ue()
        for _ in range(l0_active_m1 + 1):
            if br.u(1):
                br.se()
                br.se()
            if sps["chroma_array_type"] != 0 and br.u(1):
                for _ in range(4):
                    br.se()
        if st == 1:
            for _ in range(l1_active_m1 + 1):
                if br.u(1):
                    br.se()
                    br.se()
                if sps["chroma_array_type"] != 0 and br.u(1):
                    for _ in range(4):
                        br.se()

    if nal_ref_idc != 0:
        if nal_type == 5:
            br.u(1)
            br.u(1)
        else:
            if br.u(1):
                while True:
                    mmco = br.ue()
                    if mmco == 0:
                        break
                    if mmco in (1, 3):
                        br.ue()
                    if mmco == 2:
                        br.ue()
                    if mmco in (3, 6):
                        br.ue()
                    if mmco == 4:
                        br.ue()

    if pps["cabac"] and st not in (2, 4):
        _cabac_init_idc = br.ue()
    slice_qp_delta = br.se()
    slice_qp = 26 + pps["pic_init_qp_minus26"] + slice_qp_delta
    return {
        "nal_type": nal_type,
        "slice_type": slice_name,
        "cabac": pps["cabac"],
        "pic_init_qp": 26 + pps["pic_init_qp_minus26"],
        "slice_qp_delta": slice_qp_delta,
        "slice_qp": slice_qp,
    }


def parse_h264_annexb(path: Path) -> dict:
    raw = path.read_bytes()
    sps_map = {}
    pps_map = {}
    slices = []
    for nal in split_annexb_nals(raw):
        nal_type = nal[0] & 0x1f
        rbsp = strip_emulation_prevention(nal)
        if nal_type == 7:
            sps = parse_sps(rbsp)
            sps_map[sps["sps_id"]] = sps
        elif nal_type == 8:
            pps = parse_pps(rbsp)
            pps_map[pps["pps_id"]] = pps
        elif nal_type in (1, 5):
            slices.append(parse_slice(rbsp, sps_map, pps_map))

    qps = [s["slice_qp"] for s in slices]
    p_qps = [s["slice_qp"] for s in slices if s["slice_type"] != "I"]
    i_qps = [s["slice_qp"] for s in slices if s["slice_type"] == "I"]
    sps0 = next(iter(sps_map.values()), {})
    pps0 = next(iter(pps_map.values()), {})
    return {
        "profile_idc": sps0.get("profile_idc"),
        "level_idc": sps0.get("level_idc"),
        "cabac": pps0.get("cabac"),
        "slice_count": len(slices),
        "i_qp": i_qps[0] if i_qps else None,
        "p_qp_min": min(p_qps) if p_qps else None,
        "p_qp_max": max(p_qps) if p_qps else None,
        "p_qp_avg": round(sum(p_qps) / len(p_qps), 2) if p_qps else None,
        "slice_qps": qps[:24],
    }


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_cmd(args: list[str], timeout: int = 45) -> subprocess.CompletedProcess:
    return subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tool",
        type=Path,
        default=Path(__file__).with_name("verify_vt_h264"),
        help="Path to compiled verify_vt_h264 executable",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output report directory (defaults to ./vt-h264-report-<timestamp>)",
    )
    parser.add_argument(
        "--expected-va-sha256",
        default=None,
        help="Optional expected SHA-256 of installed AppleIntelTGLGraphicsVADriver",
    )
    parser.add_argument(
        "--dry-run-soft",
        action="store_true",
        help="Run only ll-must-reject and uu-soft control without hardware encode cases.",
    )
    args = parser.parse_args()

    if not args.tool.exists():
        raise SystemExit(f"Missing compiled binary {args.tool}")

    live_hash = digest(LIVE_BIN) if LIVE_BIN.exists() else None
    if args.expected_va_sha256 and live_hash != args.expected_va_sha256:
        raise SystemExit(f"Live VADriver SHA-256 ({live_hash}) != expected ({args.expected_va_sha256})")
    if not args.dry_run_soft and LIVE_BIN.exists():
        subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(LIVE_BIN.parents[2])], check=True)

    now = datetime.datetime.now().astimezone()
    report_dir = args.out or (Path.cwd() / f"vt-h264-report-{now.strftime('%Y%m%d-%H%M%S')}")
    report_dir.mkdir(parents=True, exist_ok=True)

    diagnostics = Path("/Library/Logs/DiagnosticReports")
    reports_before = set(diagnostics.glob("*.gpuRestart")) if diagnostics.exists() else set()

    ll = run_cmd([str(args.tool), "ll-must-reject", "1280", "720", "1", "1400000", "motion"])
    (report_dir / "ll-must-reject.log").write_text(ll.stdout)
    if ll.returncode != 0 or "PASS ll-must-reject status=-12903" not in ll.stdout:
        raise RuntimeError(f"ll-must-reject failed:\n{ll.stdout}")

    if args.dry_run_soft:
        cases = [("software-ctrl", "uu-soft", 1728, 1080, 24, 1400000, "motion")]
    else:
        cases = [
            ("baseline", "uu-normal", 1728, 1080, 24, 1400000, "motion"),
            ("still", "uu-normal", 1728, 1080, 90, 1400000, "still"),
            ("hardware-8m", "uu-normal", 1728, 1080, 24, 8000000, "motion"),
            ("default-rate", "uu-normal", 1728, 1080, 24, 0, "motion"),
            ("hidpi-2880", "uu-normal", 2880, 1800, 24, 4000000, "motion"),
            ("software-ctrl", "uu-soft", 1728, 1080, 24, 1400000, "motion"),
            ("hw-decode", "hw-decode", 1728, 1080, 24, 1400000, "motion"),
        ]

    results = {}
    for name, mode, w, h, frames, bitrate, motion in cases:
        case_dir = report_dir / name
        proc = run_cmd([str(args.tool), mode, str(w), str(h), str(frames), str(bitrate), motion, str(case_dir)])
        case_dir.mkdir(parents=True, exist_ok=True)
        (case_dir / "run.log").write_text(proc.stdout)
        if proc.returncode != 0:
            raise RuntimeError(f"Case {name} failed (rc={proc.returncode}):\n{proc.stdout}")
        metrics = json.loads((case_dir / "metrics.json").read_text())
        metrics["h264"] = parse_h264_annexb(case_dir / "output.h264")
        results[name] = metrics

    kernel = run_cmd([
        "/usr/bin/log", "show", "--style", "compact", "--info",
        "--start", now.strftime("%Y-%m-%d %H:%M:%S"),
        "--predicate",
        'process == "kernel" AND (eventMessage CONTAINS "GPURestart" OR '
        '(eventMessage CONTAINS "waitForStamp" AND (eventMessage CONTAINS "expired" OR eventMessage CONTAINS "graphics error")))',
    ])
    kernel.check_returncode()
    (report_dir / "kernel.log").write_text(kernel.stdout)
    reports_after = set(diagnostics.glob("*.gpuRestart")) if diagnostics.exists() else set()
    new_gpu_restarts = sorted(p.name for p in (reports_after - reports_before))
    if "kernel[" in kernel.stdout or new_gpu_restarts:
        raise RuntimeError(f"GPU error detected during run: restarts={new_gpu_restarts}\n{kernel.stdout}")

    summary = {
        "mode": "dry-run-soft" if args.dry_run_soft else "live",
        "live_va_hash": live_hash,
        "ll_must_reject": "PASS (-12903)",
        "gpu_restarts": 0,
        "cases": results,
    }
    out_json = report_dir / "result.json"
    out_json.write_text(json.dumps(summary, indent=2) + "\n")

    print(f"Report: {out_json}")
    print("CASE            MODE       RES         FR   TARGET    ACTUAL(Mb/s)  PSNR-Y   IDR(B)   AVG-P(B)  QP(I / P_avg)")
    for name, m in results.items():
        h264 = m["h264"]
        tgt = f"{m['bitrate_target_bps']/1e6:.1f}M" if m["bitrate_target_bps"] > 0 else "none"
        print(
            f"{name:14s}  {m['mode']:9s}  {m['width']:4d}x{m['height']:<4d}  {m['frames']:2d}   "
            f"{tgt:6s}   {m['bitrate_mbps']:8.3f}      {m['psnr_y_db']:6.2f}   "
            f"{m['idr_bytes']:7d}  {m['avg_p_bytes']:8.1f}  {h264['i_qp']} / {h264['p_qp_avg']}"
        )


if __name__ == "__main__":
    main()
