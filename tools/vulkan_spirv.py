"""Compile and validate DXC SPIR-V, or verify the installed Linux Vulkan consumer."""

import argparse
import hashlib
import os
import pathlib
import platform
import re
import shutil
import struct
import tempfile
import zipfile

from audit_licenses import sdk_fingerprint
from d3d12_dxil import reference
from linux_comparison import GIT, ROOT, read, require, save, sha
from shader_cache import PROFILES, check_campaign, command_runner, compile_dxc, exercise_cache
from shader_toolchain import FIXTURE, FLAGS, LOCK, build_spirv, inventory, load_dxc


SCOPE = 'p5-shd-03-vulkan-software-v1'
CONSUMER = ROOT/'tests/vulkan_spirv'
D3D12 = ROOT/'benchmarks/reports/validation/2026-11-04-d3d12-dxil'
HEADERS_COMMIT = 'b379292b2ab6df5771ba9870d53cf8b2c9295daf'


def provision(out, work, archive, invoke, jobs):
    lock = read(LOCK)
    shutil.copyfile(LOCK, out/LOCK.name)
    root, hashes = load_dxc(lock['dxc'], 'Linux', archive, work, out)
    _, validator = build_spirv(lock['sources'], work, out, lambda name, args: invoke(name, *args),
                               jobs, build_cross=False)
    hashes['spirv-val'] = sha(validator)
    invoke('dxc-version', root/'bin/dxc', '--version')
    invoke('spirv-val-version', validator, '--version')
    return root/'bin/dxc', validator, hashes


def compile_shader(dxc, validator, source, entry, stage, output, work, invoke, *,
                   name='main', cache=None, identity=None):
    arguments = (*FLAGS, '-spirv', '-fspv-target-env=vulkan1.1',
                 f'-fspv-entrypoint-name={name}', '-E', entry, '-T', PROFILES[stage])

    def validate(path, step):
        step(output.stem+'-validate', validator, '--target-env', 'vulkan1.1', path)
        inspect_module(path, stage, name)

    return compile_dxc(dxc, arguments, source, output, work, invoke, validate,
                       target=f'Vulkan 1.1 / SPIR-V 1.3 {PROFILES[stage]}', cache=cache, identity=identity)


def compiler_identity():
    lock = read(LOCK)
    return dict(dxc=lock['dxc']['Linux']['sha256'], validator=lock['sources']['SPIRV-Tools']['commit'],
                spirv_headers=lock['sources']['SPIRV-Headers']['commit'])


def inspect_module(path, stage, entry):
    data = path.read_bytes()
    require(20 <= len(data) <= 4 * 1024 * 1024 and len(data) % 4 == 0, 'Invalid SPIR-V module size')
    words = struct.unpack(f'<{len(data)//4}I', data)
    require(words[:2] == (0x07230203, 0x00010300), 'Expected little-endian SPIR-V 1.3')
    at = 5
    entries = []
    while at < len(words):
        size, opcode = words[at] >> 16, words[at] & 0xffff
        require(0 < size <= len(words)-at, 'Invalid SPIR-V instruction framing')
        if opcode == 15:
            require(size >= 4, 'Truncated SPIR-V entry point')
            name, terminator, _ = data[(at+3)*4:(at+size)*4].partition(b'\0')
            require(bool(terminator), 'Unterminated SPIR-V entry point')
            entries.append((words[at+1], name))
        at += size
    require(entries == [(0 if stage == 'vert' else 4, entry.encode())], 'Unexpected SPIR-V entry point')


