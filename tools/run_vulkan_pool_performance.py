"""Measure paired Phase 6 Vulkan pool timings and separate native allocation counts."""

import argparse
import hashlib
import itertools
import json
import math
import os
import pathlib
import statistics
import subprocess
import zipfile

import check_vulkan_pool_workload as workload
from linux_comparison import ROOT, read, require, save, sha
from run_vulkan_pool_churn import CALLS, SIZES


SOURCE = read(ROOT/'experiments/vulkan_pool_four_slot/source-receipts.json')
PINS = SOURCE['policies']
POLICIES = ('dedicated', 'adaptive', 'four-slot')
PROFILES = ('mixed', 'sparse', 'held')
CHANNELS = ('create', 'release', 'wait')
ORDERS = tuple(itertools.permutations(POLICIES))
ROUNDS = {'mixed': 16, 'sparse': 4, 'held': 8}
BATCHES = {'mixed': (16, 20, 0), 'sparse': (12, 12, 0), 'held': (16, 16, 8)}
LABELS = {'mixed': ('full', 'survivors', 'released'),
          'sparse': ('full', 'fixed-survivors', 'holes-refilled', 'empty', 'reset-singleton', 'reset-empty'),
          'held': ('ended-retained', 'replacements', 'pending-retained', 'completed', 'empty')}


def expected_reads(profile):
    if profile == 'sparse':
        return 4*(124+32+2*20), 4*(31+8+2*5)*sum(workload.SIZES)
    if profile == 'held':
        return 8*192, 8*8*sum(SIZES)
    live = []
    reads = size = 0
    for i in range(16):
        new = [SIZES[(index+i*7)%len(SIZES)] for index in range(len(live), 96)]
        live += new
        live = [item for j, item in enumerate(live) if (j+i)%(2+i%3) == 0]
        reads += len(new)+len(live)
        size += sum(new)+sum(live)
        if i%4 == 3:
            live.clear()
    return reads, size


def verify_samples(result, observation):
    profile = result['profile']
    samples = result['samples']
    require(len(samples) == (1 if observation else 10), 'Wrong number of sweeps')
    expected = expected_reads(profile)
    digest = samples[0]['digest']
    require(isinstance(digest, str) and digest.isdecimal(), 'Missing workload digest')
    for row in samples:
        require(row['digest'] == digest and (row['reads'], row['bytes']) == expected, 'Readback workload differs')
        for channel, count in zip(CHANNELS, BATCHES[profile], strict=True):
            values = row[channel]
            require(len(values) == (0 if observation else count) and
                    all(type(value) is int and value >= 0 for value in values) and
                    (not values or sum(values) > 0), 'Batch timing shape differs')
    return digest


def live_counters(row):
    return all(type(row[key]) is int and row[key] >= 0 for key in ('buffers', 'memories', 'bytes', 'images'))


