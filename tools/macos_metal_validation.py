"""Run Metal API Validation against installed SDK workloads and replay the receipts."""

import argparse
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import time

import macos_metal_backend as backend
from audit_licenses import sdk_fingerprint
from linux_comparison import GIT, ROOT, read, record, require, save, sha, unchanged


PROFILE = {
    'MTL_DEBUG_LAYER': '1',
    'MTL_DEBUG_LAYER_ERROR_MODE': 'assert',
    'MTL_DEBUG_LAYER_WARNING_MODE': 'ignore',
}
CONTROL = 'tests/macos_metal_validation/Control.mm'
FIXTURE = 'tests/fixtures/metal/graphics.metal'
SDK_CASES = ('submission', 'buffer', 'texture', 'graphics', 'layer', 'window')
COMMANDS = ('build-control', 'valid-off', 'valid-on', 'invalid-on', *SDK_CASES)
MARKER = 'CONTROL: invalid blit source length=16 copy size=20 before commit'
# These receipts describe a Darwin child, including when replayed on Windows.
DARWIN_ABORT = -6
ENABLED = 'Metal API Validation Enabled'


def negative_control(status, stdout, stderr):
    require(status == DARWIN_ABORT and stdout.strip() == MARKER,
            'Invalid Metal control must reach the blit and terminate with SIGABRT')
    diagnostic = re.search(r'copyFromBuffer[^\n]*failed assertion([^\n]*(?:\n[^\n]*){0,2})', stderr, re.IGNORECASE)
    require(diagnostic and re.search(r'\bsource\b|sourceOffset', diagnostic[1], re.IGNORECASE)
            and re.search(r'\blength\b|bounds|\bsize\b', diagnostic[1], re.IGNORECASE),
            'Metal did not diagnose the controlled invalid blit range')


def window_result(value, fingerprint, status):
    if status == 77:
        require(value == dict(presentation_executed=False, reason='no AppKit screen'), 'Unrecognized window skip')
        return False
    backend.core.exact(value, dict(contract='metal_window_v1', SDK_source_fingerprint=fingerprint, frames=8,
                                  presentation_executed=True, interactive=False, native_control_presented=0))
    callbacks = [value[k] for k in ('before_resize_presented', 'after_resize_presented')]
    require(all(type(n) is int and 0 <= n <= 4 for n in callbacks), 'Window callback counts invalid')
    displayed = min(callbacks) > 0
    backend.core.exact(value, dict(display_observed=displayed, native_control_frames=0 if displayed else 3))
    return True


def inventory(folder):
    return [dict(path=p.relative_to(folder).as_posix(), bytes=p.stat().st_size, sha256=sha(p))
            for p in sorted(folder.rglob('*'), key=lambda p: p.as_posix())
            if p.is_file() and p != folder/'receipt.json']


def check(folder):
    receipt = read(folder/'receipt.json')
    backend.core.exact(receipt, dict(schema_version=1, scope='macos-metal-api-validation-v1', profile=PROFILE,
                                    shader_validation_executed=False, physical_GPU_accepted=False))
    require(receipt['architecture'] in ('arm64', 'x86_64') and re.fullmatch(r'[0-9a-f]{40}', receipt['source_commit']),
            'Invalid validation source or architecture')
    require(receipt['files'] == inventory(folder), 'Validation evidence inventory changed')
    fp = receipt['SDK_source_fingerprint']
    require(re.fullmatch(r'[0-9a-f]{64}', fp), 'Invalid SDK source fingerprint')
    rows = read(folder/'commands.json')
    require([r['name'] for r in rows] == list(COMMANDS), 'Missing or reordered validation processes')
    for row in rows:
        name = row['name']
        expected = {} if name in ('build-control', 'valid-off') else PROFILE
        status = {DARWIN_ABORT} if name == 'invalid-on' else {0, 77} if name == 'window' else {0}
        require(row['validation_environment'] == expected and type(row['returncode']) is int and row['returncode'] in status,
                f'{name} did not run with the expected environment and exit status')
    def raw(name, stream):
        return (folder/'commands'/f'{name}.{stream}').read_text(encoding='utf-8')

    for row in rows[1:]:
        name = row['name']
        stderr = raw(name, 'stderr')
        if row['returncode'] != 77:
            require((ENABLED in stderr) == (name != 'valid-off'), f'Metal did not report the expected validation state in {name}')
        if name != 'invalid-on':
            require(not re.search(r'failed assertion|Metal (?:API )?Validation Error', stderr, re.IGNORECASE),
                    f'Unexpected native validation error in {name}')
    for name in ('valid-off', 'valid-on'):
        require(json.loads(raw(name, 'stdout')) == dict(contract='native_metal_valid_blit_v1', verified_bytes=16),
                'Native valid blit did not verify every byte')
    negative_control(rows[3]['returncode'], raw('invalid-on', 'stdout'), raw('invalid-on', 'stderr'))
    result = {name: json.loads(raw(name, 'stdout')) for name in SDK_CASES}
    backend.result(result['submission'], fp, receipt['registry_ids'])
    backend.buffer_result(result['buffer'], fp)
    backend.textures.result(result['texture'], fp)
    backend.graphics.result(result['graphics'], fp)
    for name, verify in (('buffer', backend.payloads), ('texture', backend.textures.payloads), ('graphics', backend.graphics.payloads)):
        require(verify(folder/(name+'-payloads')) == result[name]['verified_bytes'], f'{name} validated readback differs')
    backend.core.exact(result['layer'], dict(contract='metal_layer_contracts_v1', SDK_source_fingerprint=fp,
                                           presentation_executed=False))
    window = window_result(result['window'], fp, rows[-1]['returncode'])
    return dict(architecture=receipt['architecture'], SDK_source_fingerprint=fp, native_API_validation_observed=True,
                native_invalid_blit_rejected=True, SDK_workloads_executed=5+int(window), window_executed=window,
                compositor_display_observed=result['window'].get('display_observed', False),
                verified_payload_bytes=sum(result[n]['verified_bytes'] for n in ('buffer', 'texture', 'graphics')),
                shader_validation_executed=False, physical_GPU_accepted=False)