def mismatched_store(source):
    data = bytearray(source.read_bytes())
    at = 20
    while at < len(data):
        word, = struct.unpack_from('<I', data, at)
        size, opcode = word >> 16, word & 0xffff
        require(0 < size <= (len(data)-at)//4, 'Invalid SPIR-V control source framing')
        if opcode == 62:
            require(size >= 3, 'Invalid OpStore framing')
            # Both IDs are defined, but the destination pointer cannot be stored as a float4 value.
            destination, = struct.unpack_from('<I', data, at+4)
            struct.pack_into('<I', data, at+8, destination)
            return data
        at += size*4
    raise ValueError('Expected an OpStore in the fixture')


def compare_d3d12(image):
    summary = read(D3D12/'summary.json')
    archive = D3D12/'evidence.zip'
    require(sha(archive) == summary['archive_sha256'], 'D3D12 reference archive differs')
    files = {item['path']: item for item in summary['files']}
    with zipfile.ZipFile(archive) as package:
        for suffix in ('dxil', 'dxbc'):
            name = f'matrix/warp/{suffix}.rgba'
            saved = package.read(name)
            require(hashlib.sha256(saved).hexdigest() == files[name]['sha256'] and
                    image == saved == reference(), f'Vulkan differs from archived D3D12 {suffix} reference')
    return summary['source_commit']


def check(out):
    receipt = read(out/'receipt.json')
    require(receipt['schema_version'] == 1 and receipt['scope'] == SCOPE and receipt['system'] == 'Linux' and
            receipt['backend'] == 'Vulkan' and receipt['software_adapter'] is True and
            receipt['runtime_validation_layer'] is False,
            'Vulkan DXC receipt scope differs')
    require(receipt['files'] == inventory(out), 'Vulkan DXC evidence inventory or bytes differ')
    lock = read(out/LOCK.name)
    require(receipt['dxc_release_sha256'] == lock['dxc']['Linux']['sha256'] and
            lock['dxc']['Linux']['reported_build'] in (out/'dxc-version.stdout').read_text(encoding='utf-8'),
            'DXC release or version differs')
    for name in ('SPIRV-Tools', 'SPIRV-Headers'):
        require((out/f'{name}-revision.stdout').read_text(encoding='utf-8').strip() == lock['sources'][name]['commit'],
                f'{name} revision differs')
    require(lock['sources']['SPIRV-Tools']['commit'] in (out/'spirv-val-version.stdout').read_text(encoding='utf-8') and
            set(receipt['tool_sha256']) == {*lock['dxc']['Linux']['tools'], 'spirv-val'} and
            all(re.fullmatch('[0-9a-f]{64}', value) for value in receipt['tool_sha256'].values()),
            'External validator executable or source identity differs')
    for name, stage, entry in (('vert', 'vert', 'main'), ('frag', 'frag', 'main'), ('named', 'frag', 'pixelMain')):
        inspect_module(out/f'{name}.spv', stage, entry)
    require(mismatched_store(out/'frag.spv') == (out/'wrong-store-type.spv').read_bytes(),
            'SPIR-V negative control differs')
    commands = read(out/'commands.json')
    controls = ('wrong-store-type-validate', 'invalid-compile')
    positives = {'consumer', *(f'{stage}-{kind}' for stage in ('vert', 'frag', 'named')
                             for kind in ('compile', 'validate'))}
    require((positives | set(controls)).issubset(commands) and
            all(row['exit_code'] > 0 if name in controls else row['exit_code'] == 0
                for name, row in commands.items()), 'Unexpected compiler, validator, or consumer exit status')
    require('type does not match Object' in (out/'wrong-store-type-validate.stderr').read_text(encoding='utf-8') and
            'missingSymbol' in (out/'invalid-compile.stderr').read_text(encoding='utf-8') and
            not (out/'invalid.spv').exists(), 'Shader negative controls differ')
    stdout = (out/'consumer.stdout').read_text(encoding='utf-8')
    require(all(text in stdout for text in ('entry point must be main', 'execution model/stage mismatch',
                'SPIR-V stage/byte framing', 'PASS Vulkan software (llvmpipe', '), 13x7',
                'SDK_SHA256='+receipt['SDK_source_fingerprint'])), 'Installed Vulkan consumer receipt differs')
    stderr = (out/'consumer.stderr').read_text(encoding='utf-8')
    library = pathlib.PurePosixPath(read(out/'icd.json')['ICD']['library_path']).name
    require(re.search(r'calling init:[ \t]+/[^\n]*/'+re.escape(library)+r'[ \t]*$', stderr, re.MULTILINE) and
            'libVkLayer_khronos_validation' not in stderr, 'Unexpected Vulkan driver or validation layer')
    require(receipt['D3D12_source_commit'] == compare_d3d12((out/'spirv.rgba').read_bytes()),
            'D3D12 reference revision differs')
    if (out/'cache-evidence/results.json').is_file():
        check_campaign(out/'cache-evidence', 'spv', 'Vulkan 1.1 / SPIR-V 1.3 ps_6_0')
        require((out/'cached.rgba').read_bytes() == (out/'spirv.rgba').read_bytes(),
                'Cached Vulkan draw differs from the original')
    return receipt


def run(args, out, work, dxc, validator, hashes, invoke):
    headers = args.headers.resolve()
    require(invoke('vulkan-headers-revision', *GIT, '-C', headers, 'rev-parse', 'HEAD').strip() == HEADERS_COMMIT
            and not invoke('vulkan-headers-status', *GIT, '-C', headers, 'status', '--porcelain').strip(),
            'Vulkan-Headers checkout differs from reviewed revision')
    source = out/FIXTURE.name
    shutil.copyfile(FIXTURE, source)
    for stage, entry in (('vert', 'vertexMain'), ('frag', 'pixelMain')):
        compile_shader(dxc, validator, source, entry, stage, out/f'{stage}.spv', work, invoke)
    compile_shader(dxc, validator, source, 'pixelMain', 'frag', out/'named.spv', work, invoke, name='pixelMain')
    negative = out/'wrong-store-type.spv'
    negative.write_bytes(mismatched_store(out/'frag.spv'))
    invoke('wrong-store-type-validate', validator, '--target-env', 'vulkan1.1', negative, reject=True)
    bad = out/'invalid.hlsl'
    bad.write_text('float4 broken() : SV_Target0 { return missingSymbol; }\n', encoding='utf-8')
    invoke('invalid-compile', dxc, *FLAGS, '-spirv', '-fspv-target-env=vulkan1.1', '-E', 'broken',
           '-T', 'ps_6_0', bad, '-Fo', work/'invalid.spv', reject=True)
    require(not (work/'invalid.spv').exists(), 'Rejected HLSL left a shader object')

    fingerprint = sdk_fingerprint(ROOT)
    sdk, package, consumer, build = (work/name for name in ('sdk', 'package', 'source', 'build'))
    shutil.copytree(CONSUMER, consumer)
    shutil.copyfile(ROOT/'tests/PipelineCacheSupport.hpp', consumer/'PipelineCacheSupport.hpp')
    shutil.copytree(consumer, out/'consumer-source')
    compiler = ('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_CXX_COMPILER={args.compiler}')
    invoke('sdk-configure', 'cmake', '-S', ROOT, '-B', sdk, *compiler, '-DGFX_ENABLE_D3D12=OFF',
           '-DGFX_ENABLE_VULKAN=ON', '-DGFX_BUILD_TESTS=OFF', '-DGFX_BUILD_EXAMPLES=OFF',
           f'-DGFX_VULKAN_HEADERS={headers}')
    invoke('sdk-build', 'cmake', '--build', sdk, '--parallel', args.jobs)
    invoke('sdk-install', 'cmake', '--install', sdk, '--prefix', package)
    invoke('consumer-configure', 'cmake', '-S', consumer, '-B', build, *compiler, f'-DPACKAGE_ROOT={package}')
    invoke('consumer-build', 'cmake', '--build', build, '--parallel', args.jobs)

    icd = args.icd.resolve()
    require('vulkan_lvp' in read(icd)['ICD']['library_path'], 'Expected lavapipe')
    shutil.copyfile(icd, out/'icd.json')
    empty = work/'empty-layers'
    empty.mkdir()
    env = {key: value for key, value in os.environ.items() if not key.startswith(('VK_', 'LD_'))}
    env.update(VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd), VK_LAYER_PATH=str(empty),
               VK_LOADER_LAYERS_DISABLE='~implicit~', LD_DEBUG='libs')
    invoke('consumer', build/'spirv_consumer', out, env=env)
    cache_out = out/'cache-evidence'
    cache_invoke = command_runner(cache_out)
    results = exercise_cache(cache_out, lambda source, label, cache: compile_shader(
        dxc, validator, source, 'pixelMain', 'frag', cache_out/f'{label}.spv', work, cache_invoke,
        cache=cache, identity=compiler_identity()))
    save(cache_out/'results.json', results)
    require('PASS pipeline cache' in invoke('cached-consumer', build/'spirv_consumer', out,
                                            cache_out/'warm.spv', env=env), 'Missing Vulkan pipeline cache evidence')
    require(sdk_fingerprint(ROOT) == fingerprint, 'SDK sources changed during Vulkan execution')
    revision = invoke('source-revision', *GIT, '-C', ROOT, 'rev-parse', 'HEAD').strip()
    save(out/'receipt.json', dict(schema_version=1, scope=SCOPE, source_commit=revision,
         github_run_id=os.environ.get('GITHUB_RUN_ID'), SDK_source_fingerprint=fingerprint, system='Linux',
         backend='Vulkan', software_adapter=True, runtime_validation_layer=False,
         runner_sha256=sha(pathlib.Path(__file__)),
         dxc_release_sha256=read(LOCK)['dxc']['Linux']['sha256'], tool_sha256=hashes,
         D3D12_source_commit=compare_d3d12((out/'spirv.rgba').read_bytes()), files=inventory(out)))
    check(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    verify = sub.add_parser('check')
    compile_cmd = sub.add_parser('compile')
    runtime = sub.add_parser('run')
    for cmd in (verify, compile_cmd, runtime):
        cmd.add_argument('--output', type=pathlib.Path, required=True)
    for cmd in (compile_cmd, runtime):
        cmd.add_argument('--dxc-archive', type=pathlib.Path)
        cmd.add_argument('--jobs', type=int, default=3)
    compile_cmd.add_argument('--source', type=pathlib.Path, required=True)
    compile_cmd.add_argument('--entry', required=True)
    compile_cmd.add_argument('--stage', choices=PROFILES, required=True)
    compile_cmd.add_argument('--cache-dir', type=pathlib.Path)
    runtime.add_argument('--icd', type=pathlib.Path, required=True)
    runtime.add_argument('--headers', type=pathlib.Path, default=ROOT/'.deps/Vulkan-Headers')
    runtime.add_argument('--compiler', default='g++-14')
    args = parser.parse_args()
    out = args.output.resolve()
    if args.action == 'check':
        check(out)
    else:
        require(platform.system() == 'Linux' and platform.machine() == 'x86_64' and args.jobs > 0,
                'DXC SPIR-V requires Linux x64 and a positive build job count')
        if args.action == 'compile':
            require(args.source.is_file() and re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', args.entry),
                    'Expected an HLSL input file and a valid entry name')
        out.mkdir(parents=True, exist_ok=False)
        invoke = command_runner(out)
        with tempfile.TemporaryDirectory(dir=out.parent, prefix='dxc-spv-') as tmp:
            work = pathlib.Path(tmp)
            dxc, validator, hashes = provision(out, work, args.dxc_archive, invoke, args.jobs)
            if args.action == 'compile':
                cache = args.cache_dir.resolve() if args.cache_dir else None
                try:
                    result = compile_shader(dxc, validator, args.source.resolve(), args.entry, args.stage,
                                            out/'shader.spv', work, invoke, cache=cache, identity=compiler_identity())
                except ValueError as error:
                    parser.exit(1, f'{error}\n')
                save(out/'compile-receipt.json', result)
                print(out/'shader.spv')
                return
            run(args, out, work, dxc, validator, hashes, invoke)
    print('PASS installed Vulkan DXC SPIR-V, validation controls and complete D3D12 reference match')


if __name__ == '__main__':
    main()