def native_summary(result, observer):
    profile = result['profile']
    trace = result['trace']
    prior = dict(memories=0, bytes=0, buffers=0, images=0)
    require(trace['source_fingerprint'] == observer and trace['live'] == prior and not any(
            trace[key] for key in ('errors', 'dropped_events', 'device_cleanup_resources', 'device_cleanup_bytes')),
            'Native observer identity/accounting differs')
    fault = trace['fault']
    require(not any(fault.values()) and not any(trace['outside']['calls'].values()) and
            trace['outside']['errors'] == trace['outside']['allocated_bytes'] == trace['outside']['freed_bytes'] == 0,
            'Unexpected outside work or fault injection')
    phases = trace['frames']
    require(phases and phases[-1]['phase'] == 6 and phases[-1]['iteration'] == 99, 'Missing native teardown')
    target_allocs = target_frees = target_bytes = target_freed = helper_allocs = helper_frees = helper_bytes = helper_freed = 0
    allocation_prefix = [0]
    for frame in phases:
        before, after, counts = (frame[key] for key in ('before', 'after', 'counts'))
        phase = frame['phase']
        calls = counts['successes']
        require(before == prior and live_counters(after) and set(calls) == set(CALLS) and
                counts['calls'] == calls and counts['errors'] == 0 and
                not any(calls[key] for key in ('CreateImage', 'DestroyImage', 'BindImageMemory')),
                'Native interval continuity or unexpected calls')
        require(after['memories'] == before['memories']+calls['AllocateMemory']-calls['FreeMemory'] and
                after['buffers'] == before['buffers']+calls['CreateBuffer']-calls['DestroyBuffer'] and
                after['bytes'] == before['bytes']+counts['allocated_bytes']-counts['freed_bytes'] and
                after['images'] == 0, 'Native interval conservation')
        prior = after
        if phase == 2:
            helper_allocs += calls['AllocateMemory']
            helper_frees += calls['FreeMemory']
            helper_bytes += counts['allocated_bytes']
            helper_freed += counts['freed_bytes']
        elif phase in (1, 3, 4):
            target_allocs += calls['AllocateMemory']
            target_frees += calls['FreeMemory']
            target_bytes += counts['allocated_bytes']
            target_freed += counts['freed_bytes']
            require(phase == 1 or not calls['AllocateMemory'], 'Release/wait performed allocation')
        else:
            require(phase == 6 and not any(calls.values()), 'Device teardown hid cleanup')
        allocation_prefix.append(target_allocs)
    require(prior == trace['live'] and target_allocs == target_frees and target_bytes == target_freed and
            helper_allocs == helper_frees and helper_bytes == helper_freed, 'Native cleanup differs')
    rows = result['checkpoints']
    require([(r['label'], r['round']) for r in rows] ==
            [(label, i) for i in range(ROUNDS[profile]) for label in LABELS[profile]], 'Native checkpoint shape differs')
    checkpoints = []
    for row in rows:
        frame = row['frame_count']
        require(0 < frame < len(allocation_prefix) and row['payload'] <= row['required'] <= row['reserved'],
                'Native checkpoint byte conservation')
        current = phases[frame-1]['after']
        require(current['bytes'] == row['reserved']+row['helper_bytes'] and
                row['allocations'] <= current['memories'] and row['buffers'] <= current['buffers'],
                'Native checkpoint differs from interval')
        checkpoints.append({**row, 'target_allocation_calls': allocation_prefix[frame]})
    require(checkpoints[-1]['reserved'] == 0 and checkpoints[-1]['buffers'] == 0, 'End checkpoint retained allocations')
    return dict(target_allocs=target_allocs, target_frees=target_frees, target_bytes=target_bytes,
                helper_allocs=helper_allocs, helper_bytes=helper_bytes, checkpoints=checkpoints,
                peak_reserved=max(row['reserved'] for row in rows))


def timing_metrics(samples):
    metrics = {}
    for channel in CHANNELS:
        totals = sorted(sum(row[channel]) for row in samples if row[channel])
        for label, percentile in (('p50', .50), ('p95', .95)):
            if totals:
                metrics[f'{channel}_{label}_ns'] = totals[math.ceil(len(totals)*percentile)-1]
    return metrics


def repeatability(calibration):
    return {key: max(abs(a[key]-b[key]) for a, b in calibration) for key in calibration[0][0]}


def paired(calibration, comparisons):
    limits = repeatability(calibration)
    policies = {}
    for policy in POLICIES:
        policies[policy] = {}
        for key, limit in limits.items():
            differences = [block[policy][key]-block['dedicated'][key] for block in comparisons]
            delta = statistics.median(differences)
            policies[policy][key] = dict(median_ns=statistics.median(block[policy][key] for block in comparisons),
                                         paired_delta_ns=delta, repeatability_ns=limit,
                                         slower_beyond_repeatability=delta > limit,
                                         faster_beyond_repeatability=delta < -limit,
                                         paired_deltas_ns=differences)
    return policies


def schedule(adapters):
    keys = tuple(itertools.product(adapters, PROFILES))
    for adapter, profile in keys:
        for policy in POLICIES:
            yield dict(stage='observe', adapter=adapter, profile=profile, policy=policy, block=0)
    for block in range(8):
        for adapter, profile in keys:
            for leg in range(2):
                yield dict(stage='calibrate', adapter=adapter, profile=profile, policy='dedicated',
                           block=block, leg=leg)
    for block, order in enumerate(ORDERS+ORDERS[::-1]):
        for adapter, profile in keys:
            for policy in order:
                yield dict(stage='compare', adapter=adapter, profile=profile, policy=policy, block=block)


def check_row(job, result, identities, digests):
    mode = 'observe' if job['stage'] == 'observe' else 'timing'
    adapter, profile, policy = (job[key] for key in ('adapter', 'profile', 'policy'))
    require((result['adapter'], result['profile'], result['SDK'], result['mode']) ==
            (adapter, profile, PINS[policy]['sdk_source_fingerprint'], mode), 'Consumer identity differs')
    identity = result['device_driver_sha256']
    require(len(identity) == 64 and int(identity, 16) and identities.setdefault(adapter, identity) == identity,
            'Device/driver identity changed')
    digest = verify_samples(result, mode == 'observe')
    require(digests.setdefault((adapter, profile), digest) == digest, 'Logical buffer schedule differs')
    if mode == 'timing':
        require(not result['checkpoints'] and 'trace' not in result, 'Timed run has native instrumentation')


