"""Run and replay the installed D3D12 DXIL consumer on Windows x64."""

import argparse
import os
import pathlib
import platform
import re
import shutil
import subprocess
import tempfile

from audit_licenses import sdk_fingerprint
from linux_comparison import GIT, ROOT, read, require, save, sha
from shader_cache import PROFILES, check_campaign, command_runner, compile_dxil, exercise_cache
from shader_toolchain import FIXTURE, LOCK, inventory, load_dxc, verify_payload


SCOPE = 'p5-shd-02-d3d12-warp-v1'
CONSUMER = ROOT/'tests/d3d12_dxil'


def reference():
    return b''.join(bytes((255, 64, 0, 255)) if 2 <= x < 10 and 1 <= y < 5 else bytes((0, 0, 255, 255))
                    for y in range(7) for x in range(13))


def check(out):
    receipt = read(out/'receipt.json')
    require(receipt['schema_version'] == 1 and receipt['scope'] == SCOPE and
            receipt['backend'] == 'D3D12' and receipt['software_adapter'] is True,
            'D3D12 DXIL receipt scope differs')
    require(receipt['files'] == inventory(out), 'D3D12 DXIL evidence inventory or bytes differ')
    pin = read(out/LOCK.name)['dxc']['Windows']
    require(receipt['dxc_release_sha256'] == pin['sha256'] and
            pin['reported_build'] in (out/'dxc-version.stdout').read_text(encoding='utf-8'),
            'D3D12 DXIL runtime differs from lock')
    require('PASS missing app-local DXC and working DXBC bootstrap' in
            (out/'missing-runtime.stdout').read_text(encoding='utf-8'), 'Missing DXC control failed')
    require('PASS D3D12 WARP (' in (out/'consumer.stdout').read_text(encoding='utf-8') and
            'missingSymbol' in (out/'invalid-hlsl.log').read_text(encoding='utf-8'),
            'Installed consumer or compiler diagnostics differ')
    for stage in ('vert', 'frag'):
        require('Validation succeeded.' in (out/f'{stage}-dxv.stdout').read_text(encoding='utf-8'),
                f'{stage}: Microsoft DXIL validation missing')
    verify_payload(out, 'Windows')
    for name in ('dxil', 'dxbc'):
        require((out/f'{name}.rgba').read_bytes() == reference(), f'{name} full RGBA reference differs')
    if (out/'cache-evidence/results.json').is_file():
        check_campaign(out/'cache-evidence', 'dxil', 'DXIL ps_6_0')
        require((out/'cached.rgba').read_bytes() == reference(), 'Cached DXIL full RGBA reference differs')
    return receipt


