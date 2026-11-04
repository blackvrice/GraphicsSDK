"""Run a same-adapter Metal draw diagnostic and independently replay its evidence."""

import argparse
import csv
import json
import os
import pathlib
import platform
import re
import shlex
import shutil
import statistics
import subprocess
import time

from audit_licenses import sdk_fingerprint
from linux_comparison import GIT, ROOT, cache, read, record, require, save, sha, unchanged
from macos_core import exact


BENCH = 'benchmarks/macos_metal_comparison'
PINS = {
    'SDL': ('3b2bc951500ca6a1f428af77e036e732d9a8afe3', 'Zlib', '1c040b8271b37e5076359f8fd54240e371114112924d2df81ef87c7d6a1dfdfd'),
    'LLGL': ('649c0cc8fa608312c5e4b94dae409a5859fc7553', 'BSD-3-Clause', '03f822e8b006db9cc9a2e9150046b6d52463f0b620727f0ea4d37d6dd7302033'),
}
ADAPTERS = {'sdk': ('graphicssdk', 'explicit-registry-id'), 'sdl': ('sdl3', 'pinned-default-selection-with-single-adapter'),
            'llgl': ('llgl', 'injected-and-observed-native-registry-id')}
DRAWS = (1, 128, 1024)
SAMPLES = 12
PROFILE = {'MTL_DEBUG_LAYER': '1', 'MTL_DEBUG_LAYER_ERROR_MODE': 'assert', 'MTL_DEBUG_LAYER_WARNING_MODE': 'ignore'}


def reference(draws):
    left = 5 + 4*DRAWS.index(draws)
    return b''.join(bytes((255, 0, 0, 255)) if left <= x < left+19 and 3 <= y < 14 else bytes((0, 0, 255, 255))
                    for y in range(32) for x in range(64))


def timing(path):
    fields = ['draws', 'sample', 'record_ns', 'submit_ns', 'queue_idle_wait_ns']
    with path.open(encoding='utf-8', newline='') as file:
        reader = csv.DictReader(file)
        require(reader.fieldnames == fields, 'Unexpected timing fields')
        rows = list(reader)
    require(len(rows) == len(DRAWS)*SAMPLES, 'Incomplete timing sample matrix')
    summary = {}
    for index, draws in enumerate(DRAWS):
        values = []
        for sample, row in enumerate(rows[index*SAMPLES:(index+1)*SAMPLES]):
            require(set(row) == set(fields) and all(v and re.fullmatch(r'[0-9]+', v) for v in row.values()), 'Invalid raw timing values')
            require(int(row['draws']) == draws and int(row['sample']) == sample, 'Timing case/sample order differs')
            intervals = [int(row[k]) for k in fields[2:]]
            require(0 < sum(intervals) < 120_000_000_000, 'Invalid diagnostic interval total')
            values.append(intervals)
        summary[str(draws)] = dict(host_record_submit_median_ns=statistics.median(a+b for a,b,_ in values),
                                  host_queue_wait_median_ns=statistics.median(c for _,_,c in values))
    return summary


def case(out, key, mode, fp):
    name = f'{key}-{mode}'
    result = read(out/'commands'/f'{name}.stdout')
    adapter, identity = ADAPTERS[key]
    measured = mode == 'timing'
    exact(result, dict(adapter=adapter, identity=identity, mode=mode, images=3, timing_samples=36 if measured else 0))
    if key == 'sdk':
        exact(result, dict(SDK_source_fingerprint=fp))
    require(re.fullmatch(r'[0-9a-f]{16}', result['registry_id']) and int(result['registry_id'], 16), 'Missing native registry identity')
    require(type(result['name']) is str and result['name'] and result['target_storage'] in ('private', 'shared', 'managed'), 'Missing native adapter/storage metadata')
    require(type(result['mac1']) is bool and type(result['mac2']) is bool, 'Missing native Mac family support')
    if key == 'sdl':
        require(result['mac1'] or result['mac2'], 'SDL Metal requires observed Mac family support')
        exact(result, dict(mac1_compatibility=result['mac1'] and not result['mac2']))
    stderr = (out/'commands'/f'{name}.stderr').read_text(encoding='utf-8')
    require(('Metal API Validation Enabled' in stderr) == (not measured), f'{name} validation state differs')
    require(not re.search(r'failed assertion|Metal (?:API )?Validation Error', stderr, re.IGNORECASE), f'{name} emitted an API validation error')
    folder = out/name
    files = {f'{count}.rgba' for count in DRAWS}
    require({p.name for p in folder.iterdir()} == (files | {'samples.csv'} if measured else files), 'Unexpected image/timing outputs')
    for count in DRAWS:
        require((folder/f'{count}.rgba').read_bytes() == reference(count), f'{name} full RGBA reference differs')
    return result, timing(folder/'samples.csv') if measured else None


