#!/usr/bin/env python3
"""Build a private, reversible VA bundle candidate. Does not install or load VA."""
import argparse
import hashlib
import json
import os
import plistlib
import re
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
MAIN = 'AppleIntelTGLGraphicsVADriver'
DONOR = 'a972cdb7ddd56bcf38c3ab71e874b73efc34bc683d30e49f0a1a1919625a726d'
BRIDGE = 'cbf0a308a536d2d54565684025d32b200649681aa2fda45dd46bdfdb77b3da9e'
TEXT = '3af22e872e2312035f4603b70b28c2fd5fb47a274ef21a40abf5446911cbf30f'
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def run(*args): subprocess.run([str(a) for a in args], check=True)
def copy_payload(source, target):
    # Copy bytes and executable bits, not system-volume ACLs, flags or xattrs.
    # The output belongs to the current developer and is not an installed asset.
    target.mkdir()
    for item in source.iterdir():
        destination = target/item.name
        if item.is_symlink():
            value = os.readlink(item)
            assert not Path(value).is_absolute() and '..' not in Path(value).parts
            destination.symlink_to(value)
        elif item.is_dir(): copy_payload(item, destination)
        else:
            shutil.copyfile(item, destination)
            destination.chmod((item.stat().st_mode & 0o777) | 0o200)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--donor', type=Path, required=True, help='Exact installed VA .bundle')
    parser.add_argument('--output', type=Path, required=True, help='New private output directory')
    args = parser.parse_args()
    source = args.donor.resolve(); out = args.output.resolve()
    assert not out.exists(), 'Preserve previous output; use a new directory'
    macos = source / 'Contents/MacOS'
    assert sha(macos / MAIN) == DONOR and sha(macos / 'TGLVA') == BRIDGE, 'Unsupported donor'
    assert not (macos / 'TGLVAOriginal').exists(), 'Already adapted'
    out.mkdir(parents=True)
    common = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations']
    run(*common, '-DNATIVEAVC_TESTING', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        HERE/'test.cpp', HERE/'adapter.cpp', '-o', out/'tests')
    run(out/'tests', macos/MAIN)
    candidate = out / source.name
    copy_payload(source, candidate)
    target = candidate / 'Contents/MacOS'
    (target/'TGLVA').rename(target/'TGLVAOriginal')
    run(*common, '-dynamiclib', '-O2', HERE/'adapter.cpp', '-framework', 'IOKit', '-framework', 'CoreFoundation',
        '-Wl,-install_name,@loader_path/NativeAvcCompat', f'-Wl,-reexport_library,{macos / "TGLVA"}',
        '-o', target/'TGLVA')
    # Only our proxy's load command is changed. Retain the imported adapter byte for byte.
    run('install_name_tool', '-change', '@loader_path/TGLVA', '@loader_path/TGLVAOriginal', target/'TGLVA')
    run('codesign', '--force', '--sign', '-', target/'TGLVA')
    run('codesign', '--force', '--sign', '-', '--preserve-metadata=entitlements', candidate)
    # The imported Mod leaf is not a system trust anchor on this host (the
    # installed donor has the same failure). Verify our ad-hoc signatures and
    # the sealed resources explicitly; do not add a root certificate or alter
    # the imported companion just to make a trust-policy check pass.
    run('codesign', '--verify', '--strict', target/'TGLVA')
    run('codesign', '--verify', '--strict', '--ignore-resources', candidate)
    assert sha(target/'TGLVAOriginal') == BRIDGE
    before = (macos/MAIN).read_bytes(); after = (target/MAIN).read_bytes()
    assert before[0x1160:0x754000] == after[0x1160:0x754000]
    assert hashlib.sha256(after[0x1160:0x754000]).hexdigest() == TEXT
    resources = plistlib.loads((candidate/'Contents/_CodeSignature/CodeResources').read_bytes())
    for name, entry in resources['files2'].items():
        file = candidate/'Contents'/name
        if 'hash2' in entry: assert hashlib.sha256(file.read_bytes()).digest() == entry['hash2']
        if 'hash' in entry: assert hashlib.sha1(file.read_bytes()).digest() == entry['hash']
        if 'cdhash' in entry:
            detail = subprocess.run(['codesign','-d','--verbose=4',str(file)], check=True, capture_output=True, text=True)
            cdhash = re.search(r'^CDHash=([a-f0-9]+)$',detail.stderr,re.M)
            assert cdhash and bytes.fromhex(cdhash[1]) == entry['cdhash']
    # Linkage smoke test loads only the two companions into a short-lived
    # helper process. The Apple VA driver is never loaded or submitted to.
    helper = out/'linkage.cpp'
    helper.write_text('''#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <cstdio>
#include <cstring>
int main(int argc, char **argv) {
    if (argc != 2) return 1;
    if (!dlopen(argv[1], RTLD_NOW | RTLD_LOCAL)) { std::puts(dlerror()); return 2; }
    bool original = false, proxy = false;
    for (uint32_t i=0; i<_dyld_image_count(); ++i) {
        const char *p = _dyld_get_image_name(i);
        if (std::strstr(p, "TGLVAOriginal")) original = true;
        if (std::strstr(p, "/Contents/MacOS/TGLVA") && !std::strstr(p,"TGLVAOriginal")) proxy = true;
        if (std::strstr(p, "/Contents/MacOS/AppleIntelTGLGraphicsVADriver")) return 3;
    }
    if (!original || !proxy) return 4;
    std::puts("Companion linkage passed; Apple VA driver not loaded."); return 0;
}
''')
    run(*common, helper, '-o', out/'linkage')
    run(out/'linkage', target/'TGLVA')
    inventory = {str(p.relative_to(candidate)): sha(p) for p in candidate.rglob('*') if p.is_file()}
    manifest = dict(status='prepared; CPU tested; not deployed or hardware verified',abiRevision=2,
                    donorSHA256=DONOR, originalBridgeSHA256=BRIDGE,
                    immutableTextSHA256=TEXT, darwin='25.6.0', physicalGpu='8086:a7a0',
                    signatureValidation='ad-hoc proxy/main and sealed resource hashes verified; imported Mod trust chain remains untrusted as in installed donor',
                    scope='native progressive AVC VDEnc feature 0x400 SPS ABI projection',
                    artifacts=inventory)
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps({k:v for k,v in manifest.items() if k != 'artifacts'}, indent=2))

if __name__ == '__main__': main()
