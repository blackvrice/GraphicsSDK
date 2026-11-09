"""Build an isolated four-slot churn overlay and verify guarded native regressions."""

import argparse
import json
import os
import pathlib
import subprocess

import check_vulkan_pool_workload as workload
import run_vulkan_buffer_pool_fault as fault
import run_vulkan_pool_churn as churn
import run_vulkan_pool_rebase_fault as mixed
from audit_licenses import sdk_fingerprint
from linux_comparison import ROOT, read, require, save, sha


EXPERIMENT = ROOT/'experiments/vulkan_pool_four_slot'
HISTORICAL = ROOT/'experiments/vulkan_pool_churn'
PIN = read(EXPERIMENT/'source-receipts.json')['policies']['four-slot']
SDK = PIN['sdk_source_fingerprint']
PROFILES = ('mixed', 'sparse', 'held', 'capped')
TARGETS = {'churn': 'vulkan_four_slot_churn.exe', 'regression': 'vulkan_buffer_fault.exe',
           'mixed-fault': 'vulkan_rebase_fault.exe'}


def replace(text, before, after):
    require(text.count(before) == 1, f'Historical churn anchor changed: {before[:60]}')
    return text.replace(before, after)


def prepare(output):
    model = (HISTORICAL/'Model.hpp').read_text(encoding='utf-8')
    model = replace(model, 'std::min(largest*2,65536u/s)',
                    f'std::min(largest*2,std::min({workload.CAPPED_SLOTS}u,65536u/s))')
    main = (HISTORICAL/'main.cpp').read_text(encoding='utf-8')
    for include in ('../../experiments/vulkan_buffer_pool_fault/Client.hpp',
                    '../../benchmarks/common/Adapter.hpp', '../../benchmarks/vulkan_upload/Identity.hpp'):
        main = replace(main, f'#include "{include}"', f'#include "{pathlib.PurePosixPath(include).name}"')
    survivors = ', '.join(f'{index}u' for index in workload.SURVIVORS)
    start = main.index('        std::set<std::uint64_t> kept;')
    end = main.index('c.inject(round);', start)
    main = replace(main, main[start:end], f'''        constexpr std::array survivorIds{{{survivors}}};
        std::vector<Item> survivors;
        for (unsigned i = 0; i < live.size(); ++i) {{
            if (std::ranges::find(survivorIds, i % {workload.INITIAL}) != survivorIds.end()) survivors.push_back(live[i]);
            else c.drop(live[i]);
        }}
        live = std::move(survivors);
        c.capture("fixed-survivors",round);''')
    main = replace(main, 'std::string validationLayer(bool enabled)', '#include "CapFault.hpp"\nstd::string validationLayer(bool enabled)')
    main = replace(main, 'profile!="held")', 'profile!="held"&&profile!="capped")')
    main = replace(main, 'else held(c);', 'else if(profile=="held")held(c);else capped(c);')
    main = replace(main, '(profile=="sparse"?3u:4u)', '(profile=="capped"?1u:profile=="sparse"?3u:4u)')
    output.mkdir(parents=True)
    (output/'Model.hpp').write_text(model, encoding='utf-8', newline='\n')
    (output/'main.cpp').write_text(main, encoding='utf-8', newline='\n')
    (output/'ModelTests.cpp').write_bytes((HISTORICAL/'ModelTests.cpp').read_bytes())


def validate_sparse(result):
    model = workload.model(workload.CAPPED_SLOTS)
    labels = {'full': 'full', 'fixed-survivors': 'sparse', 'holes-refilled': 'refilled',
              'empty': 'empty', 'reset-singleton': 'reset_singleton', 'reset-empty': 'reset_empty'}
    for row in result['checkpoints']:
        if row['label'] in labels:
            expected = model[labels[row['label']]]
            require(all(row[key] == expected[key] for key in ('buffers', 'allocations', 'reserved', 'required', 'payload')),
                    'Fixed-survivor native footprint differs from the four-slot model')


