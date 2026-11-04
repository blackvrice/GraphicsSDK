"""Convert validated SPIR-V to the SDK's Metal graphics profile and verify native execution."""

import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import tempfile
import zipfile

from audit_licenses import sdk_fingerprint
from linux_comparison import GIT, ROOT, read, require, save, sha
from shader_toolchain import LOCK, build_spirv, inventory
from vulkan_spirv import command_runner, compare_d3d12, inspect_module


SCOPE = 'p5-shd-04-spirv-cross-metal-v1'
CONSUMER = ROOT/'tests/metal_spirv'
VULKAN = ROOT/'benchmarks/reports/validation/2026-11-04-vulkan-spirv'
RESOURCE_SHADER = ROOT/'tests/fixtures/spirv_bindings/sample.spvasm'
STAGES = {'vert': ('vertex', 'gfx_vertex'), 'frag': ('fragment', 'gfx_fragment')}
PROFILE_ERROR = 'Metal conversion requires no resources or user varyings and one float4 color output at location 0'
METAL_STANDARD = '-std=macos-metal2.1'


def prior_payload():
    summary = read(VULKAN/'summary.json')
    archive = VULKAN/'evidence.zip'
    require(sha(archive) == summary['archive_sha256'], 'Vulkan source evidence differs')
    files = {item['path']: item['sha256'] for item in summary['files']}
    payload = {}
    with zipfile.ZipFile(archive) as package:
        for name in ('vert.spv', 'frag.spv', 'spirv.rgba'):
            member = 'matrix/lavapipe/'+name
            data = package.read(member)
            require(hashlib.sha256(data).hexdigest() == files[member], f'Vulkan evidence member differs: {name}')
            payload[name] = data
    return summary['source_commit'], payload


def profile(reflection, stage):
    require(reflection['entryPoints'] == [{'name': 'main', 'mode': stage}], 'Unexpected SPIRV-Cross entry point')
    outputs = reflection.get('outputs', [])
    require(set(reflection) <= {'entryPoints', 'inputs', 'outputs'} and not reflection.get('inputs') and
            (not outputs if stage == 'vert' else
             len(outputs) == 1 and outputs[0].get('location') == 0 and outputs[0].get('type') == 'vec4'
             and 'array' not in outputs[0]), PROFILE_ERROR)


def check_msl(path, stage):
    qualifier, entry = STAGES[stage]
    source = path.read_text(encoding='utf-8')
    require(re.search(rf'\b{qualifier}\s+[^\n]+\b{entry}\s*\(', source) is not None and
            '\0' not in source, f'MSL has no expected {entry} entry')
    attributes = {re.sub(r'\s+', '', value) for value in re.findall(r'\[\[([^\]]+)\]\]', source)}
    require(('position' in attributes and attributes <= {'position', 'vertex_id', 'instance_id'}
             if stage == 'vert' else attributes == {'color(0)'}), 'MSL uses attributes outside the graphics profile')


def convert(cross, validator, module, stage, name, out, invoke):
    inspect_module(module, stage, 'main')
    invoke(name+'-validate', validator, '--target-env', 'vulkan1.1', module)
    reflection = json.loads(invoke(name+'-reflect', cross, module, '--reflect'))
    profile(reflection, stage)
    with tempfile.TemporaryDirectory(dir=out, prefix='msl-') as directory:
        pending = pathlib.Path(directory)/f'{name}.metal'
        invoke(name+'-convert', cross, module, '--msl', '--msl-version', '20100',
               '--rename-entry-point', 'main', STAGES[stage][1], stage, '--output', pending)
        check_msl(pending, stage)
        pending.replace(out/pending.name)


def provision(out, work, invoke, jobs):
    shutil.copyfile(LOCK, out/LOCK.name)
    cross, validator = build_spirv(read(LOCK)['sources'], work, out, lambda name, args: invoke(name, *args), jobs)
    invoke('spirv-val-version', validator, '--version')
    return cross, validator


def compare_image(out):
    raw = (out/'metal.bin').read_bytes()
    require(len(raw) == 7*256 and all(raw[row+52:row+256] == b'\xb7'*204 for row in range(0, len(raw), 256)),
            'Metal readback size or row padding differs')
    image = b''.join(raw[row:row+52] for row in range(0, len(raw), 256))
    prior, payload = prior_payload()
    require(image == payload['spirv.rgba'], 'Metal pixels differ from the Vulkan draw')
    require(all((out/name).read_bytes() == payload[name] for name in ('vert.spv', 'frag.spv')),
            'Metal input differs from the Vulkan draw modules')
    return prior, compare_d3d12(image)


