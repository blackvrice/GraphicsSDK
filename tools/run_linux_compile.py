"""Compile the real SDK on Linux; CPU contracts only, never GPU acceptance.

Uses reviewed headers already present in .deps, without downloading dependencies.
Requires fresh preset build roots and preserves every subprocess output on failure.
"""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

from audit_licenses import sdk_fingerprint

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRESETS = ('linux-vulkan', 'linux-vulkan-validation')
CPU_TESTS = ('core_contracts', 'adapter_contracts', 'memory_budget_contracts',
             'vertex_layout_contracts', 'binding_layout_contracts', 'render_pass_contracts',
             'spirv_interface_contracts', 'spirv_vertex_contracts', 'spirv_binding_contracts',
             'vulkan_timestamp_math', 'vulkan_creation_ownership')
HEADERS = 'b379292b2ab6df5771ba9870d53cf8b2c9295daf'

def require(value, message):
    if not value:
        raise ValueError(message)

def record(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++-14')
    args = parser.parse_args()
    require(platform.system() == 'Linux', 'Actual Linux host required; no simulated CMAKE_SYSTEM_NAME')
    require(shutil.which(args.compiler) and shutil.which('cmake') and shutil.which('ninja'), 'Native compiler/CMake/Ninja required')
    output = ROOT/'out/linux-vulkan-compile-evidence'
    require(not output.exists(), 'Evidence root already exists; preserve it and use a fresh checkout')
    require(all(not (ROOT/'out'/name).exists() for name in PRESETS), 'Preset roots must be fresh')
    output.mkdir(parents=True)
    commands = []
    def run(name, argv):
        start = time.monotonic()
        child = subprocess.run(argv, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=600)
        (output/(name+'.stdout')).write_bytes(child.stdout)
        (output/(name+'.stderr')).write_bytes(child.stderr)
        commands.append(dict(name=name, argv=argv, returncode=child.returncode, seconds=time.monotonic()-start))
        (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
        if child.returncode:
            print(child.stdout.decode('utf-8',errors='replace')[-12000:], flush=True)
            print(child.stderr.decode('utf-8',errors='replace')[-12000:], file=sys.stderr, flush=True)
        require(child.returncode == 0, name+' failed; raw diagnostics preserved')
        print(name+' PASS', flush=True)
        return child.stdout.decode('utf-8')
    source_commit = run('source-commit', ['git','rev-parse','HEAD']).strip()
    require(not os.environ.get('GITHUB_SHA') or os.environ['GITHUB_SHA']==source_commit, 'CI/checkout revision differs')
    require(not run('source-status',['git','status','--porcelain','--untracked-files=no']).strip(), 'Tracked checkout is modified')
    headers_root = ROOT/'.deps/Vulkan-Headers'
    require(run('headers-commit',['git','-C',str(headers_root),'rev-parse','HEAD']).strip()==HEADERS, 'Unreviewed headers revision')
    require(not run('headers-status',['git','-C',str(headers_root),'status','--porcelain']).strip(), 'Modified headers checkout')
    environment = dict(os_release=platform.freedesktop_os_release(), kernel=platform.release(),
                       machine=platform.machine(), compiler=run('compiler-version',[args.compiler,'--version']).strip(),
                       cmake=run('cmake-version',['cmake','--version']).strip(),
                       ninja=run('ninja-version',['ninja','--version']).strip())
    native_fingerprint = sdk_fingerprint(ROOT)
    profiles=[]
    for name in PRESETS:
        run('configure-'+name,['cmake','--preset',name,'-DCMAKE_CXX_COMPILER='+args.compiler,'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        run('build-'+name,['cmake','--build','--preset',name,'--verbose'])
        build = ROOT/'out'/name
        compile_commands = json.loads((build/'compile_commands.json').read_text())
        library_commands = [row for row in compile_commands if 'GraphicsSDKCore.dir/' in row['command']]
        require(library_commands and all(native_fingerprint in row['command'] for row in library_commands), 'Compiled SDK fingerprint differs')
        cache = (build/'CMakeCache.txt').read_text()
        for value in ('GFX_ENABLE_D3D12:BOOL=OFF','GFX_ENABLE_VULKAN:BOOL=ON','GFX_BUILD_TESTS:BOOL=ON',
                      'CMAKE_BUILD_TYPE:STRING=Release','CMAKE_CXX_COMPILER:FILEPATH='):
            require(value in cache, 'Native preset cache differs: '+value)
        expected_validation = 'ON' if name.endswith('-validation') else 'OFF'
        require('GFX_VULKAN_VALIDATION_TESTS:BOOL='+expected_validation in cache, 'Validation profile differs')
        junit = output/(name+'-cpu.xml')
        run('cpu-'+name,['ctest','--preset',name,'-R','^('+'|'.join(CPU_TESTS)+')$','--output-junit',str(junit)])
        cases=ET.parse(junit).getroot().findall('.//testcase')
        require(len(cases)==len(CPU_TESTS) and {c.attrib['name'] for c in cases}==set(CPU_TESTS), 'CPU test set differs')
        require(all(not any(c.find(tag) is not None for tag in ('failure','error','skipped')) for c in cases), 'CPU failures/skips')
        for source,label in ((build/'compile_commands.json','compile_commands'),(build/'CMakeCache.txt','cache')):
            shutil.copyfile(source,output/(name+'-'+label+'.txt'))
        library = build/'libGraphicsSDKCore.a'
        require(library.is_file(), 'Real native static library missing')
        profiles.append(dict(preset=name, library=record(library), SDK_compile_units=len(library_commands),
                             CPU_tests=len(cases), GPU_tests=0, validation_tests_executed=False))
    require(sdk_fingerprint(ROOT)==native_fingerprint, 'Sources changed during native build')
    receipt = dict(schema_version=1, task='P3-LNX-03', source_commit=source_commit,
                   execution='actual Linux configure/compiler/linker and CPU contract executables',
                   native_SDK_source_fingerprint=native_fingerprint, platform=environment,
                   headers_commit=HEADERS, profiles=profiles,
                   github_run_id=os.environ.get('GITHUB_RUN_ID'), github_run_attempt=os.environ.get('GITHUB_RUN_ATTEMPT'),
                   runner=record(pathlib.Path(__file__).resolve()),
                   input_files=[record(ROOT/p) for p in ('CMakeLists.txt','CMakePresets.json','.github/workflows/linux-vulkan-compile.yml')],
                   GPU_runtime_accepted=False, validation_layer_execution_accepted=False, WSI_accepted=False,
                   installed_consumer_accepted=False, physical_memory_acceptance=False,
                   fingerprint_note='Fingerprint hashes actual checkout bytes; LF/CRLF differences may change it relative to the historical Windows build.',
                   diagnostics=[record(p) for p in sorted(output.iterdir()) if p.is_file()])
    (output/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('GFX_LINUX_COMPILE_RECEIPT='+json.dumps(receipt,ensure_ascii=True,separators=(',',':')),flush=True)

if __name__=='__main__':
    main()
