"""Build a pinned, process-local Windows Vulkan validation prerequisite.

No registry, driver, system installation or persistent environment changes.
Third-party runtime binaries remain local and are not SDK/package payload.
"""
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'out/vulkan-validation-runtime'
LOCK = ROOT / 'tools/vulkan-validation.lock.json'

def main():
    if sys.platform != 'win32':
        raise RuntimeError('This pinned build profile is Windows x64 only')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['CMAKE_BUILD_PARALLEL_LEVEL'] = '1'
    # VVL adds /MP internally. Append /MP1 without modifying upstream sources.
    env['_CL_'] = (env.get('_CL_', '') + ' /MP1').strip()
    commands = []
    def run(name, argv):
        start = time.monotonic()
        with (OUTPUT / (name + '.log')).open('wb') as log:
            result = subprocess.run([str(a) for a in argv], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        commands.append(dict(name=name, argv=[str(a) for a in argv], returncode=result.returncode, seconds=time.monotonic()-start))
        (OUTPUT/'commands.json').write_bytes((json.dumps(commands, indent=2)+'\n').encode())
        print(name, 'exit=', result.returncode, 'seconds=', round(time.monotonic()-start, 1), flush=True)
        if result.returncode: raise RuntimeError(name + ' failed; inspect ' + str(OUTPUT/(name+'.log')))
    def git(path, *args):
        return subprocess.check_output(['git', '-c', 'gc.auto=0', '-c', 'maintenance.auto=false', '-C', str(path), *args], text=True).strip()
    lock = json.loads(LOCK.read_text())
    sources = {}
    for component in lock['components']:
        name = component['name']
        source = ROOT / '.deps' / (name if name in ('Vulkan-Headers', 'Vulkan-ValidationLayers') else 'vvl-validation/'+name)
        if not source.exists():
            run(name+'-init', ['git', 'init', source])
            run(name+'-remote', ['git', '-C', source, 'remote', 'add', 'origin', component['url']])
            run(name+'-fetch', ['git', '-c', 'gc.auto=0', '-C', source, 'fetch', '--depth', '1', 'origin', component['commit']])
            run(name+'-checkout', ['git', '-C', source, 'checkout', '--detach', 'FETCH_HEAD'])
        if git(source, 'rev-parse', 'HEAD') != component['commit'] or git(source, 'status', '--porcelain', '--untracked-files=no'):
            raise RuntimeError('Dependency is not the clean pinned checkout: '+name)
        sources[name] = source
    common = ['-G', 'Visual Studio 17 2022', '-A', 'x64', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_INSTALL_PREFIX='+(OUTPUT/'prerequisites').as_posix(), '-DCMAKE_PREFIX_PATH='+(OUTPUT/'prerequisites').as_posix(),
        '-DPython3_EXECUTABLE='+pathlib.Path(sys.executable).as_posix(), '-DPYTHON_EXECUTABLE='+pathlib.Path(sys.executable).as_posix()]
    for name in ('Vulkan-Headers', 'Vulkan-Utility-Libraries', 'SPIRV-Headers', 'SPIRV-Tools', 'robin-hood-hashing'):
        options = []
        if name == 'SPIRV-Tools':
            options = ['-DSPIRV-Headers_SOURCE_DIR='+sources['SPIRV-Headers'].as_posix(), '-DSPIRV_WERROR=OFF',
                       '-DSPIRV_SKIP_TESTS=ON', '-DSPIRV_SKIP_EXECUTABLES=ON']
        if name == 'robin-hood-hashing': options = ['-DRH_STANDALONE_PROJECT=OFF']
        build = OUTPUT/'build'/name
        run(name+'-configure', ['cmake', '-S', sources[name], '-B', build, *common, *options])
        run(name+'-build', ['cmake', '--build', build, '--config', 'Release', '--target', 'install', '--parallel', '1'])
    build = OUTPUT/'build/Vulkan-ValidationLayers'
    run('layer-configure', ['cmake', '-S', sources['Vulkan-ValidationLayers'], '-B', build, *common,
        '-DUPDATE_DEPS=OFF', '-DBUILD_TESTS=OFF', '-DUSE_MIMALLOC=OFF', '-DBUILD_WERROR=OFF',
        '-DCMAKE_CXX_FLAGS_RELEASE='+lock['configuration']['layer_release_flags']])
    run('layer-build', ['cmake', '--build', build, '--config', 'Release', '--parallel', '1'])
    artifacts = [p for p in build.rglob('*') if p.is_file() and p.name in ('VkLayer_khronos_validation.dll', 'VkLayer_khronos_validation.json')]
    if len(artifacts) != 2: raise RuntimeError('Missing/ambiguous layer binary or manifest')
    record = dict(lock_sha256=hashlib.sha256(LOCK.read_bytes()).hexdigest(), components=lock['components'],
        environment_overrides=dict(CMAKE_BUILD_PARALLEL_LEVEL='1', _CL_=env['_CL_']),
        configuration=lock['configuration'], artifacts=[dict(path=p.relative_to(ROOT).as_posix(),
        sha256=hashlib.sha256(p.read_bytes()).hexdigest(), bytes=p.stat().st_size) for p in sorted(artifacts)])
    (OUTPUT/'receipt.json').write_bytes((json.dumps(record, indent=2)+'\n').encode())
    print('PASS: pinned local Vulkan validation layer built; no system registration', flush=True)

if __name__ == '__main__':
    main()