def check(out):
    receipt = read(out/'receipt.json')
    require(receipt['schema_version'] == 1 and receipt['scope'] == SCOPE and
            receipt['architecture'] in ('arm64', 'x86_64') and receipt['physical_GPU_accepted'] is False,
            'Metal conversion receipt scope differs')
    require(receipt['files'] == inventory(out), 'Metal conversion evidence inventory or bytes differ')
    commands = read(out/'commands.json')
    required = {'invalid-metal', 'resource-validate', 'resource-reflect', 'consumer', 'consumer-arch',
                'offline-library', *(f'{stage}-{kind}' for stage in STAGES
                                     for kind in ('validate', 'reflect', 'convert', 'metal'))}
    require(required <= commands.keys() and all(row['exit_code'] > 0 if name == 'invalid-metal'
            else row['exit_code'] == 0 for name, row in commands.items()), 'Unexpected Metal command outcome')
    for name, pin in read(out/LOCK.name)['sources'].items():
        require((out/f'{name}-revision.stdout').read_text(encoding='utf-8').strip() == pin['commit'],
                f'{name} revision differs')
    require(set(receipt['tool_sha256']) == {'spirv-cross', 'spirv-val', 'spirv-as'} and
            all(re.fullmatch('[0-9a-f]{64}', value) for value in receipt['tool_sha256'].values()),
            'Missing pinned tool identity')
    for stage in STAGES:
        require(METAL_STANDARD in commands[stage+'-metal']['argv'], 'Apple compiler used a different MSL dialect')
        profile(read(out/f'{stage}-reflect.stdout'), stage)
        check_msl(out/f'{stage}.metal', stage)
    resource = read(out/'resource-reflect.stdout')
    require(resource.get('separate_images') and resource.get('separate_samplers') and
            (out/'resource-profile.txt').read_text(encoding='utf-8').strip() == PROFILE_ERROR and
            not (out/'resource.metal').exists(), 'Resource-bearing module was not rejected by the profile')
    require((out/'consumer-arch.stdout').read_text().strip() == receipt['architecture'] and
            'missingSymbol' in (out/'invalid-metal.stderr').read_text(encoding='utf-8'), 'Offline compiler or architecture differs')
    require(set(receipt['offline_artifact_sha256']) == {'vert.air', 'frag.air', 'draw.metallib'} and
            all(re.fullmatch('[0-9a-f]{64}', value) for value in receipt['offline_artifact_sha256'].values()),
            'Apple offline shader outputs missing')
    stdout = (out/'consumer.stdout').read_text(encoding='utf-8')
    require(all(word in stdout for word in ('missingSymbol', 'PASS Metal (', '), 13x7',
                'SDK_SHA256='+receipt['SDK_source_fingerprint'])), 'Installed Metal runtime receipt differs')
    require((receipt['Vulkan_source_commit'], receipt['D3D12_source_commit']) == compare_image(out),
            'Reference revisions differ')
    return receipt


