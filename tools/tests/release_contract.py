"""Offline U2/U3 controls: generated header inclusion and immutable release preflight.
No GitHub calls, real signing keys, uploads, workflow dispatches or release builds.
"""
from pathlib import Path
import importlib.util
import io
import json
import os
import shutil
import subprocess
import tempfile
import urllib.error
ROOT = Path(__file__).resolve().parents[2]

def module(path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value

headers = module(ROOT / 'Telegram/build/write_packer_public.py')
preflight = module(ROOT / 'Telegram/build/check_release_mutability.py')
packer = (ROOT / 'Telegram/SourceFiles/_other/packer.cpp').read_text('utf-8')
include = packer[packer.index('#ifndef PACKER_DISABLE_PRIVATE'):packer.index('\nQString countAlphaVersionSignature')]
guard = packer[packer.index('#ifdef PACKER_V2_ONLY\n\tif (AlphaVersion'):packer.index('\n\tif (OnlyAlphaKey)')]
canary = (ROOT / '.github/workflows/canary.yml').read_text('utf-8')
assert canary.count('write_packer_public.py') == 3
assert 'DesktopPrivate/packer_private.h' not in canary and 'random_key()' not in canary
release = (ROOT / '.github/workflows/release.yml').read_text('utf-8')
assert release.index('Refuse published versions') < release.index('  windows-libraries:')
assert release.index('Recheck before changing') < release.index('Publish update packages')
assert 'gh release edit "$TAG" --draft' not in release and '--clobber' not in release
assert 'group: luxury-release-${{ inputs.tag }}' in release
assert release.index('python3 Telegram/build/check_release_mutability.py "$TAG"\n') < release.index('gh release edit "$TAG"')
# 7.3 compiles the generated wallet_engine.cpp binding from the library cache;
# a prune that drops it fails configure on every warm Windows build.
prune = next(l for l in canary.splitlines() if 'run: find $LibrariesPath' in l).strip()
assert "-path '*/src/wallet_engine/*'" in prune
win = (ROOT / '.github/workflows/win.yml').read_text('utf-8')
for workflow in (win, release):
    assert prune in [l.strip() for l in workflow.splitlines()]
assert '-libs-v2-' not in win and '-release-libs-v2-' not in release
# The MSBuild legs ran out of compiler heap with a cl.exe per core; only
# they get the cap, Ninja and the Ninja-built release take no MSBuild flags.
assert '[ -z "$GENERATOR" ] && MSBUILD_ARGS="-- /p:CL_MPCount=2"' in win
assert 'cmake --build ..\\out --config Debug --parallel %TDESKTOP_BUILD_NATIVE%' in win
assert win.count('cmake --build') == 1
assert 'TDESKTOP_BUILD_GENERATOR=-G "Ninja Multi-Config"' in release
# Both Windows legs build on D:; C: is too small for a cold prepare.
assert release.count('mklink /d %GITHUB_WORKSPACE%\\TBuild D:\\TBuild') == 2
assert '%userprofile%\\TBuild' not in release
assert release.count('mkdir D:\\TBuild\\Libraries') == 2
assert release.count('echo TBUILD=%GITHUB_WORKSPACE%\\TBuild>>%GITHUB_ENV%') == 2
# Prepare keys hold absolute paths: the D: trees never share a C: cache.
assert release.count('-release-third-party-v1-') == 3 and '}-third-party-' not in release
assert '-x64-libs-v3-' not in release and '-x64-qt6-' not in release

def opener(product=None, updates=None, failure=None):
    def request(req, timeout):
        assert req.headers.get('Authorization') == 'Bearer synthetic-token' and timeout == 30
        if failure:
            if failure == 'network': raise OSError('offline')
            raise urllib.error.HTTPError(req.full_url, failure, 'synthetic', None, None)
        obj = updates if req.full_url.endswith('/updates') else product
        if obj is None: raise urllib.error.HTTPError(req.full_url,404,'absent',None,None)
        return io.BytesIO(json.dumps(obj).encode())
    return request

cases = 0
for p,u in [(None,None),({'draft':True},None),({'draft':True},{'draft':False,'assets':[{'name':'older-package'}]})]:
    assert preflight.check_release('test/test','luxury-v1.0.20','synthetic-token',['tx64upd1000020'],opener(p,u))['product'] == ('draft' if p else 'absent'); cases += 1
for p,u,error in [({'draft':False},None,None),({'draft':True},{'draft':False,'assets':[{'name':'tx64upd1000020'}]},None),({'draft':True},{'draft':False},None),({},None,None),({'draft':'true'},None,None),(None,None,401),(None,None,403),(None,None,429),(None,None,500),(None,None,'network')]:
    try: preflight.check_release('test/test','luxury-v1.0.20','synthetic-token',['tx64upd1000020'],opener(p,u,error))
    except preflight.ReleaseCheckError: cases += 1
    else: raise AssertionError((p,u,error))
with tempfile.TemporaryDirectory(prefix='luxury-release-contract-') as directory:
    work = Path(directory); source = work / 'Telegram/SourceFiles/_other'; private = work / 'DesktopPrivate'; headers.write_headers(source,private)
    assert 'PRIVATE KEY' not in (source / 'packer_private.h').read_text() and 'PRIVATE KEY' not in (private / 'alpha_private.h').read_text()
    cpp = '#include <iostream>\n#include <string>\n#include <optional>\nusing std::cout;\n' + include + '\nint main(int argc,char**) { bool AlphaVersion=argc==2, BetaChannel=argc==3, OnlyAlphaKey=argc==4; std::optional<int> V2Channel=argc==5?std::optional<int>(1):std::nullopt; struct { bool isEmpty() { return true; } } V2UnsignedFile;\n' + guard + '\nreturn 0; }\n'
    file = source / 'header.cpp'; file.write_text(cpp,encoding='utf-8'); exe = work / ('header.exe' if os.name == 'nt' else 'header')
    cxx = os.environ.get('CXX') or shutil.which('g++') or r'C:\Strawberry\c\bin\g++.exe'
    for mode in [[],['-DPACKER_DISABLE_PRIVATE']]:
        subprocess.run([cxx,'-std=c++20','-O0','-D_DEBUG',*mode,str(file),'-o',str(exe)],check=True)
        for n in range(5):
            result = subprocess.run([str(exe),*(['fixture']*n)])
            assert (result.returncode==0) == (n==4); cases += 1
print(f'PASS: {cases} offline release/header controls, actual helper and Packer guard; no native Packer/CI or GitHub mutations')
