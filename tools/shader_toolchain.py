"""Build and verify the offline P5-SHD-01 shader toolchain profile."""

import argparse
import os
import pathlib
import platform
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

from linux_comparison import GIT, ROOT, read, require, save, sha


LOCK = ROOT/'tools/shader-toolchain.lock.json'
FIXTURE = ROOT/'tests/fixtures/shader_toolchain/triangle.hlsl'
STAGES = (('vert', 'vertexMain', 'vs_6_0', 'vertex', 'gfx_vertex'),
          ('frag', 'pixelMain', 'ps_6_0', 'fragment', 'gfx_fragment'))
SCOPE = 'p5-shd-01-offline-v1'
FLAGS = ('-HV', '2021', '-O3', '-WX', '-Ges')


def inventory(folder):
    return {p.relative_to(folder).as_posix(): sha(p) for p in sorted(folder.rglob('*'))
            if p.is_file() and p != folder/'receipt.json'}


def load_dxc(pin, system, archive, work, out):
    asset = pin[system]
    if archive is None:
        archive = work/asset['asset']
        url = f"https://github.com/{pin['repository']}/releases/download/{pin['tag']}/{asset['asset']}"
        with urllib.request.urlopen(url, timeout=120) as response, archive.open('wb') as destination:
            shutil.copyfileobj(response, destination)
    require(sha(archive) == asset['sha256'], 'DXC release archive SHA-256 differs from the lock')
    paths = asset['tools'] + asset['notices']
    root = work/'dxc'
    if system == 'Windows':
        with zipfile.ZipFile(archive) as package:
            files = {name: package.read(name) for name in paths}
    else:
        with tarfile.open(archive, 'r:gz') as package:
            files = {}
            for name in paths:
                member = package.getmember('./'+name)
                require(member.isfile(), f'DXC member is not a regular file: {name}')
                with package.extractfile(member) as file:
                    files[name] = file.read()
    for name, data in files.items():
        target = root/name if name in asset['tools'] else out/'notices/DXC'/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        if name in asset['tools'] and system == 'Linux':
            target.chmod(0o755)
    return root, {name: sha(root/name) for name in asset['tools']}


def build_spirv(sources, work, out, invoke, jobs):
    for name, pin in sources.items():
        source = work/name
        source.mkdir()
        invoke(name+'-init', [*GIT, 'init', '--quiet', source])
        invoke(name+'-fetch', [*GIT, '-C', source, 'fetch', '--depth=1',
                               f"https://github.com/{pin['repository']}.git", pin['commit']])
        invoke(name+'-checkout', [*GIT, '-C', source, 'checkout', '--quiet', '--detach', 'FETCH_HEAD'])
        revision = invoke(name+'-revision', [*GIT, '-C', source, 'rev-parse', 'HEAD']).strip()
        require(revision == pin['commit'], f'{name} checkout differs from lock')
        notice = out/'notices'/name/pin['notice']
        notice.parent.mkdir(parents=True)
        shutil.copyfile(source/pin['notice'], notice)
    cross = work/'cross-build'
    tools = work/'tools-build'
    configs = [
        ('cross', 'SPIRV-Cross', cross, 'spirv-cross',
         ['-DSPIRV_CROSS_ENABLE_TESTS=OFF', '-DSPIRV_CROSS_CLI=ON']),
        ('tools', 'SPIRV-Tools', tools, 'spirv-val',
         ['-DSPIRV_SKIP_TESTS=ON', f'-DSPIRV-Headers_SOURCE_DIR={work/"SPIRV-Headers"}']),
    ]
    for name, source, build, target, options in configs:
        invoke(name+'-configure', ['cmake', '-S', work/source, '-B', build, '-G', 'Ninja',
                                   '-DCMAKE_BUILD_TYPE=Release', *options])
        invoke(name+'-build', ['cmake', '--build', build, '--target', target, '--parallel', str(jobs)])
    return cross/'spirv-cross', tools/'tools/spirv-val'


