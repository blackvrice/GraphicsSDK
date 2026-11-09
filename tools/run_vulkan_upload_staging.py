"""Record and replay the product Vulkan staging baselines for the upload ring study."""

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


COUNTS = (1, 16, 64)
POLICIES = ('transient', 'reuse')
CHANNELS = ('allocate', 'write', 'record', 'submit', 'wait', 'release')
SIZES = (256, 1024, 4096)
PAIRS = 4


def jobs(adapters):
    for adapter, policy in itertools.product(adapters, POLICIES):
        yield dict(adapter=adapter, count=64, policy=policy, mode='validation', block=0)
    for adapter, count, policy in itertools.product(adapters, COUNTS, POLICIES):
        yield dict(adapter=adapter, count=count, policy=policy, mode='observe', block=0)
    for block in range(PAIRS):
        for adapter, count, policy in itertools.product(adapters, COUNTS, POLICIES[::1 if block%2 == 0 else -1]):
            yield dict(adapter=adapter, count=count, policy=policy, mode='timing', block=block)


def byte_count(count):
    return sum(SIZES[slot%len(SIZES)] for slot in range(count))


def native(trace, observer, count, policy):
    zero = dict(memories=0, bytes=0, buffers=0, images=0)
    require(trace['source_fingerprint'] == observer and trace['live'] == zero and not any(
            trace[key] for key in ('errors', 'dropped_events', 'device_cleanup_resources', 'device_cleanup_bytes')),
            'Native staging observer identity or teardown differs')
    require(not any(trace['fault'].values()) and not any(trace['outside']['calls'].values()),
            'Unexpected fault or unscoped resource activity')
    expected_frames = [(2, 99)]
    if policy == 'reuse':
        expected_frames.append((1, 99))
    for index in range(5):
        phases = (2, 2, 2, 2, 2) if policy == 'reuse' else (1, 2, 2, 2, 2, 3, 2)
        expected_frames.extend((phase, index) for phase in phases)
    if policy == 'reuse':
        expected_frames.append((3, 99))
    expected_frames.extend(((2, 99), (6, 99)))
    require([(f['phase'], f['iteration']) for f in trace['frames']] == expected_frames,
            'Native staging phase schedule differs')
    prior = zero
    allocated = freed = calls = frees = peak = 0
    for frame in trace['frames']:
        phase = frame['phase']
        counters = frame['counts']
        before, after = frame['before'], frame['after']
        expected_alloc = (count if phase == 1 else 2*count
                          if (phase, frame['iteration']) == (2, 99) and before == zero else 0)
        expected_free = (count if phase == 3 else 2*count
                         if (phase, frame['iteration']) == (2, 99) and after == zero else 0)
        observed = counters['calls']
        require(before == prior and not counters['errors'] and observed == counters['successes'] and
                observed == dict(CreateBuffer=expected_alloc, DestroyBuffer=expected_free,
                                 CreateImage=0, DestroyImage=0, AllocateMemory=expected_alloc,
                                 FreeMemory=expected_free, BindBufferMemory=expected_alloc, BindImageMemory=0),
                'Native staging allocation or helper call count differs')
        require(after['buffers'] == before['buffers']+expected_alloc-expected_free and
                after['memories'] == before['memories']+expected_alloc-expected_free and
                after['bytes'] == before['bytes']+counters['allocated_bytes']-counters['freed_bytes'] and
                after['images'] == 0, 'Native staging interval accounting differs')
        prior = after
        if phase == 1:
            require(before['buffers'] == 2*count, 'Overlapping staging batches')
            calls += expected_alloc
            allocated += counters['allocated_bytes']
            peak = max(peak, counters['allocated_bytes'])
        if phase == 3:
            require(after['buffers'] == 2*count, 'Upload buffers survived release')
            frees += expected_free
            freed += counters['freed_bytes']
    require(prior == zero and calls == frees == count*(1 if policy == 'reuse' else 5) and
            allocated == freed and allocated >= byte_count(count)*(1 if policy == 'reuse' else 5),
            'Final staging accounting differs')
    return dict(allocations=calls, frees=frees, requested_bytes=allocated, peak_staging_bytes=peak)