def run(out, work, cross, validator, invoke, jobs):
    architecture = platform.machine()
    require(platform.system() == 'Darwin' and architecture in ('arm64', 'x86_64'), 'Native macOS required')
    expected_arch = os.environ.get('GFX_METAL_SPIRV_ARCH', architecture)
    require(architecture == expected_arch, 'Unexpected native architecture')
    _, payload = prior_payload()
    for stage in STAGES:
        source = out/f'{stage}.spv'
        source.write_bytes(payload[source.name])
        convert(cross, validator, source, stage, stage, out, invoke)
    tools_build = validator.parent.parent
    invoke('assembler-build', 'cmake', '--build', tools_build, '--target', 'spirv-as', '--parallel', jobs)
    assembler = validator.with_name('spirv-as')
    shutil.copyfile(RESOURCE_SHADER, out/'resource.spvasm')
    resource = out/'resource.spv'
    invoke('assemble-resource', assembler, '--target-env', 'vulkan1.1', out/'resource.spvasm', '-o', resource)
    try:
        convert(cross, validator, resource, 'frag', 'resource', out, invoke)
    except ValueError as error:
        require(str(error) == PROFILE_ERROR, f'Unexpected resource control failure: {error}')
        (out/'resource-profile.txt').write_text(str(error)+'\n', encoding='utf-8')
    else:
        raise ValueError('Resource-bearing shader was converted')

    xcrun = ('xcrun', '--sdk', 'macosx')
    invoke('xcode-version', 'xcodebuild', '-version')
    invoke('metal-version', *xcrun, 'metal', '--version')
    metal = (*xcrun, 'metal', METAL_STANDARD, '-c')
    for stage in STAGES:
        invoke(stage+'-metal', *metal, out/f'{stage}.metal', '-o', work/f'{stage}.air')
    invoke('offline-library', *xcrun, 'metallib', work/'vert.air', work/'frag.air', '-o', work/'draw.metallib')
    invalid = out/'invalid.metal'
    invalid.write_text('#include <metal_stdlib>\nusing namespace metal;\n'
                       'fragment float4 gfx_fragment() { return missingSymbol; }\n', encoding='utf-8')
    invoke('invalid-metal', *metal, invalid, '-o', work/'invalid.air', reject=True)
    require(not (work/'invalid.air').exists(), 'Invalid MSL left an AIR object')

    fingerprint = sdk_fingerprint(ROOT)
    sdk, package, source, build = (work/name for name in ('sdk', 'installed SDK', 'consumer source', 'consumer build'))
    shutil.copytree(CONSUMER, source)
    shutil.copytree(CONSUMER, out/'consumer-source')
    clang = invoke('clang-path', *xcrun, '--find', 'clang++').strip()
    sysroot = invoke('sysroot', *xcrun, '--show-sdk-path').strip()
    flags = ('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_OSX_ARCHITECTURES={architecture}',
             f'-DCMAKE_OSX_SYSROOT={sysroot}', f'-DCMAKE_CXX_COMPILER={clang}')
    invoke('sdk-configure', 'cmake', '-S', ROOT, '-B', sdk, *flags, '-DGFX_ENABLE_METAL=ON',
           '-DGFX_BUILD_TESTS=OFF', '-DGFX_BUILD_EXAMPLES=OFF')
    invoke('sdk-build', 'cmake', '--build', sdk, '--parallel', jobs)
    invoke('sdk-install', 'cmake', '--install', sdk, '--prefix', package)
    invoke('consumer-configure', 'cmake', '-S', source, '-B', build, *flags, f'-DPACKAGE_ROOT={package}')
    invoke('consumer-build', 'cmake', '--build', build, '--parallel', jobs)
    binary = build/'metal_spirv_consumer'
    invoke('consumer-arch', 'lipo', '-archs', binary)
    invoke('consumer', binary, out)
    require(sdk_fingerprint(ROOT) == fingerprint, 'SDK sources changed during Metal execution')
    revision = invoke('source-revision', *GIT, '-C', ROOT, 'rev-parse', 'HEAD').strip()
    vulkan, d3d12 = compare_image(out)
    save(out/'receipt.json', dict(schema_version=1, scope=SCOPE, source_commit=revision,
         github_run_id=os.environ.get('GITHUB_RUN_ID'), architecture=architecture, SDK_source_fingerprint=fingerprint,
         physical_GPU_accepted=False, Vulkan_source_commit=vulkan, D3D12_source_commit=d3d12,
         runner_sha256=sha(pathlib.Path(__file__)),
         tool_sha256={path.name: sha(path) for path in (cross, validator, assembler)},
         offline_artifact_sha256={name: sha(work/name) for name in ('vert.air', 'frag.air', 'draw.metallib')},
         files=inventory(out)))
    check(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    for action in ('convert', 'run', 'check'):
        command = sub.add_parser(action)
        command.add_argument('--output', type=pathlib.Path, required=True)
        if action != 'check':
            command.add_argument('--jobs', type=int, default=3)
        if action == 'convert':
            command.add_argument('--module', type=pathlib.Path, required=True)
            command.add_argument('--stage', choices=STAGES, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if args.action == 'check':
        check(out)
    else:
        require(platform.system() in ('Linux', 'Darwin') and args.jobs > 0, 'Linux or macOS and positive jobs required')
        require(args.action != 'run' or platform.system() == 'Darwin', 'Native macOS required for Metal execution')
        out.mkdir(parents=True, exist_ok=False)
        invoke = command_runner(out)
        with tempfile.TemporaryDirectory(prefix='spirv metal ') as directory:
            work = pathlib.Path(directory).resolve()
            cross, validator = provision(out, work, invoke, args.jobs)
            if args.action == 'convert':
                convert(cross, validator, args.module.resolve(), args.stage, 'shader', out, invoke)
                print(out/'shader.metal')
                return
            run(out, work, cross, validator, invoke, args.jobs)
    print('PASS Metal offline compile, installed Metal draw and Vulkan/D3D12 image parity')


if __name__ == '__main__':
    main()