def verify_payload(out, system):
    for stage, _, _, qualifier, name in STAGES:
        dxil = (out/f'{stage}.dxil').read_bytes()
        require(len(dxil) >= 36 and dxil[:4] == b'DXBC', f'{stage}: missing DXIL container')
        size, count = struct.unpack_from('<2I', dxil, 24)
        require(size == len(dxil) and 0 < count <= (len(dxil)-32)//4, f'{stage}: invalid DXIL directory')
        offsets = struct.unpack_from(f'<{count}I', dxil, 32)
        require(any(32 + 4*count <= pos <= size-8 and dxil[pos:pos+4] == b'DXIL'
                    and 0 < struct.unpack_from('<I', dxil, pos+4)[0] <= size-pos-8
                    for pos in offsets), f'{stage}: missing DXIL program part')
        if system == 'Linux':
            spirv = (out/f'{stage}.spv').read_bytes()
            require(len(spirv) >= 20 and len(spirv) % 4 == 0 and
                    struct.unpack_from('<2I', spirv) == (0x07230203, 0x00010300),
                    f'{stage}: expected little-endian SPIR-V 1.3')
            msl = (out/f'{stage}.metal').read_text(encoding='utf-8')
            require(re.search(rf'\b{qualifier}\s+[^\n]+\b{name}\s*\(', msl) is not None,
                    f'{stage}: generated MSL entry point differs')


def run(out, archive, jobs):
    system = platform.system()
    require(system in ('Windows', 'Linux') and platform.machine().lower() in ('amd64', 'x86_64'),
            'Offline profile requires an x64 Windows or Linux host')
    lock = read(LOCK)
    require(lock['schema_version'] == 1, 'Unexpected shader toolchain lock schema')
    out.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(FIXTURE, out/FIXTURE.name)
    shutil.copyfile(LOCK, out/LOCK.name)
    source = subprocess.check_output([*GIT, '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    commands = {}
    env = dict(os.environ)
    env.pop('LD_PRELOAD', None)
    with tempfile.TemporaryDirectory(prefix='shader-tools-', dir=out.parent) as directory:
        work = pathlib.Path(directory)

        def invoke(label, args, failure=False):
            args = [str(value) for value in args]
            process = subprocess.run(args, cwd=out, env=env, capture_output=True, timeout=900)
            (out/f'{label}.stdout').write_bytes(process.stdout)
            (out/f'{label}.stderr').write_bytes(process.stderr)
            commands[label] = dict(argv=args, exit_code=process.returncode)
            require((process.returncode > 0 if failure else process.returncode == 0),
                    f'{label}: unexpected exit {process.returncode}; see {out}/{label}.stderr')
            return process.stdout.decode('utf-8', errors='replace')

        root, executables = load_dxc(lock['dxc'], system, archive, work, out)
        if system == 'Linux':
            env['LD_LIBRARY_PATH'] = str(root/'lib')
            dxc, dxv = root/'bin/dxc', root/'bin/dxv'
            invoke('cmake-version', ['cmake', '--version'])
            invoke('ninja-version', ['ninja', '--version'])
            cross, validator = build_spirv(lock['sources'], work, out, invoke, jobs)
            executables.update({'spirv-cross': sha(cross), 'spirv-val': sha(validator)})
            invoke('spirv-val-version', [validator, '--version'])
        else:
            dxc, dxv = root/'bin/x64/dxc.exe', root/'bin/x64/dxv.exe'
        invoke('dxc-version', [dxc, '--version'])
        for stage, entry, profile, _, msl_entry in STAGES:
            base = [dxc, *FLAGS, '-E', entry, '-T', profile, FIXTURE.name]
            invoke(stage+'-dxil', [*base, '-Fo', f'{stage}.dxil'])
            invoke(stage+'-dxv', [dxv, f'{stage}.dxil'])
            if system == 'Linux':
                spirv = f'{stage}.spv'
                invoke(stage+'-spirv', [*base, '-spirv', '-fspv-target-env=vulkan1.1', '-Fo', spirv])
                invoke(stage+'-spirv-val', [validator, '--target-env', 'vulkan1.1', spirv])
                invoke(stage+'-msl', [cross, spirv, '--msl', '--msl-version', '20100',
                                      '--rename-entry-point', entry, msl_entry, stage,
                                      '--output', f'{stage}.metal'])
        (out/'invalid.hlsl').write_bytes(b'float4 broken() : SV_Target0 { return missingSymbol; }\n')
        invoke('invalid-hlsl', [dxc, *FLAGS, '-E', 'broken', '-T', 'ps_6_0', 'invalid.hlsl',
                                '-Fo', 'invalid.dxil'], failure=True)
        if system == 'Linux':
            invoke('wrong-vulkan-target', [validator, '--target-env', 'vulkan1.0', 'frag.spv'], failure=True)
        require(not (out/'invalid.dxil').exists(), 'Rejected HLSL left a DXIL output')
        save(out/'commands.json', commands)
    verify_payload(out, system)
    receipt = dict(schema_version=1, scope=SCOPE, source_commit=source,
                   github_run_id=os.environ.get('GITHUB_RUN_ID'), system=system, architecture='x86_64',
                   runner_sha256=sha(pathlib.Path(__file__)),
                   dxc_release_sha256=lock['dxc'][system]['sha256'], executables=executables,
                   targets=['dxil', 'spirv', 'msl-source'] if system == 'Linux' else ['dxil'],
                   sdk_backend_executed=False, gpu_executed=False, files=inventory(out))
    save(out/'receipt.json', receipt)
    return check(out)


def check(out):
    receipt = read(out/'receipt.json')
    system = receipt['system']
    require(receipt['schema_version'] == 1 and receipt['scope'] == SCOPE and
            system in ('Windows', 'Linux') and receipt['architecture'] == 'x86_64' and
            receipt['sdk_backend_executed'] is False and receipt['gpu_executed'] is False,
            'Shader receipt scope differs')
    require(receipt['files'] == inventory(out), 'Shader evidence inventory or bytes differ')
    lock = read(out/LOCK.name)
    require(receipt['dxc_release_sha256'] == lock['dxc'][system]['sha256'], 'DXC release lock differs')
    expected_targets = ['dxil', 'spirv', 'msl-source'] if system == 'Linux' else ['dxil']
    require(receipt['targets'] == expected_targets, 'Shader target matrix differs')
    tools = set(lock['dxc'][system]['tools'])
    notices = {f'notices/DXC/{name}' for name in lock['dxc'][system]['notices']}
    version = (out/'dxc-version.stdout').read_text(encoding='utf-8')
    require(lock['dxc'][system]['reported_build'] in version, 'Reported DXC build differs from lock')
    commands = read(out/'commands.json')
    expected = {'dxc-version', *(f'{stage}-{kind}' for stage, *_ in STAGES for kind in ('dxil', 'dxv'))}
    failures = {'invalid-hlsl': 'missingSymbol'}
    if system == 'Linux':
        tools.update(('spirv-cross', 'spirv-val'))
        for name, pin in lock['sources'].items():
            notices.add(f"notices/{name}/{pin['notice']}")
            require((out/f'{name}-revision.stdout').read_text(encoding='utf-8').strip() == pin['commit'],
                    f'{name} recorded checkout differs from lock')
        expected.update({'spirv-val-version', *(f'{stage}-{kind}' for stage, *_ in STAGES
                                                for kind in ('spirv', 'spirv-val', 'msl'))})
        failures['wrong-vulkan-target'] = 'version'
    require(set(receipt['executables']) == tools and
            all(re.fullmatch('[0-9a-f]{64}', digest) for digest in receipt['executables'].values()),
            'Shader tool binary identity differs')
    require({name for name in receipt['files'] if name.startswith('notices/')} == notices,
            'Shader tool license inventory differs')
    require(all(commands[name]['exit_code'] == 0 for name in expected), 'Shader command failed')
    for name, diagnostic in failures.items():
        require(commands[name]['exit_code'] > 0 and diagnostic in
                (out/f'{name}.stderr').read_text(encoding='utf-8', errors='replace'),
                f'Expected compiler/validator diagnostic missing: {name}')
    require(not (out/'invalid.dxil').exists(), 'Rejected HLSL has an output')
    verify_payload(out, system)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('run', 'check'))
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--dxc-archive', type=pathlib.Path)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    require(args.jobs > 0, 'Build jobs must be positive')
    out = args.output.resolve()
    receipt = run(out, args.dxc_archive, args.jobs) if args.action == 'run' else check(out)
    print(f"PASS {receipt['system']} {receipt['architecture']}: {', '.join(receipt['targets'])}")


if __name__ == '__main__':
    main()