def run(out, archive):
    require(platform.system() == 'Windows' and platform.machine().lower() in ('amd64', 'x86_64'),
            'D3D12 DXIL runner requires Windows x64')
    out.mkdir(parents=True, exist_ok=False)
    pin = read(LOCK)['dxc']
    pin['Windows']['tools'].append('inc/dxcapi.h')
    revision = subprocess.check_output([*GIT, '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    fingerprint = sdk_fingerprint(ROOT)
    commands = {}

    def invoke(name, *args):
        args = [str(arg) for arg in args]
        process = subprocess.run(args, cwd=out, capture_output=True, timeout=900)
        commands[name] = dict(argv=args, exit_code=process.returncode)
        save(out/'commands.json', commands)
        (out/f'{name}.stdout').write_bytes(process.stdout)
        (out/f'{name}.stderr').write_bytes(process.stderr)
        require(process.returncode == 0, f'{name} failed ({process.returncode}); see logs in {out}')

    with tempfile.TemporaryDirectory(dir=out.parent, prefix='dxil-') as directory:
        work = pathlib.Path(directory)
        dxc, tools = load_dxc(pin, 'Windows', archive, work, out)
        sdk, package, source, build, missing = (work/name for name in ('sdk', 'package', 'source', 'build', 'missing'))
        shutil.copytree(CONSUMER, source)
        shutil.copyfile(ROOT/'tests/PipelineCacheSupport.hpp', source/'PipelineCacheSupport.hpp')
        shutil.copyfile(FIXTURE, source/FIXTURE.name)
        shutil.copytree(source, out/'consumer-source')
        shutil.copyfile(LOCK, out/LOCK.name)
        generator = ('-G', 'Visual Studio 17 2022', '-A', 'x64')
        invoke('sdk-configure', 'cmake', '-S', ROOT, '-B', sdk, *generator, '-DGFX_ENABLE_D3D12=ON',
               '-DGFX_BUILD_TESTS=OFF', '-DGFX_BUILD_EXAMPLES=OFF', f'-DGFX_DXC_INCLUDE_DIR={dxc/"inc"}')
        invoke('sdk-build', 'cmake', '--build', sdk, '--config', 'Release', '--parallel', '3')
        invoke('sdk-install', 'cmake', '--install', sdk, '--config', 'Release', '--prefix', package)
        configs = list((package/'lib/cmake/GraphicsSDK').glob('*.cmake'))
        require(configs and all('dxc' not in path.read_text(encoding='utf-8').lower() for path in configs),
                'Installed package exports a private DXC dependency')
        invoke('consumer-configure', 'cmake', '-S', source, '-B', build, *generator, f'-DPACKAGE_ROOT={package}')
        invoke('consumer-build', 'cmake', '--build', build, '--config', 'Release')
        binaries = build/'Release'
        consumer = binaries/'dxil_consumer.exe'
        missing.mkdir()
        shutil.copyfile(consumer, missing/consumer.name)
        invoke('missing-runtime', missing/consumer.name, '--missing-runtime')
        for dll in ('dxcompiler.dll', 'dxil.dll'):
            name = f'bin/x64/{dll}'
            shutil.copyfile(dxc/name, binaries/dll)
            require(sha(binaries/dll) == tools[name], f'Staged {dll} differs from the checked release')
        invoke('dxc-version', dxc/'bin/x64/dxc.exe', '--version')
        cache_out = out/'cache-evidence'
        cache_invoke = command_runner(cache_out)
        identity = {'dxc': pin['Windows']['sha256']}
        results = exercise_cache(cache_out, lambda source, label, cache: compile_dxil(
            dxc, source, 'pixelMain', 'frag', cache_out/f'{label}.dxil', work, cache_invoke, cache, identity))
        save(cache_out/'results.json', results)
        invoke('consumer', consumer, source/FIXTURE.name, out, cache_out/'warm.dxil')
        require('PASS pipeline cache' in (out/'consumer.stdout').read_text(encoding='utf-8'),
                'Missing D3D12 pipeline cache evidence')
        for stage in ('vert', 'frag'):
            invoke(f'{stage}-dxv', dxc/'bin/x64/dxv.exe', out/f'{stage}.dxil')
    require(sdk_fingerprint(ROOT) == fingerprint, 'SDK inputs changed during DXIL verification')
    save(out/'receipt.json', dict(schema_version=1, scope=SCOPE, source_commit=revision,
         github_run_id=os.environ.get('GITHUB_RUN_ID'), SDK_source_fingerprint=fingerprint,
         backend='D3D12', software_adapter=True, runner_sha256=sha(pathlib.Path(__file__)),
         dxc_release_sha256=pin['Windows']['sha256'], tool_sha256=tools, files=inventory(out)))
    return check(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('run', 'check', 'compile'))
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--dxc-archive', type=pathlib.Path)
    parser.add_argument('--source', type=pathlib.Path)
    parser.add_argument('--entry')
    parser.add_argument('--stage', choices=PROFILES)
    parser.add_argument('--cache-dir', type=pathlib.Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if args.action == 'run':
        run(out, args.dxc_archive)
    elif args.action == 'compile':
        if not args.source or not args.source.is_file() or not args.stage or not re.fullmatch(
                r'[A-Za-z_][A-Za-z_0-9]*', args.entry or ''):
            parser.error('compile requires --source, --entry and --stage')
        require(platform.system() == 'Windows' and platform.machine().lower() in ('amd64', 'x86_64'),
                'DXIL compilation requires Windows x64')
        out.mkdir(parents=True, exist_ok=False)
        with tempfile.TemporaryDirectory(dir=out.parent, prefix='dxc-dxil-') as directory:
            work = pathlib.Path(directory)
            pin = read(LOCK)['dxc']
            dxc, _ = load_dxc(pin, 'Windows', args.dxc_archive, work, out)
            cache = args.cache_dir.resolve() if args.cache_dir else None
            try:
                result = compile_dxil(dxc, args.source.resolve(), args.entry, args.stage, out/'shader.dxil',
                                      work, command_runner(out), cache, {'dxc': pin['Windows']['sha256']})
            except ValueError as error:
                parser.exit(1, f'{error}\n')
            save(out/'compile-receipt.json', result)
        print(out/'shader.dxil')
        return
    else:
        check(out)
    print('PASS installed D3D12 WARP DXIL and DXBC, two complete 13x7 RGBA images')


if __name__ == '__main__':
    main()