def summarize(campaign):
    require(campaign['base_commit'] == SOURCE['base_commit'], 'Pinned SDK base differs')
    adapters = campaign['adapters']
    require(adapters and len(set(adapters)) == len(adapters), 'Expected unique hardware adapters')
    records = campaign['records']
    jobs = list(schedule(adapters))
    require(len(records) == len(jobs), 'Incomplete paired campaign')
    by = {(adapter, profile): dict(native={}, calibration=[[] for _ in range(8)],
                                  comparisons=[{} for _ in range(12)])
          for adapter in adapters for profile in PROFILES}
    identities, digests = {}, {}
    for job, row in zip(jobs, records, strict=True):
        require(job == row['job'], 'Recorded execution order differs')
        result = row['result']
        check_row(job, result, identities, digests)
        group = by[job['adapter'], job['profile']]
        policy, block = job['policy'], job['block']
        if job['stage'] == 'observe':
            group['native'][policy] = native_summary(result, campaign['observer'])
        else:
            metrics = timing_metrics(result['samples'])
            if job['stage'] == 'calibrate':
                group['calibration'][block].append(metrics)
            else:
                group['comparisons'][block][policy] = metrics
    output = []
    for (adapter, profile), group in by.items():
        native = group['native']
        dedicated, adaptive, capped = (native[policy] for policy in POLICIES)
        for other in (adaptive, capped):
            require(all(other[key] == dedicated[key] for key in ('helper_allocs', 'helper_bytes')) and
                    all(all(a[key] == b[key] for key in ('label', 'round', 'buffers', 'payload', 'required', 'helper_bytes'))
                        for a, b in zip(dedicated['checkpoints'], other['checkpoints'], strict=True)),
                    'Native comparison workload differs')
        if profile == 'sparse':
            labels = {'full': 'full', 'fixed-survivors': 'sparse', 'holes-refilled': 'refilled', 'empty': 'empty',
                      'reset-singleton': 'reset_singleton', 'reset-empty': 'reset_empty'}
            for policy, cap in zip(POLICIES, (1, None, workload.CAPPED_SLOTS), strict=True):
                model = workload.model(cap)
                for row in native[policy]['checkpoints']:
                    expected = model[labels[row['label']]]
                    require(all(row[key] == expected[key] for key in ('reserved', 'required', 'allocations')) and
                            row['target_allocation_calls'] == row['round']*model['reset_empty']['allocation_calls']+
                            expected['allocation_calls'], 'Sparse observation differs from the matched model')
        output.append(dict(adapter=adapter, profile=profile, workload_digest=digests[adapter, profile],
                           native=native, timing=paired(group['calibration'], group['comparisons'])))
    return dict(schema_version=1, base_commit=campaign['base_commit'], sdk_sources=PINS,
                observer=campaign['observer'], binaries=campaign['binaries'],
                observed_processes=sum(job['stage'] == 'observe' for job in jobs),
                calibration_processes=sum(job['stage'] == 'calibrate' for job in jobs),
                comparison_processes=sum(job['stage'] == 'compare' for job in jobs),
                profiles=output)


