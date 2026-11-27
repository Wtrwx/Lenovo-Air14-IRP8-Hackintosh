#!/usr/bin/env python3
"""Prepare the Air14 EFI's optional USB, battery or audio drivers."""

import argparse
from hashlib import sha256
import io
import os
from pathlib import Path, PurePosixPath
import plistlib
import shutil
import stat
from tempfile import TemporaryDirectory
from urllib.request import Request, urlopen
from zipfile import BadZipFile, ZipFile


DRIVERS = (
    (
        "USBToolBox.kext",
        "com.dhinakg.USBToolBox.kext",
        "1.2.0",
        "https://github.com/USBToolBox/kext/releases/download/1.2.0/USBToolBox-1.2.0-RELEASE.zip",
        "c315a3a5acfd496dd97d0d19b4fbd1d487103d2fd541c5651583d4c9cebcfe07",
    ),
    (
        "ECEnabler.kext",
        "com.1Revenger1.ECEnabler",
        "1.0.6",
        "https://github.com/averycblack/ECEnabler/releases/download/1.0.6/ECEnabler-1.0.6-RELEASE.zip",
        "e21149519856308c6c4c06e0917aef9e3423ff3e23b6ea77989099f4f2cf0f0a",
    ),
)

AUDIO_DRIVER = (
    "SimpleHDA.kext",
    "cn.laobamac.SimpleHDA",
    "1.0.0",
    "https://github.com/laobamac/SimpleHDA/releases/download/v1.0.0/SimpleHDA-Tahoe-v1.0.0-RELEASE.zip",
    "89198256664078c91155d673ee7cebb08134dad1b71d5e34b0aa4ef0d4877f8f",
)
SST_KEXT = "IntelSSTDMIC.kext"
APPLE_NVRAM = "7C436110-AB2A-4BBB-A880-FE41995C9F82"


def download_bundle(driver, staging):
    bundle, identifier, version, url, digest = driver
    destination = staging / bundle
    request = Request(url, headers={"User-Agent": "Air14-IRP8-EFI"})
    with urlopen(request, timeout=30) as response:
        data = response.read(4 * 1024 * 1024)
    if sha256(data).hexdigest() != digest:
        raise ValueError(f"Upstream archive failed SHA-256 verification for {bundle}")
    with ZipFile(io.BytesIO(data)) as archive:
        for member in archive.infolist():
            parts = PurePosixPath(member.filename).parts
            if bundle not in parts or member.is_dir():
                continue
            relative = parts[parts.index(bundle) + 1 :]
            if not relative or ".." in relative or stat.S_ISLNK(member.external_attr >> 16):
                raise ValueError(f"Unexpected path in archive for {bundle}")
            path = destination.joinpath(*relative)
            path.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source, path.open("xb") as output:
                shutil.copyfileobj(source, output)
    info = plistlib.loads((destination / "Contents/Info.plist").read_bytes())
    if (info.get("CFBundleIdentifier"), info.get("CFBundleVersion")) != (identifier, version):
        raise ValueError(f"Unexpected bundle version or identifier for {bundle}")
    binary = destination / "Contents/MacOS" / info["CFBundleExecutable"]
    binary.chmod(0o755)
    return destination


def files(directory):
    return {p.relative_to(directory): sha256(p.read_bytes()).digest()
            for p in directory.rglob("*") if p.is_file()}


def prepare(efi, audio=False):
    oc = efi / "OC"
    config_path = oc / "config.plist"
    kexts = oc / "Kexts"
    original = config_path.read_bytes()
    config = plistlib.loads(original)
    drivers = (AUDIO_DRIVER,) if audio else DRIVERS
    names = {driver[0] for driver in drivers} | {SST_KEXT if audio else "UTBXHCI.kext"}
    kernel_add = config["Kernel"]["Add"]
    entries = [entry for entry in kernel_add if entry["BundlePath"] in names]
    if len(entries) != len(names) or {e["BundlePath"] for e in entries} != names:
        raise ValueError("Use the configuration shipped with the Air14 public EFI")
    if audio:
        audio_order = {"Lilu.kext", "AppleALC.kext", *names}
        ordered = [e["BundlePath"] for e in kernel_add if e["BundlePath"] in audio_order]
        if ordered != ["Lilu.kext", "AppleALC.kext", SST_KEXT, AUDIO_DRIVER[0]]:
            raise ValueError("Use the Air14 EFI's Lilu → AppleALC → IntelSSTDMIC → SimpleHDA order")
        if not (kexts / SST_KEXT / "Contents/MacOS/IntelSSTDMIC").is_file():
            raise ValueError("The EFI is missing the Air14 IntelSSTDMIC driver")
        nvram = config["NVRAM"]["Add"][APPLE_NVRAM]
        args = nvram["boot-args"].split()
        for argument in ("keepsyms=1", "-sstdmicadl", "-sstdmiclayout=4"):
            if argument not in args:
                args.append(argument)
        nvram["boot-args"] = " ".join(args)
        config["Kernel"]["Quirks"]["DisableLinkeditJettison"] = True
    elif not (kexts / "UTBXHCI.kext/Contents/Info.plist").is_file():
        raise ValueError("The EFI is missing the Air14 USB map")
    with TemporaryDirectory(prefix=".air14-kexts-", dir=oc) as tmp:
        stage = Path(tmp)
        downloaded = [download_bundle(driver, stage) for driver in drivers]
        for entry in entries:
            entry["Enabled"] = True
        prepared_config = stage / "config.plist"
        prepared = plistlib.dumps(config, sort_keys=False)
        prepared_config.write_bytes(prepared)
        for source in downloaded:
            expected = files(source)
            current = kexts / source.name
            if not current.exists():
                try:
                    source.rename(current)
                except OSError:
                    if not current.is_dir():
                        raise
            if files(current) != expected:
                raise ValueError(f"An existing {source.name} differs from the pinned version")
        if config_path.read_bytes() not in (original, prepared):
            raise RuntimeError("config.plist changed during preparation")
        backup = oc / ("config.before-audio.plist" if audio else "config.before-extras.plist")
        try:
            with backup.open("xb") as output:
                output.write(original)
        except FileExistsError:
            pass
        os.replace(prepared_config, config_path)
    print("Enabled Air14 IntelSSTDMIC and SimpleHDA." if audio else
          "Enabled USBToolBox, the Air14 USB map and ECEnabler.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("efi", type=Path, help="Path to the extracted Air14 EFI directory")
    parser.add_argument("--audio", action="store_true",
                        help="Enable audio using the bundled IntelSSTDMIC and upstream SimpleHDA")
    args = parser.parse_args()
    try:
        prepare(args.efi, args.audio)
    except (OSError, KeyError, ValueError, RuntimeError, BadZipFile) as exc:
        parser.exit(1, f"{exc}\n")
