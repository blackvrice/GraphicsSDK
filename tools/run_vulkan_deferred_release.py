"""Measure and replay the installed Windows Vulkan deferred release baseline."""

import argparse
import hashlib
import itertools
import json
import os
import pathlib
import statistics
import subprocess
import zipfile

from linux_comparison import ROOT, read, require, save, sha
from run_vulkan_frame_slots import digest


COUNTS, POLICIES = (1, 32), ('early', 'late')
BLOCKS, CHECKS, WARMUP, REPETITIONS, BYTES = 4, 5, 5, 24, 4096
CHANNELS = ('drop_before', 'wait', 'drop_after', 'total')
CREATE, BEFORE, WAIT, AFTER, PERSISTENT, RETIRE = range(1, 7)


def jobs(adapters):
    for mode in ('validation', 'observe'):
        for adapter, count, policy in itertools.product(adapters, COUNTS, POLICIES):
            yield dict(adapter=adapter, count=count, policy=policy, mode=mode, block=0)
    for block in range(BLOCKS):
        order = POLICIES if block % 2 == 0 else POLICIES[::-1]
        for adapter, count, policy in itertools.product(adapters, COUNTS, order):
            yield dict(adapter=adapter, count=count, policy=policy, mode='timing', block=block)


def native(trace, observer, count, early):
    zero = dict(memories=0, bytes=0, buffers=0, images=0)
    require(trace['schema_version'] == 1 and trace['source_fingerprint'] == observer and trace['live'] == zero
            and not any(trace[key] for key in ('errors', 'dropped_events', 'device_cleanup_resources',
                                              'device_cleanup_bytes')), 'Native observer or final ownership differs')
    require(not any(trace['fault'].values()) and not any(trace['outside']['calls'].values()),
            'Unexpected faults or unscoped native work')
    # (phase, iteration, buffer allocations, buffer releases)
    schedule = [(PERSISTENT, 0, count, 0), (CREATE, 0, 2*count, 0), (BEFORE, 0, 0, 0), (AFTER, 0, 0, 2*count)]
    for i in range(1, CHECKS + 1):
        schedule.extend(((CREATE, i, 2*count, 0), (BEFORE, i, 0, 0),
                         (WAIT, i, 0, 2*count if early else 0), (AFTER, i, 0, 0 if early else 2*count)))
    schedule.extend(((PERSISTENT, 0, 0, count), (RETIRE, 0, 0, 0)))
    prior, peak, allocated, freed = zero, 0, 0, 0
    for (phase, iteration, creates, destroys), frame in zip(schedule, trace['frames'], strict=True):
        counters, after = frame['counts'], frame['after']
        calls = dict(CreateBuffer=creates, DestroyBuffer=destroys, CreateImage=0, DestroyImage=0,
                     AllocateMemory=creates, FreeMemory=destroys, BindBufferMemory=creates, BindImageMemory=0)
        require((phase, iteration) == (frame['phase'], frame['iteration']) and frame['before'] == prior
                and counters['calls'] == counters['successes'] == calls and counters['errors'] == 0,
                'Native call count or release phase differs')
        alloc_bytes, free_bytes = counters['allocated_bytes'], counters['freed_bytes']
        require((alloc_bytes >= creates*BYTES if creates else alloc_bytes == 0) and
                (free_bytes >= destroys*BYTES if destroys else free_bytes == 0), 'Native allocation byte count differs')
        require(after['bytes'] == prior['bytes'] + alloc_bytes - free_bytes and after['bytes'] >= after['buffers']*BYTES
                and after['buffers'] == after['memories'] == prior['buffers'] + creates - destroys
                and after['images'] == 0, 'Native live ledger differs')
        prior = after
        peak = max(peak, after['bytes'])
        allocated += alloc_bytes
        freed += free_bytes
    require(prior == zero and allocated == freed, 'Native release incomplete')
    return dict(peak_bytes=peak, allocated_bytes=allocated, submitted_buffers_freed_at_wait=2*count*CHECKS if early else 0,
                submitted_buffers_freed_after_wait=0 if early else 2*count*CHECKS,
                discarded_buffers_freed=2*count)