def validate(job, row, sdk, observer, identities):
    require(row['SDK'] == sdk and all(row[key] == job[key] for key in ('adapter', 'count', 'policy', 'mode')),
            'Product, device or workload differs')
    digest = row['device_driver_sha256']
    require(len(digest) == 64 and int(digest, 16) and identities.setdefault(job['adapter'], digest) == digest,
            'Device/driver identity changed')
    timing = job['mode'] == 'timing'
    require(row['verified_bytes'] == byte_count(job['count'])*(35 if timing else 5), 'Incomplete reference readback')
    require(len(row['samples']) == (30 if timing else 0), 'Wrong timing sample count')
    for sample in row['samples']:
        require(len(sample) == len(CHANNELS) and all(type(n) is int and n >= 0 for n in sample) and
                sum(sample[1:5]) > 0, 'Invalid timing sample')
        if job['policy'] == 'reuse':
            require(sample[0] == sample[5] == 0, 'Unexpected per-batch allocation or release for reuse')
    if job['mode'] == 'observe':
        return native(row['trace'], observer, job['count'], job['policy'])
    require('trace' not in row, 'Unexpected native observer')
    return None


def summarize(campaign):
    adapters, records = campaign['adapters'], campaign['records']
    require(adapters and len(adapters) == len(set(adapters)), 'Expected unique hardware adapters')
    results = {(adapter, count): dict(adapter=adapter, count=count, bytes_per_batch=byte_count(count),
                                       native={}, timing={policy: [] for policy in POLICIES})
               for adapter, count in itertools.product(adapters, COUNTS)}
    identities = {}
    for job, entry in zip(jobs(adapters), records, strict=True):
        require(entry['job'] == job, 'Campaign ordering differs')
        row = entry['result']
        observation = validate(job, row, campaign['sdk'], campaign['observer'], identities)
        cell = results[job['adapter'], job['count']]
        if observation:
            cell['native'][job['policy']] = observation
        if job['mode'] == 'timing':
            values = list(zip(*row['samples'], strict=True))
            medians = dict(zip(CHANNELS, (statistics.median(v) for v in values), strict=True))
            medians['tracked_total'] = statistics.median(sum(sample) for sample in row['samples'])
            cell['timing'][job['policy']].append(medians)
    for cell in results.values():
        a, b = (cell['timing'][policy] for policy in POLICIES)
        cell['median_ns'] = {policy: {key: statistics.median(row[key] for row in cell['timing'][policy])
                                     for key in a[0]} for policy in POLICIES}
        cell['paired_total_delta_ns'] = [right['tracked_total']-left['tracked_total']
                                         for left, right in zip(a, b, strict=True)]
    return dict(schema_version=1, sdk=campaign['sdk'], source_commit=campaign['source_commit'],
                observer=campaign['observer'], binary_sha256=campaign['binary_sha256'],
                validation_layer_sha256=campaign['validation_layer_sha256'], adapters=adapters,
                processes=len(records), cells=list(results.values()))