def inventory(folder):
    return [dict(path=p.relative_to(folder).as_posix(), bytes=p.stat().st_size, sha256=sha(p))
            for p in sorted(folder.rglob('*'), key=lambda p:p.as_posix()) if p.is_file() and p != folder/'receipt.json']


def verify_build(out, arch):
    exact(cache(out/'CMakeCache.txt'), dict(CMAKE_BUILD_TYPE='Release', CMAKE_OSX_ARCHITECTURES=arch,
                                           CMAKE_INTERPROCEDURAL_OPTIMIZATION='OFF', CMAKE_UNITY_BUILD='OFF',
                                           SDL_SHARED='OFF', SDL_STATIC='ON', SDL_METAL='ON', SDL_VULKAN='OFF',
                                           LLGL_BUILD_STATIC_LIB='ON', LLGL_BUILD_RENDERER_METAL='ON', LLGL_BUILD_RENDERER_VULKAN='OFF'))
    exact(cache(out/'sdk-CMakeCache.txt'), dict(GFX_ENABLE_METAL='ON', GFX_ENABLE_D3D12='OFF', GFX_ENABLE_VULKAN='OFF'))
    rows = read(out/'compile_commands.json')
    sdk = read(out/'sdk-compile_commands.json')
    units = ('/.deps/SDL/src/gpu/metal/SDL_gpu_metal.m', '/.deps/LLGL/sources/Renderer/Metal/MTRenderSystem.mm',
             '/src/backends/metal/Metal.cpp')
    selected = []
    for unit in units:
        matches = [r for r in (*rows, *sdk) if r['file'].endswith(unit)]
        require(len(matches) == 1, f'Expected one native implementation of {unit}')
        selected.extend(matches)
    adapters = [r for r in rows if r['file'].endswith(f'/{BENCH}/Compare.mm')]
    require(len(adapters) == 3, 'Expected all three independently linked comparison adapters')
    for row in (*selected, *adapters):
        args = shlex.split(row['command'])
        require('-arch' in args and args[args.index('-arch')+1] == arch and '-O2' in args and '-DNDEBUG' in args
                and not any(a in ('-O0','-O1','-O3','-Os','-Oz','-Ofast') or a.startswith('-flto') for a in args),
                'Comparison build architecture or optimization differs')


