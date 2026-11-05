#!/usr/bin/env python3
"""Build and CPU-test a private Metal lifetime candidate; never install or load the driver."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
MAIN = 'AppleIntelTGLGraphicsMTLDriver'
DONOR = '1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c'
BRIDGE = 'cd415a30e111d9ad48e6dd1936d848c1e809821aa3770c93ae7c759a4454567c'
TEXT = '49b0f40eb067acb4ffc0345170f6b68e482b4736a8d0eddd3c15204885be2835'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)

def copy_payload(source, target):
    target.mkdir()
    for item in source.iterdir():
        destination = target/item.name
        if item.is_symlink():
            value = os.readlink(item)
            assert not Path(value).is_absolute() and '..' not in Path(value).parts
            destination.symlink_to(value)
        elif item.is_dir():
            copy_payload(item, destination)
        else:
            shutil.copyfile(item, destination)
            destination.chmod((item.stat().st_mode & 0o777) | 0o200)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--donor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.donor.resolve(); out = args.output.resolve()
    assert source.name == MAIN+'.bundle' and not out.exists()
    macos = source/'Contents/MacOS'
    assert sha(macos/MAIN) == DONOR and sha(macos/'impostor.dylib') == BRIDGE, 'Unsupported donor'
    assert not (macos/'impostor-original.dylib').exists(), 'Already adapted'
    out.mkdir(parents=True)
    common = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations']
    run(*common, '-DMETAL_LIFETIME_TESTING', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        HERE/'test.mm', HERE/'adapter.mm', HERE/'replay.S', '-lobjc', '-o', out/'tests')
    environment = dict(os.environ, ASAN_OPTIONS='quarantine_size_mb=16')
    run(out/'tests', macos/MAIN, env=environment)
    candidate = out/source.name
    copy_payload(source, candidate)
    target = candidate/'Contents/MacOS'
    (target/'impostor.dylib').rename(target/'impostor-original.dylib')
    run(*common, '-dynamiclib', '-O2', HERE/'adapter.mm', '-framework', 'IOKit', '-framework',
        'CoreFoundation', '-lobjc', '-Wl,-install_name,@loader_path/MetalLifetimeCompat',
        f'-Wl,-reexport_library,{macos/"impostor.dylib"}', '-o', target/'impostor.dylib')
    run('install_name_tool', '-change', '@rpath/impostor.dylib',
        '@loader_path/impostor-original.dylib', target/'impostor.dylib')
    run('codesign', '--force', '--sign', '-', target/'impostor.dylib')
    run('codesign', '--force', '--sign', '-', '--preserve-metadata=entitlements', candidate)
    run('codesign', '--verify', '--strict', target/'impostor.dylib')
    run('codesign', '--verify', '--strict', '--ignore-resources', candidate)
    assert sha(target/'impostor-original.dylib') == BRIDGE
    for name in ('descriptor.dylib', 'libigdmd.dylib'):
        assert sha(target/name) == sha(macos/name)
    before = (macos/MAIN).read_bytes(); after = (target/MAIN).read_bytes()
    assert before[0x20d0:0x480000] == after[0x20d0:0x480000]
    assert hashlib.sha256(after[0x20d0:0x480000]).hexdigest() == TEXT
    resources = plistlib.loads((candidate/'Contents/_CodeSignature/CodeResources').read_bytes())
    for name, entry in resources['files2'].items():
        file = candidate/'Contents'/name
        if 'hash2' in entry: assert hashlib.sha256(file.read_bytes()).digest() == entry['hash2']
        if 'hash' in entry: assert hashlib.sha1(file.read_bytes()).digest() == entry['hash']
        if 'cdhash' in entry:
            detail = run('codesign', '-d', '--verbose=4', file, capture_output=True, text=True)
            cdhash = re.search(r'^CDHash=([a-f0-9]+)$', detail.stderr, re.M)
            assert cdhash and bytes.fromhex(cdhash[1]) == entry['cdhash']
    helper = out/'linkage.cpp'
    helper.write_text('''#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <cstdio>
#include <cstring>
int main(int argc,char **argv) {
    if(argc!=2)return 1;
    void *handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!handle){std::puts(dlerror());return 2;}
    bool original=false,proxy=false;
    for(uint32_t i=0;i<_dyld_image_count();++i){
        const char *p=_dyld_get_image_name(i);
        if(std::strstr(p,"/impostor-original.dylib"))original=true;
        if(std::strstr(p,"/impostor.dylib"))proxy=true;
        if(std::strstr(p,"/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver"))return 3;
    }
    Dl_info info={};
    void *symbol=dlsym(handle,"xe_install_descriptor_placement");
    if(!original||!proxy||!symbol||!dladdr(symbol,&info)||
       !std::strstr(info.dli_fname,"/impostor-original.dylib"))return 4;
    std::puts("PASS: original companion exports preserved; Apple Metal driver not loaded.");return 0;
}
''')
    run(*common, helper, '-o', out/'linkage')
    run(out/'linkage', target/'impostor.dylib')
    inventory = {str(p.relative_to(candidate)): sha(p) for p in candidate.rglob('*') if p.is_file()}
    manifest = dict(status='prepared; sanitizer and exact-donor CPU replay passed; not deployed or hardware verified',
                    abiRevision=1, donorSHA256=DONOR, originalCompanionSHA256=BRIDGE,
                    immutableTextSHA256=TEXT, darwin='25.6.0', physicalGpu='8086:a7a0',
                    requirement='WEX WebKit GPU policy must admit the exact impostor-original.dylib path',
                    scope='native dealloc of uncached sole-buffer IGPoshUsedAsTarget allocation',
                    signatureValidation='ad-hoc proxy/main and sealed resource hashes verified; imported Mod chain unchanged',
                    artifacts=inventory)
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps({k:v for k,v in manifest.items() if k!='artifacts'}, indent=2))

if __name__ == '__main__':
    main()