def validate_cap(result, validation, source):
    require(result['profile'] == 'capped' and result['SDK_source_fingerprint'] == SDK and
            result['source_fingerprint'] == source and result['candidate'] is True and result['validation'] is validation,
            'Capped fault result identity differs')
    require(result['verified_buffers'] == 34 and result['verified_bytes'] == 34 * 4096, 'Capped fault readback differs')
    rows = result['checkpoints']
    expected_rows = [('initial', 0, 0, 0, 0)]
    expected_events = []
    for index, oom in enumerate((-1, -2)):
        for label in ('cap-full', 'fallback-released', 'rollback'):
            expected_rows.append((label, index, 7, 3, 28672))
        expected_rows.extend((('retry', index, 8, 4, 45056), ('empty', index, 0, 0, 0)))
        for size, error in ((16384, oom), (16384, oom), (4096, -2)):
            expected_events.append(dict(iteration=index, call='AllocateMemory', result=error,
                                        bytes_or_offset=size, poisoned=True))
    expected_rows.append(('final', 99, 0, 0, 0))
    require([(r['label'], r['round'], r['buffers'], r['allocations'], r['reserved']) for r in rows] == expected_rows,
            'Capped fault checkpoint differs')
    trace = result['native_trace']
    require(trace['source_fingerprint'] == source and not any(trace[key] for key in
            ('errors', 'dropped_events', 'device_cleanup_resources', 'device_cleanup_bytes')) and
            not any(trace['live'].values()), 'Capped fault native cleanup differs')
    plan = trace['fault']
    require(plan['injections'] == expected_events and not any(plan[k] for k in ('pending', 'invalid_frees', 'guard_errors')),
            'Capped growth or fallback injected bytes differ')
    frames = trace['frames']
    require([(f['phase'], f['iteration']) for f in frames] ==
            [(p, i) for i in range(2) for p in (1, 2, 2, 3, 4)]+[(6, 99)], 'Capped fault frame matrix differs')
    prior = dict(memories=0, bytes=0, buffers=0, images=0)
    allocated = freed = buffers = allocations = 0
    for frame, failures in zip(frames, [0, 1, 2, 0, 0]*2+[0], strict=True):
        before, after, counts = (frame[k] for k in ('before', 'after', 'counts'))
        calls, ok = counts['calls'], counts['successes']
        require(before == prior and counts['errors'] == 0, 'Capped fault interval continuity')
        require(calls['AllocateMemory']-ok['AllocateMemory'] == failures and
                all(calls[k] == ok[k] for k in churn.CALLS if k != 'AllocateMemory'), 'Unexpected native failure')
        require(after['memories'] == before['memories']+ok['AllocateMemory']-ok['FreeMemory'] and
                after['buffers'] == before['buffers']+ok['CreateBuffer']-ok['DestroyBuffer'] and
                after['bytes'] == before['bytes']+counts['allocated_bytes']-counts['freed_bytes'] and
                after['images'] == 0, 'Capped fault native accounting')
        prior = after
        allocated += counts['allocated_bytes']
        freed += counts['freed_bytes']
        buffers += ok['CreateBuffer']
        allocations += ok['AllocateMemory']
    require(prior == trace['live'] and allocated == freed and not any(trace['outside']['calls'].values()),
            'Capped fault native byte conservation')
    return dict(creates=buffers, allocations=allocations, allocated_bytes=allocated,
                verified_buffers=34, verified_bytes=34 * 4096, injections=len(expected_events))


def validate(kind, profile, result, validation, harness):
    if kind == 'regression':
        fault.validate(result, profile, validation, SDK)
        trace = result['native_trace']
        counts = [f['counts'] for f in trace['frames']]
        return dict(verified_buffers=result['verified_buffers'], verified_bytes=result['verified_bytes'],
                    injections=len(trace['fault']['injections']),
                    creates=sum(row['successes']['CreateBuffer'] for row in counts),
                    allocations=sum(row['successes']['AllocateMemory'] for row in counts),
                    allocated_bytes=sum(row['allocated_bytes'] for row in counts))
    if kind == 'mixed-fault':
        stats = mixed.validate_mixed(result, profile, validation, sdk=SDK)
        return {**stats, 'creates': stats['buffers'], 'allocations': stats['native_frees']}
    if profile == 'capped':
        return validate_cap(result, validation, harness)
    stats = churn.validate(result, profile, validation, True, sdk=SDK, source=harness, fixed_sparse=True)
    if profile == 'sparse':
        validate_sparse(result)
    return stats