def run(args):
    require(os.name == 'nt', 'Windows Vulkan and the pinned observer are required')
    output = args.output.resolve()
    require(output.is_relative_to(ROOT/'out') and not output.exists(), 'Use a fresh out/ path')
    output.mkdir(parents=True)
    empty = output/'empty-layers'
    empty.mkdir()
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(('VK_', 'VVL_', 'GFX_EXPECT_', 'GFX_TRACE_'))}
    env.update(VK_LAYER_PATH=str(empty), VK_LOADER_LAYERS_DISABLE='~implicit~')
    layer = args.layer.resolve()
    observer = (layer/'harness-fingerprint.txt').read_text().strip()
    dll = layer/'Release/VkLayer_gfx_buffer_fault.dll'
    vvl_receipt = read(ROOT/'out/vulkan-validation-runtime/receipt.json')
    require(vvl_receipt['lock_sha256'] == sha(ROOT/'tools/vulkan-validation.lock.json') and all(
            sha(ROOT/row['path']) == row['sha256'] for row in vvl_receipt['artifacts']), 'Pinned VVL runtime differs')
    vvl = next(ROOT/row['path'] for row in vvl_receipt['artifacts'] if row['path'].endswith('.dll'))
    vvl_json = next(ROOT/row['path'] for row in vvl_receipt['artifacts'] if row['path'].endswith('.json'))

    def manifest(source, library, name):
        record = read(source)
        record['layer']['library_path'] = str(library)
        save(output/name/source.name, record)
        return str(output/name)

    environments = {
        'timing': env,
        'observe': {**env, 'VK_LAYER_PATH': manifest(dll.with_suffix('.json'), dll, 'observer-layer'),
                    'VK_INSTANCE_LAYERS': 'VK_LAYER_GFX_buffer_fault', 'GFX_EXPECT_FAULT_LAYER': str(dll)},
        'validation': {**env, 'VK_LAYER_PATH': manifest(vvl_json, vvl, 'validation-layer'), 'VK_LOADER_DEBUG': 'layer'},
    }
    executable = args.binary.resolve()
    binary_hash = sha(executable)

    def invoke(name, arguments, mode):
        proc = subprocess.run([str(executable), *arguments], cwd=ROOT, env=environments[mode],
                              capture_output=True, timeout=120)
        (output/f'{name}.stderr').write_bytes(proc.stderr)
        require(proc.returncode == 0, f'{name} failed: {proc.stderr[-1500:]}')
        if mode == 'validation':
            log = proc.stderr.decode('utf-8', errors='replace').replace('\\','/').casefold()
            require(vvl.as_posix().casefold() in log and 'insert instance layer' in log, 'Expected VVL module was not loaded')
        else:
            require(not proc.stderr, f'{name}: unexpected diagnostics')
        return json.loads(proc.stdout)

    adapters = invoke('probe', ['--probe'], 'timing')
    require(adapters, 'No hardware adapter')
    records, identities = [], {}
    for index, job in enumerate(jobs(adapters)):
        name = f'{index:02d}-{job["mode"]}'
        row = invoke(name, [job['adapter'], str(job['count']), job['policy'], job['mode']], job['mode'])
        raw_ids = [row.pop(key) for key in ('device_uuid', 'driver_uuid')]
        require(all(len(value) == 32 and int(value, 16) for value in raw_ids), 'Missing device/driver identity')
        row['device_driver_sha256'] = hashlib.sha256(':'.join(raw_ids).encode()).hexdigest()
        validate(job, row, args.sdk, observer, identities)
        records.append(dict(job=job, result=row))
        with (output/'capture.jsonl').open('a', encoding='utf-8') as journal:
            journal.write(json.dumps(records[-1], separators=(',', ':'))+'\n')
        print(name, job['adapter'], job['count'], job['policy'], 'PASS', flush=True)
    require(sha(executable) == binary_hash, 'Consumer binary changed')
    campaign = dict(sdk=args.sdk, source_commit=args.source_commit, observer=observer, binary_sha256=binary_hash,
                    validation_layer_sha256=sha(vvl), adapters=adapters, records=records)
    summary = summarize(campaign)
    archive = output/'campaign.zip'
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as package:
        package.writestr('campaign.json', json.dumps(campaign, separators=(',', ':')))
    save(output/'summary.json', dict(**summary, archive_sha256=sha(archive)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    native_run = sub.add_parser('run')
    for key in ('binary', 'layer', 'output'):
        native_run.add_argument('--'+key, type=pathlib.Path, required=True)
    native_run.add_argument('--sdk', required=True)
    native_run.add_argument('--source-commit', required=True)
    replay = sub.add_parser('check')
    replay.add_argument('--input', type=pathlib.Path, required=True)
    args = parser.parse_args()
    if args.action == 'run':
        run(args)
    else:
        archive = args.input/'campaign.zip'
        summary = read(args.input/'summary.json')
        require(summary.pop('archive_sha256') == sha(archive), 'Raw campaign archive differs')
        with zipfile.ZipFile(archive) as package:
            require(summarize(json.loads(package.read('campaign.json'))) == summary, 'Replayed staging summary differs')
        print('PASS native staging allocations, complete readbacks and paired timing replay')


if __name__ == '__main__':
    main()