def summarize(campaign):
    adapters = campaign['adapters']
    require(adapters and len(set(adapters)) == len(adapters), 'Expected unique hardware adapters')
    require(all(digest(campaign[key]) for key in ('sdk', 'observer', 'binary_sha256', 'validation_layer_sha256',
                                                 'observer_sha256', 'consumer_sha256'))
            and digest(campaign['sdk_source_commit'], 40), 'Missing build identity')
    cells = {(a, n): dict(adapter=a, count=n, native={}, timing={p: [] for p in POLICIES})
             for a, n in itertools.product(adapters, COUNTS)}
    identities, verified = {}, 0
    for job, entry in zip(jobs(adapters), campaign['records'], strict=True):
        require(entry['job'] == job, 'Campaign input or order differs')
        row = entry['result']
        adapter, count, policy, mode = (job[key] for key in ('adapter', 'count', 'policy', 'mode'))
        require(row['SDK'] == campaign['sdk'] and all(row[key] == job[key] for key in ('adapter', 'count', 'policy', 'mode')),
                'Build, adapter or workload differs')
        device = row['device_driver_sha256']
        driver = row['driver']
        require(digest(device) and type(driver) is int and driver > 0 and
                identities.setdefault(adapter, dict(identity_sha256=device, driver=driver)) ==
                dict(identity_sha256=device, driver=driver), 'Device/driver identity differs')
        timing = mode == 'timing'
        coverage = (WARMUP + REPETITIONS if timing else CHECKS)*count*BYTES
        require(row['verified_bytes'] == coverage and row['discarded_recordings'] == (0 if timing else 1)
                and row['final_sdk_ownership_zero'] is True, 'Incomplete readback or final ownership')
        verified += coverage
        validation = mode == 'validation'
        require(entry['pinned_layer_loaded'] is validation and
                (type(row['validation_errors']) is int and row['validation_errors'] == 0 if validation
                 else row['validation_errors'] is None), 'Validation state differs')
        observe = mode == 'observe'
        require(('trace' in row) == observe, 'Unexpected native observer state')
        cell = cells[adapter, count]
        if observe:
            cell['native'][policy] = native(row['trace'], campaign['observer'], count, policy == 'early')
        samples = row['samples']
        require(len(samples) == (REPETITIONS if timing else 0), 'Wrong timing sample count')
        if timing:
            for sample in samples:
                require(len(sample) == len(CHANNELS) and all(type(n) is int and n >= 0 for n in sample) and
                        sample[2 if policy == 'early' else 0] == 0 and 0 < sum(sample[:3]) <= sample[3],
                        'Invalid release intervals')
            medians = dict(zip(CHANNELS, map(statistics.median, zip(*samples, strict=True)), strict=True))
            cell['timing'][policy].append(medians)
    for cell in cells.values():
        cell['median_ns'] = {p: {key: statistics.median(row[key] for row in cell['timing'][p])
                                 for key in CHANNELS} for p in POLICIES}
        early, late = (cell['timing'][p] for p in POLICIES)
        cell['paired_early_minus_late_ns'] = [a['total'] - b['total'] for a, b in zip(early, late, strict=True)]
    return dict(schema_version=1, sdk=campaign['sdk'], sdk_source_commit=campaign['sdk_source_commit'],
                observer=campaign['observer'], observer_sha256=campaign['observer_sha256'],
                consumer_sha256=campaign['consumer_sha256'], binary_sha256=campaign['binary_sha256'],
                validation_layer_sha256=campaign['validation_layer_sha256'], identities=identities,
                processes=len(campaign['records']), verified_bytes=verified, cells=list(cells.values()))