def run(output, build, layer):
    require(os.name == 'nt', 'The reviewed native observer requires Windows')
    bins = {policy: build/policy/'bench/Release/pool_performance.exe' for policy in POLICIES}
    dll = layer/'Release/VkLayer_gfx_buffer_fault.dll'
    manifest = layer/'Release/VkLayer_gfx_buffer_fault.json'
    observer = (layer/'harness-fingerprint.txt').read_text().strip()
    binaries = {policy: sha(path) for policy, path in bins.items()}
    binaries.update(observer_dll=sha(dll), observer_manifest=sha(manifest))
    clean = {key: value for key, value in os.environ.items()
             if not key.upper().startswith(('VK_', 'VVL_', 'GFX_EXPECT_', 'GFX_TRACE_'))}
    clean['VK_LOADER_LAYERS_DISABLE'] = '~implicit~'
    output.mkdir(parents=True)
    empty = output/'empty-layers'
    empty.mkdir()
    clean['VK_LAYER_PATH'] = str(empty)

    def call(policy, args, env):
        process = subprocess.run([str(bins[policy]), *args], env=env, cwd=ROOT,
                                 capture_output=True, timeout=180)
        require(process.returncode == 0 and not process.stderr,
                f'{policy} {args}: exit {process.returncode} {process.stderr.decode(errors="replace")}')
        return json.loads(process.stdout)

    adapters = call('dedicated', ['--probe'], clean)
    require(adapters and len(set(adapters)) == len(adapters), 'Expected unique hardware adapters')
    jobs = list(schedule(adapters))
    records, identities, digests = [], {}, {}
    observer_dir = output/'observer-layer'
    observer_manifest = read(manifest)
    observer_manifest['layer']['library_path'] = str(dll)
    save(observer_dir/manifest.name, observer_manifest)
    observed = {**clean, 'VK_LAYER_PATH': str(observer_dir), 'VK_INSTANCE_LAYERS': 'VK_LAYER_GFX_buffer_fault',
                'GFX_EXPECT_FAULT_LAYER': str(dll)}
    with (output/'capture.jsonl').open('x', encoding='utf-8', newline='\n') as journal:
        for index, job in enumerate(jobs):
            stage, policy = job['stage'], job['policy']
            mode = 'observe' if stage == 'observe' else 'timing'
            result = call(policy, [job['profile'], job['adapter'], mode], observed if mode == 'observe' else clean)
            native_ids = [result.pop(key) for key in ('device_uuid', 'driver_uuid')]
            require(all(len(value) == 32 and int(value, 16) for value in native_ids), 'Missing native identity')
            result['device_driver_sha256'] = hashlib.sha256(':'.join(native_ids).encode()).hexdigest()
            check_row(job, result, identities, digests)
            if mode == 'observe':
                native_summary(result, observer)
            record = dict(job=job, result=result)
            records.append(record)
            journal.write(json.dumps(record, separators=(',', ':'))+'\n')
            journal.flush()
            next_stage = jobs[index+1]['stage'] if index+1 < len(jobs) else None
            if stage == 'calibrate' and next_stage != stage:
                keys = tuple(itertools.product(adapters, PROFILES))
                jitter = {}
                for adapter, profile in keys:
                    pairs = [timing_metrics(row['result']['samples']) for row in records if
                             row['job']['stage'] == 'calibrate' and row['job']['adapter'] == adapter and
                             row['job']['profile'] == profile]
                    require(len(pairs) == 16, 'Dedicated calibration is incomplete')
                    jitter[adapter+'/'+profile] = repeatability(list(zip(pairs[::2], pairs[1::2], strict=True)))
                save(output/'calibration.json', jitter)
            if (index+1)%6 == 0 or next_stage != stage:
                print(f'{index+1}/{len(jobs)} {stage} {job["adapter"]} {job["profile"]}', flush=True)
    require(all(sha(path) == binaries[policy] for policy, path in bins.items()) and
            sha(dll) == binaries['observer_dll'] and sha(manifest) == binaries['observer_manifest'],
            'Consumer binaries changed during the campaign')
    campaign = dict(base_commit=SOURCE['base_commit'], adapters=adapters, observer=observer,
                    binaries=binaries, records=records)
    summary = summarize(campaign)
    with zipfile.ZipFile(output/'campaign.zip', 'x', compression=zipfile.ZIP_DEFLATED) as package:
        package.writestr('campaign.json', json.dumps(campaign, separators=(',', ':')))
    summary['archive_sha256'] = sha(output/'campaign.zip')
    save(output/'summary.json', summary)
    print(f'PASS {len(jobs)} fresh processes', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    native = sub.add_parser('run')
    native.add_argument('--build', type=pathlib.Path, required=True)
    native.add_argument('--layer', type=pathlib.Path, required=True)
    native.add_argument('--output', type=pathlib.Path, required=True)
    replay = sub.add_parser('check')
    replay.add_argument('--input', type=pathlib.Path, required=True)
    args = parser.parse_args()
    if args.action == 'run':
        output = args.output.resolve()
        require(output.is_relative_to(ROOT/'out') and not output.exists(), 'Use a new isolated out/ directory')
        run(output, args.build.resolve(), args.layer.resolve())
    else:
        archive = args.input/'campaign.zip'
        summary = read(args.input/'summary.json')
        require(summary.pop('archive_sha256') == sha(archive), 'Campaign archive differs')
        with zipfile.ZipFile(archive) as package:
            require(summary == summarize(json.loads(package.read('campaign.json'))), 'Paired report differs')
        print('PASS native counts, calibration and paired timing replay')


if __name__ == '__main__':
    main()
