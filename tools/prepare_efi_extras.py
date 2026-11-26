#!/usr/bin/env python3
"""Retrieve the optional Air14 EFI drivers from their original releases."""

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


def prepare(efi):
    oc = efi / "OC"
    config_path = oc / "config.plist"
    kexts = oc / "Kexts"
    original = config_path.read_bytes()
    config = plistlib.loads(original)
    names = {driver[0] for driver in DRIVERS} | {"UTBXHCI.kext"}
    entries = [e for e in config["Kernel"]["Add"] if e["BundlePath"] in names]
    if len(entries) != len(names) or {e["BundlePath"] for e in entries} != names:
        raise ValueError("Use the configuration shipped with the Air14 public EFI")
    port_map = kexts / "UTBXHCI.kext/Contents/Info.plist"
    if not port_map.is_file():
        raise ValueError("The EFI is missing the Air14 USB map")
    with TemporaryDirectory(prefix=".air14-kexts-", dir=oc) as tmp:
        stage = Path(tmp)
        downloaded = [download_bundle(driver, stage) for driver in DRIVERS]
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
        backup = oc / "config.before-extras.plist"
        try:
            with backup.open("xb") as output:
                output.write(original)
        except FileExistsError:
            pass
        os.replace(prepared_config, config_path)
    print("Enabled USBToolBox, the Air14 USB map and ECEnabler.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("efi", type=Path, help="Path to the extracted Air14 EFI directory")
    args = parser.parse_args()
    try:
        prepare(args.efi)
    except (OSError, ValueError, RuntimeError, BadZipFile) as exc:
        parser.exit(1, f"{exc}\n")