def run(evidence, presentation, folder):
    require(platform.system() == 'Darwin', 'Native macOS required for Metal API Validation')
    import resource

    receipt = read(evidence/'receipt.json')
    arch = platform.machine()
    fp = sdk_fingerprint(ROOT)
    commit = subprocess.check_output([*GIT, 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    backend.core.exact(receipt, dict(scope='macos-metal-graphics-v4', architecture=arch, source_commit=commit,
                                    SDK_source_fingerprint=fp))
    out = folder.resolve()
    require(out.is_relative_to(ROOT/'out') and not out.exists(), 'Use a fresh validation output under out/')
    control = out.with_name(out.name+'-control')
    require(not control.exists(), 'Native control binary already exists')
    (out/'commands').mkdir(parents=True)
    shutil.copyfile(ROOT/CONTROL, out/'Control.mm')
    shutil.copyfile(ROOT/FIXTURE, out/'graphics.metal')
    base = evidence/'availability/core'
    compiler = (base/'commands/compiler-path.stdout').read_text().strip()
    sdk = read(base/'receipt.json')['sdk_path']
    env = {k:v for k,v in os.environ.items() if not k.startswith(('MTL_', 'METAL_', 'DYLD_', 'LD_', 'GFX_'))}
    # The expected abort stays in this subprocess tree and must not create a core dump.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    rows = []

    def call(name, argv, profile, accepted):
        argv = list(map(str, argv))
        with (out/'commands'/f'{name}.stdout').open('wb') as stdout, (out/'commands'/f'{name}.stderr').open('wb') as stderr:
            started = time.monotonic()
            with subprocess.Popen(argv, cwd=ROOT, env={**env, **profile}, stdin=subprocess.DEVNULL,
                                  stdout=stdout, stderr=stderr) as child:
                try:
                    child.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
                    raise
        rows.append(dict(name=name, argv=argv, validation_environment=profile, pid=child.pid,
                         started=started, finished=time.monotonic(), returncode=child.returncode))
        save(out/'commands.json', rows)
        require(child.returncode in accepted, f'{name} exit {child.returncode}: '+
                (out/'commands'/f'{name}.stderr').read_text(errors='replace')[-6000:])

    call('build-control', [compiler, '-arch', arch, '-isysroot', sdk, '-std=c++23', '-fobjc-arc',
                           ROOT/CONTROL, '-framework', 'Metal', '-framework', 'Foundation', '-o', control], {}, {0})
    consumer = evidence.with_name(evidence.name+'-consumer')
    commands = [
        ('submission', [consumer/'macos_metal_consumer', fp]),
        ('buffer', [consumer/'macos_metal_buffer_consumer', fp, out/'buffer-payloads']),
        ('texture', [consumer/'macos_metal_texture_consumer', fp, out/'texture-payloads']),
        ('graphics', [consumer/'macos_metal_graphics_consumer', ROOT/FIXTURE, fp, out/'graphics-payloads']),
        ('layer', [presentation, '--contracts']),
        ('window', [presentation, '--runner-window']),
    ]
    frozen = [record(p) for p in {control, ROOT/CONTROL, ROOT/FIXTURE, *(args[0] for _, args in commands)}]
    call('valid-off', [control, '--valid'], {}, {0})
    call('valid-on', [control, '--valid'], PROFILE, {0})
    call('invalid-on', [control, '--invalid'], PROFILE, {DARWIN_ABORT})
    for name, args in commands:
        call(name, args, PROFILE, {0, 77} if name == 'window' else {0})
    require(unchanged(frozen) and sdk_fingerprint(ROOT) == fp, 'Validation source or executable changed')
    ids = backend.probe.inventory(read(evidence/'availability/receipt.json')['inventory'])
    save(out/'receipt.json', dict(schema_version=1, scope='macos-metal-api-validation-v1', profile=PROFILE,
                                  source_commit=commit, SDK_source_fingerprint=fp, architecture=arch,
                                  github_run_id=os.environ.get('GITHUB_RUN_ID'), registry_ids=ids,
                                  frozen_inputs=sorted(frozen, key=lambda r:r['path']), files=inventory(out),
                                  shader_validation_executed=False, physical_GPU_accepted=False))
    return check(out)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest='mode', required=True)
    native = modes.add_parser('run')
    native.add_argument('--backend', type=pathlib.Path, required=True)
    native.add_argument('--presentation', type=pathlib.Path, required=True)
    native.add_argument('--output', type=pathlib.Path, required=True)
    replay = modes.add_parser('check')
    replay.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    value = (run(args.backend.resolve(), args.presentation.resolve(), args.output)
             if args.mode == 'run' else check(args.output.resolve()))
    print(json.dumps(value, indent=2))