def check(out):
    receipt = read(out/'receipt.json')
    exact(receipt, dict(schema_version=1, scope='macos-metal-draw-diagnostic-v1',
                        physical_GPU_accepted=False, performance_accepted=False, GPU_timestamp_executed=False))
    require(receipt['architecture'] in ('arm64', 'x86_64') and re.fullmatch(r'[0-9a-f]{40}', receipt['source_commit']), 'Invalid source/architecture')
    require(receipt['files'] == inventory(out), 'Comparison evidence file inventory differs')
    for name, (commit, license_name, digest) in PINS.items():
        exact(receipt['competitors'][name], dict(commit=commit, license=license_name, license_sha256=digest))
        require(sha(out/f'{name}-LICENSE.txt') == digest, f'{name} license evidence differs')
    require(sha(out/'Draw.metal') == receipt['fixture_sha256'], 'Common shader differs')
    verify_build(out, receipt['architecture'])
    expected = ['os', 'xcode', 'sdk-path', 'sdk-configure', 'sdk-build', 'sdk-install', 'configure', 'build',
                *(f'{key}-arch' for key in ADAPTERS), *(f'{key}-{mode}' for mode in ('reference', 'timing') for key in ADAPTERS)]
    rows = read(out/'commands.json')
    require([r['name'] for r in rows] == expected, 'Missing or reordered comparison commands')
    last = 0
    for row in rows:
        require(type(row['pid']) is int and row['pid'] > 0 and last <= row['started'] <= row['finished'] and row['returncode'] == 0,
                'Overlapping or failed comparison process')
        require(row['validation_environment'] == (PROFILE if row['name'].endswith('-reference') else {}), 'Validation/timing profile drift')
        last = row['finished']
    info, intervals = {}, {}
    for key in ADAPTERS:
        require((out/'commands'/f'{key}-arch.stdout').read_text().strip() == receipt['architecture'], 'Binary architecture differs')
        baseline, _ = case(out, key, 'reference', receipt['SDK_source_fingerprint'])
        measured, intervals[key] = case(out, key, 'timing', receipt['SDK_source_fingerprint'])
        identity_fields = ('registry_id', 'name', 'identity', 'target_storage', 'mac1', 'mac2')
        require(all(baseline[k] == measured[k] for k in identity_fields), 'Reference and timing devices differ')
        info[key] = {k: baseline[k] for k in identity_fields}
    require(len({(r['registry_id'], r['name'], r['mac1'], r['mac2']) for r in info.values()}) == 1, 'Competitors used different Metal adapters')
    return dict(architecture=receipt['architecture'], SDK_source_fingerprint=receipt['SDK_source_fingerprint'], adapters=info,
                images=18, verified_RGBA_bytes=147456, timing_samples=108, intervals=intervals,
                physical_GPU_accepted=False, performance_accepted=False, GPU_timestamp_executed=False)


