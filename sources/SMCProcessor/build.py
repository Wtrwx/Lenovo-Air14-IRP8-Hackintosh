#!/usr/bin/env python3
"""Build only the SMCProcessor plugin; never build or replace VirtualSMC."""
import argparse,hashlib,json,plistlib,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parent
def run(cmd):subprocess.run([str(a) for a in cmd],check=True,cwd=ROOT)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',required=True,type=Path);p.add_argument('--lilu',required=True,type=Path)
    p.add_argument('--output',type=Path,default=ROOT/'build')
    p.add_argument('--release',action='store_true')
    a=p.parse_args();sdk=a.sdk.resolve();lilu=a.lilu.resolve()/'Contents/Resources';out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    flags=['xcrun','clang++','-arch','x86_64','-std=c++17','-O2','-g','-fapple-kext','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-builtin','-fno-asynchronous-unwind-tables','-mno-red-zone','-nostdinc','-DKERNEL','-DKERNEL_PRIVATE','-DAPPLE_KEXT_ASSERTIONS=1','-DDRIVER_PRIVATE','-DAPPLE','-DNeXT','-D__APPLE__','-D__MACH__','-DPRODUCT_NAME=SMCProcessor','-DMODULE_VERSION=1.3.8','-DDEBUG=1','-I'+str(sdk/'Headers'),'-I'+str(lilu),'-I'+str(ROOT),'-mmacosx-version-min=10.7','-fno-common','-mmmx','-msse','-msse2','-msse3','-mssse3','-mfpmath=sse','-Wall','-Wextra','-Wno-unused-parameter','-Wno-deprecated-declarations','-Wno-vla','-Wno-ossharedptr-misuse','-fdebug-compilation-dir=.','-ffile-prefix-map='+str(ROOT)+'=/SMCProcessor','-ffile-prefix-map='+str(sdk)+'=/MacKernelSDK','-ffile-prefix-map='+str(lilu)+'=/Lilu']
    if a.release:flags.remove('-DDEBUG=1')
    objects=[]
    for name in ['SMCProcessor','KeyImplementations','module']:
        obj=out/(name+'.o');run(flags+['-c',ROOT/(name+'.cpp'),'-o',obj]);objects.append(obj)
    bundle=out/'SMCProcessor-RaptorP.kext/Contents';(bundle/'MacOS').mkdir(parents=True,exist_ok=True);exe=bundle/'MacOS/SMCProcessor'
    run(['xcrun','ld','-arch','x86_64','-kext','-undefined','dynamic_lookup','-no_fixup_chains','-macos_version_min','10.7','-o',exe,*objects,sdk/'Library/x86_64/libkmod.a'])
    info=plistlib.loads((ROOT/'Info.plist').read_bytes());(bundle/'Info.plist').write_bytes(plistlib.dumps(info,sort_keys=False))
    run(['xcrun','dsymutil',exe,'-o',out/'SMCProcessor-Trace.kext.dSYM']);run(['xcrun','strip','-S',exe])
    unresolved=subprocess.check_output(['xcrun','nm','-u',str(exe)],text=True)
    assert not any(s in unresolved for s in ['chkstk_darwin','___memcpy_chk','___memmove_chk','DispatchE5IORPC'])
    symbols=subprocess.check_output(['xcrun','nm',str(exe)],text=True)
    assert ' _kmod_info' in symbols and ' __realmain' in symbols and ' __start' in symbols
    result=dict(binarySHA256=hashlib.sha256(exe.read_bytes()).hexdigest(),upstream='VirtualSMC 1.3.8',VirtualSMCBuilt=False,sourceSHA256={str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in ROOT.rglob('*') if x.is_file() and 'build' not in x.relative_to(ROOT).parts and x.suffix in ['.cpp','.hpp','.h','.plist']})
    (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(result['binarySHA256'])
if __name__=='__main__':main()