def run(output, build, sdk_source, vvl_receipt):
    require(os.name == 'nt', 'The guarded test layer requires Windows')
    require(sha(sdk_source/'candidate-receipt.json') == PIN['source_receipt_sha256'] and
            sdk_fingerprint(sdk_source) == SDK, 'Expected the reviewed four-slot source')
    receipt = read(vvl_receipt)
    require(receipt['lock_sha256'] == sha(ROOT/'tools/vulkan-validation.lock.json'), 'Validation runtime lock differs')
    for record in receipt['artifacts']:
        require(sha(ROOT/record['path']) == record['sha256'], 'Pinned validation runtime differs')
    vvl = next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    harness = (build/'churn/harness-fingerprint.txt').read_text().strip()
    exe_dirs = {kind: build/kind/'Release' for kind in TARGETS}
    artifacts = {exe_dirs[kind]/name: sha(exe_dirs[kind]/name) for kind, exe in TARGETS.items()
                 for name in (exe, 'VkLayer_gfx_buffer_fault.dll', 'VkLayer_gfx_buffer_fault.json')}
    output.mkdir(parents=True)
    settings = output/'settings'
    settings.mkdir()
    (settings/'vk_layer_settings.txt').write_text('# SDK pNext validation controls.\n')
    clean = {key: val for key, val in os.environ.items()
             if not key.upper().startswith(('VK_', 'VVL_', 'SDL_', 'GFX_EXPECT_', 'GFX_TRACE_'))}
    clean['VK_LOADER_LAYERS_DISABLE'] = '~implicit~'
    commands = []

    def execute(name, kind, args, validation=False, expected=0, overrides=None):
        folder = exe_dirs[kind]
        env = {**clean, 'VK_LAYER_PATH': str(folder)+(os.pathsep+str(vvl.parent) if validation else ''),
               'VK_INSTANCE_LAYERS': 'VK_LAYER_GFX_buffer_fault',
               'GFX_EXPECT_FAULT_LAYER': str(folder/'VkLayer_gfx_buffer_fault.dll'),
               'VK_LAYER_SETTINGS_PATH': str(settings)}
        if validation:
            env['GFX_EXPECT_VULKAN_LAYER'] = str(vvl)
        argv = [str(folder/TARGETS[kind]), *args]
        process = subprocess.run(argv, cwd=ROOT, env={**env, **(overrides or {})}, capture_output=True, timeout=120)
        (output/f'{name}.stdout').write_bytes(process.stdout)
        (output/f'{name}.stderr').write_bytes(process.stderr)
        commands.append(dict(name=name, kind=kind, argv=argv, validation=validation,
                             exit_code=process.returncode, expected=expected))
        save(output/'commands.json', commands)
        require(process.returncode == expected, f'{name}: exit {process.returncode}; see {output}')
        if expected:
            require(not process.stdout and b'FAIL:' in process.stderr, f'{name}: control did not fail closed')
            return
        require(not process.stderr, f'{name}: unexpected native/validation diagnostics')
        return json.loads(process.stdout)

    probe = execute('probe', 'churn', ['--probe'])
    adapters = probe['adapters']
    require(probe['SDK_source_fingerprint'] == SDK and adapters and len(adapters) == len(set(adapters)),
            'Expected unique hardware adapter names on the four-slot SDK')
    profiles = {'churn': PROFILES, 'regression': fault.CASES, 'mixed-fault': mixed.MIXED}
    identities = {}
    rows = []
    for index, adapter in enumerate(adapters):
        for validation in (False, True):
            for kind, cases in profiles.items():
                for case in cases:
                    name = f'{kind}-gpu{index}-v{int(validation)}-{case}'
                    args = [case, adapter, str(int(validation))]
                    if kind == 'churn':
                        args.append('1')
                    result = execute(name, kind, args, validation)
                    require(result['device_name'] == adapter and bool(result['loaded_validation_layer']) == validation,
                            f'{name}: wrong adapter or validation mode')
                    if validation:
                        require(pathlib.Path(result['loaded_validation_layer']).samefile(vvl), f'{name}: wrong VVL')
                    ids = (result['device_uuid'], result['driver_uuid'])
                    require(all(len(value) == 32 and int(value, 16) for value in ids) and
                            identities.setdefault(adapter, ids) == ids, f'{name}: GPU/driver identity differs')
                    stats = validate(kind, case, result, validation, harness)
                    rows.append(dict(adapter=adapter, kind=kind, case=case, validation=validation, **stats))
                    print(name, 'PASS', flush=True)

    missing = output/'missing-layer'
    missing.mkdir()
    for kind, case in (('churn', 'capped'), ('regression', 'control'), ('mixed-fault', 'mixed-host')):
        args = [case, adapters[0], '0']
        if kind == 'churn':
            args.append('1')
        execute(f'{kind}-missing-layer', kind, args, expected=1, overrides={'VK_LAYER_PATH': str(missing)})
        execute(f'{kind}-wrong-adapter', kind, [case, 'missing exact adapter', *args[2:]], expected=1)
    require(all(sha(path) == digest for path, digest in artifacts.items()) and
            sdk_fingerprint(sdk_source) == SDK, 'Native binaries or SDK source changed during the campaign')
    count = sum(len(cases) for cases in profiles.values())
    for index in range(0, len(rows), 2*count):
        pair = rows[index:index+count], rows[index+count:index+2*count]
        require(all({**off, 'validation': True} == on for off, on in zip(*pair, strict=True)),
                'Validation on/off changed resource work or native counts')
    sparse = read(output/'churn-gpu0-v0-sparse.stdout')['checkpoints']
    summary = dict(schema_version=1, sdk_source_fingerprint=SDK, harness_fingerprint=harness,
                   fault_harness_fingerprints={'regression': fault.fingerprint(), 'mixed-fault': mixed.fingerprint()},
                   validation_lock_sha256=receipt['lock_sha256'], validation_layer_sha256=sha(vvl), adapters=adapters,
                   clean_processes=len(rows), negative_controls=6,
                   sparse_checkpoints=[row for row in sparse if row['round'] == 0],
                   totals={key: sum(row[key] for row in rows) for key in
                           ('verified_buffers', 'verified_bytes', 'injections', 'creates', 'allocations',
                            'allocated_bytes')},
                   stdout_sha256={row['name']: sha(output/f"{row['name']}.stdout") for row in commands},
                   runs=rows)
    save(output/'summary.json', summary)
    print('PASS', len(rows), 'guarded native processes and six negative controls')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    prepare_cmd = commands.add_parser('prepare')
    runtime = commands.add_parser('run')
    for command in (prepare_cmd, runtime):
        command.add_argument('--output', type=pathlib.Path, required=True)
    runtime.add_argument('--build', type=pathlib.Path, required=True)
    runtime.add_argument('--sdk-source', type=pathlib.Path, required=True)
    runtime.add_argument('--vvl-receipt', type=pathlib.Path,
                         default=ROOT/'out/vulkan-validation-runtime/receipt.json')
    args = parser.parse_args()
    output = args.output.resolve()
    require(output.is_relative_to(ROOT/'out') and not output.exists(), 'Use a new isolated out/ directory')
    if args.action == 'prepare':
        prepare(output)
    else:
        run(output, args.build.resolve(), args.sdk_source.resolve(), args.vvl_receipt.resolve())


if __name__ == '__main__':
    main()