def run(folder):
    require(platform.system() == 'Darwin' and platform.machine() in ('arm64', 'x86_64'), 'Native macOS required')
    out = folder.resolve()
    require(out.is_relative_to(ROOT/'out') and not out.exists(), 'Use a fresh evidence folder under out/')
    build, sdk_build, install = [out.with_name(out.name+'-'+suffix) for suffix in ('build', 'sdk-build', 'install')]
    require(not any(p.exists() for p in (build, sdk_build, install)), 'Use fresh SDK and comparison build trees')
    require(not subprocess.check_output([*GIT, 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, text=True).strip(), 'Tracked checkout must be clean')
    (out/'commands').mkdir(parents=True)
    arch = platform.machine()
    fp = sdk_fingerprint(ROOT)
    commit = subprocess.check_output([*GIT, 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    frozen = [record(ROOT/name) for name in (f'{BENCH}/Draw.metal', f'{BENCH}/Compare.mm', f'{BENCH}/CMakeLists.txt', 'tools/macos_metal_comparison.py')]
    for name in ('Draw.metal', 'Compare.mm', 'CMakeLists.txt'):
        shutil.copyfile(ROOT/BENCH/name, out/name)
    competitors = {}
    for name, (rev, license_name, digest) in PINS.items():
        dep = ROOT/'.deps'/name
        require(subprocess.check_output([*GIT, 'rev-parse', 'HEAD'], cwd=dep, text=True).strip() == rev, f'{name} commit differs')
        require(not subprocess.check_output([*GIT, 'status', '--porcelain'], cwd=dep, text=True).strip(), f'{name} checkout is dirty')
        notice = dep/'LICENSE.txt'
        require(sha(notice) == digest, f'{name} license differs')
        shutil.copyfile(notice, out/f'{name}-LICENSE.txt')
        competitors[name] = dict(commit=rev, license=license_name, license_sha256=digest)

    env = {k:v for k,v in os.environ.items() if not k.startswith(('MTL_', 'METAL_', 'DYLD_', 'LD_', 'GFX_', 'SDL_'))
           and k not in ('CC','CXX','OBJC','OBJCXX','CFLAGS','CXXFLAGS','OBJCFLAGS','OBJCXXFLAGS','CPPFLAGS','LDFLAGS','SDKROOT','CMAKE_PREFIX_PATH','CPATH','LIBRARY_PATH')}
    rows = []
    def call(name, args, profile=None, timeout=600):
        args = list(map(str, args))
        profile = profile or {}
        print(f'Running {name}', flush=True)
        with (out/'commands'/f'{name}.stdout').open('wb') as stdout, (out/'commands'/f'{name}.stderr').open('wb') as stderr:
            start = time.monotonic()
            with subprocess.Popen(args, cwd=ROOT, env={**env, **profile}, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr) as child:
                try:
                    child.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
                    raise
        rows.append(dict(name=name, argv=args, pid=child.pid, started=start, finished=time.monotonic(),
                         returncode=child.returncode, validation_environment=profile))
        save(out/'commands.json', rows)
        stdout = (out/'commands'/f'{name}.stdout').read_text(errors='replace')
        stderr = (out/'commands'/f'{name}.stderr').read_text(errors='replace')
        require(child.returncode == 0, f'{name} exit {child.returncode}:\n{stdout[-6500:]}\n{stderr[-6500:]}')
        return stdout.strip()

    call('os', ['sw_vers'])
    call('xcode', ['xcodebuild', '-version'])
    sdk = call('sdk-path', ['xcrun', '--sdk', 'macosx', '--show-sdk-path'])
    flags = ['-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_OSX_ARCHITECTURES='+arch,'-DCMAKE_OSX_SYSROOT='+sdk,
             '-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF','-DCMAKE_UNITY_BUILD=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
    flags += [f'-DCMAKE_{language}_FLAGS_RELEASE=-O2 -DNDEBUG' for language in ('C','CXX','OBJC','OBJCXX')]
    call('sdk-configure', ['cmake','-S',ROOT,'-B',sdk_build,*flags,'-DGFX_ENABLE_METAL=ON','-DGFX_ENABLE_VULKAN=OFF',
                           '-DGFX_ENABLE_D3D12=OFF','-DGFX_BUILD_TESTS=OFF','-DGFX_BUILD_EXAMPLES=OFF','-DCMAKE_INSTALL_LIBDIR=lib'])
    call('sdk-build', ['cmake','--build',sdk_build,'--parallel','4'])
    call('sdk-install', ['cmake','--install',sdk_build,'--prefix',install])
    call('configure', ['cmake','-S',ROOT/BENCH,'-B',build,*flags,'-DCMAKE_PREFIX_PATH='+str(install),
                       '-DSDL_SOURCE='+str(ROOT/'.deps/SDL'),'-DLLGL_SOURCE='+str(ROOT/'.deps/LLGL')])
    call('build', ['cmake','--build',build,'--parallel','4'])
    for key in ADAPTERS:
        call(f'{key}-arch', ['lipo','-archs',build/f'metal_{key}'])
        frozen.append(record(build/f'metal_{key}'))
    device_identity = None
    for mode in ('reference', 'timing'):
        for key in ADAPTERS:
            name = f'{key}-{mode}'
            call(name, [build/f'metal_{key}',mode,ROOT/BENCH/'Draw.metal',out/name], PROFILE if mode == 'reference' else {}, timeout=120)
            result, _ = case(out, key, mode, fp)
            observed = (result['registry_id'], result['name'], result['mac1'], result['mac2'])
            if device_identity is None:
                device_identity = observed
            require(observed == device_identity, f'{name} selected a different Metal adapter')
    for path in ('CMakeCache.txt','compile_commands.json'):
        shutil.copyfile(build/path, out/path)
        shutil.copyfile(sdk_build/path, out/('sdk-'+path))
    require(unchanged(frozen) and sdk_fingerprint(ROOT) == fp, 'Comparison inputs changed during execution')
    save(out/'receipt.json', dict(schema_version=1, scope='macos-metal-draw-diagnostic-v1', source_commit=commit,
                                  github_run_id=os.environ.get('GITHUB_RUN_ID'), architecture=arch, SDK_source_fingerprint=fp,
                                  fixture_sha256=sha(ROOT/BENCH/'Draw.metal'), competitors=competitors, frozen_inputs=frozen,
                                  files=inventory(out), physical_GPU_accepted=False, performance_accepted=False, GPU_timestamp_executed=False))
    return check(out)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('run','check'))
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.output) if args.stage == 'run' else check(args.output.resolve()), indent=2))