def run(args):
    require(os.name == 'nt', 'Windows Vulkan is required')
    output = args.output.resolve()
    require(output.is_relative_to(ROOT/'out') and not output.exists(), 'Use a fresh out/ path')
    empty = output/'empty-layers'
    empty.mkdir(parents=True)
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(('VK_', 'VVL_', 'GFX_EXPECT_', 'GFX_TRACE_'))}
    env.update(VK_LAYER_PATH=str(empty), VK_LOADER_LAYERS_DISABLE='~implicit~')
    observer_dir = args.layer.resolve()
    observer = observer_dir/'Release/VkLayer_gfx_buffer_fault.dll'
    observer_id = (observer_dir/'harness-fingerprint.txt').read_text().strip()
    receipt = read(ROOT/'out/vulkan-validation-runtime/receipt.json')
    require(receipt['lock_sha256'] == sha(ROOT/'tools/vulkan-validation.lock.json') and all(
            sha(ROOT/row['path']) == row['sha256'] for row in receipt['artifacts']), 'Pinned VVL runtime differs')
    vvl = next(ROOT/row['path'] for row in receipt['artifacts'] if row['path'].endswith('.dll'))
    vvl_manifest = next(ROOT/row['path'] for row in receipt['artifacts'] if row['path'].endswith('.json'))

    def manifest(source, library, name):
        settings = read(source)
        settings['layer']['library_path'] = str(library)
        save(output/name/source.name, settings)
        return str(output/name)

    environments = {
        'timing': env,
        'observe': dict(env, VK_LAYER_PATH=manifest(observer.with_suffix('.json'), observer, 'observer-layer'),
                        VK_INSTANCE_LAYERS='VK_LAYER_GFX_buffer_fault', GFX_EXPECT_FAULT_LAYER=str(observer)),
        'validation': dict(env, VK_LAYER_PATH=manifest(vvl_manifest, vvl, 'validation-layer'), VK_LOADER_DEBUG='layer'),
    }
    binary = args.binary.resolve()
    consumer = ROOT/'benchmarks/vulkan_deferred_release/main.cpp'
    inputs = (binary, consumer, observer, vvl)
    hashes = tuple(map(sha, inputs))

    def invoke(name, arguments, mode):
        result = subprocess.run([str(binary), *arguments], cwd=ROOT, capture_output=True, timeout=120,
                                env=environments[mode])
        (output/f'{name}.stderr').write_bytes(result.stderr)
        require(result.returncode == 0, f'{name} failed: {result.stderr[-1800:]}')
        if mode == 'validation':
            log = result.stderr.decode('utf-8', errors='replace').replace('\\', '/').casefold()
            require(vvl.as_posix().casefold() in log and 'insert instance layer' in log, 'Pinned VVL module not loaded')
        else:
            require(not result.stderr, f'{name}: unexpected diagnostics')
        return json.loads(result.stdout)

    adapters = invoke('probe', ['--probe'], 'timing')
    records = []
    for index, job in enumerate(jobs(adapters)):
        mode = job['mode']
        row = invoke(f'{index:02d}-{mode}', [job['adapter'], str(job['count']), job['policy'], mode], mode)
        ids = [row.pop(key) for key in ('device_uuid', 'driver_uuid')]
        require(all(digest(value, 32) for value in ids), 'Missing device or driver UUID')
        row['device_driver_sha256'] = hashlib.sha256(':'.join(ids).encode()).hexdigest()
        records.append(dict(job=job, result=row, pinned_layer_loaded=mode == 'validation'))
        print(job['adapter'], job['count'], job['policy'], mode, f"block={job['block']} PASS", flush=True)
    require(tuple(map(sha, inputs)) == hashes, 'Campaign inputs changed')
    binary_hash, consumer_hash, observer_hash, layer_hash = hashes
    campaign = dict(sdk=args.sdk, sdk_source_commit=args.sdk_source_commit, observer=observer_id,
                    observer_sha256=observer_hash, consumer_sha256=consumer_hash, binary_sha256=binary_hash,
                    validation_layer_sha256=layer_hash, adapters=adapters, records=records)
    summary = summarize(campaign)
    archive = output/'campaign.zip'
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as package:
        package.writestr('campaign.json', json.dumps(campaign, separators=(',', ':')))
    save(output/'summary.json', dict(summary, archive_sha256=sha(archive)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    execute = actions.add_parser('run')
    for key in ('binary', 'layer', 'output'):
        execute.add_argument('--'+key, required=True, type=pathlib.Path)
    execute.add_argument('--sdk', required=True)
    execute.add_argument('--sdk-source-commit', required=True)
    replay = actions.add_parser('check')
    replay.add_argument('--input', required=True, type=pathlib.Path)
    args = parser.parse_args()
    if args.action == 'run':
        run(args)
        return
    archive = args.input/'campaign.zip'
    summary = read(args.input/'summary.json')
    require(summary.pop('archive_sha256') == sha(archive), 'Raw campaign archive differs')
    with zipfile.ZipFile(archive) as package:
        require(summarize(json.loads(package.read('campaign.json'))) == summary, 'Replayed release summary differs')
    print(f"PASS {summary['processes']} processes, {summary['verified_bytes']} reference bytes and native release replay")


if __name__ == '__main__':
    main()
