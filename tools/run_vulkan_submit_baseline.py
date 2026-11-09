"""Measure and replay the Windows Vulkan copy submission baseline."""

import argparse
import itertools
import json
import pathlib
import statistics
import zipfile

from linux_comparison import ROOT, read, require, save, sha
from run_vulkan_frame_slots import digest
from windows_vulkan_baseline import collect


GROUPS = (1, 4, 16)
ORDERS = (GROUPS, GROUPS[::-1], (4, 1, 16), (16, 1, 4))
CHECKS, WARMUP, SAMPLES, COPIES, COPY_BYTES = 3, 4, 20, 16, 4096
CHANNELS = ('begin', 'record', 'submit', 'wait', 'release', 'total')


def jobs(adapters):
    for mode in ('validation', 'observe'):
        for adapter, grouping in itertools.product(adapters, GROUPS):
            yield dict(adapter=adapter, grouping=grouping, mode=mode, block=0)
    for block, order in enumerate(ORDERS):
        for adapter, grouping in itertools.product(adapters, order):
            yield dict(adapter=adapter, grouping=grouping, mode='timing', block=block)


def check_native(row, commands):
    counted = ('pools', 'destroyed_pools', 'command_buffers', 'fences', 'destroyed_fences', 'submissions', 'ready')
    zero = ('idle', 'live_pools', 'live_fences', 'errors')
    require(all(type(row[key]) is int and row[key] == commands for key in counted) and
            all(type(row[key]) is int and row[key] == 0 for key in zero) and row['copies'] == COPIES * CHECKS,
            'Native command, fence or copy accounting differs')
    waits, not_ready = row['waits'], row['not_ready']
    require(type(waits) is int and type(not_ready) is int and 0 <= waits <= min(CHECKS, not_ready) and
            not_ready <= commands, 'Invalid native fence poll or wait count')


def summarize(campaign):
    adapters = campaign['adapters']
    require(adapters and len(adapters) == len(set(adapters)), 'Expected unique hardware adapters')
    build = campaign['build']
    require(digest(build['sdk_source_commit'], 40) and all(digest(build[key]) for key in
            ('sdk', 'binary', 'consumer', 'observer_binary', 'observer_source', 'observer_dispatch', 'vvl')),
            'Missing build identity')
    cells = {(a, g): dict(adapter=a, grouping=g, submissions_per_batch=COPIES // g, timing=[])
             for a, g in itertools.product(adapters, GROUPS)}
    identities = {}
    verified_copies = observed_submissions = 0
    for job, entry in zip(jobs(adapters), campaign['records'], strict=True):
        require(entry['job'] == job, 'Campaign input or order differs')
        row = entry['result']
        adapter, grouping, mode = (job[key] for key in ('adapter', 'grouping', 'mode'))
        require(row['sdk'] == build['sdk'] and all(row[key] == job[key] for key in ('adapter', 'grouping', 'mode')),
                'SDK, adapter or workload differs')
        identity = dict(sha256=row['device_driver_sha256'], driver=row['driver'])
        require(digest(identity['sha256']) and type(identity['driver']) is int and identity['driver'] > 0 and
                identities.setdefault(adapter, identity) == identity, 'Device/driver identity differs')
        timing, observe, validation = mode == 'timing', mode == 'observe', mode == 'validation'
        batches = WARMUP + SAMPLES if timing else CHECKS
        copies = batches * COPIES
        commands = copies // grouping
        require(row['verified_bytes'] == copies * COPY_BYTES and row['submissions'] == commands and
                row['final_sdk_ownership_zero'] is True, 'Reference or submission count differs')
        require(entry['pinned_layer_loaded'] is validation and
                (type(row['validation_errors']) is int and row['validation_errors'] == 0 if validation
                 else row['validation_errors'] is None), 'Validation layer state differs')
        verified_copies += copies
        cell = cells[adapter, grouping]
        require(('native' in row) == observe, 'Unexpected submit observer state')
        if observe:
            check_native(row['native'], commands)
            cell['native'] = row['native']
            observed_submissions += commands
        samples = row['samples']
        require(len(samples) == (SAMPLES if timing else 0), 'Wrong timing sample count')
        if timing:
            require(all(len(sample) == len(CHANNELS) and all(type(n) is int and n >= 0 for n in sample) and
                        0 < sample[0] <= sample[1] and sample[2] > 0 and sum(sample[1:5]) <= sample[5]
                        for sample in samples), 'Invalid CPU intervals')
            cell['timing'].append(dict(zip(CHANNELS, map(statistics.median, zip(*samples, strict=True)), strict=True)))
    for cell in cells.values():
        cell['median_ns'] = {key: statistics.median(row[key] for row in cell['timing']) for key in CHANNELS}
        baseline = cells[cell['adapter'], 1]['timing']
        cell['paired_total_delta_ns'] = [row['total'] - base['total'] for row, base in
                                         zip(cell['timing'], baseline, strict=True)]
    return dict(schema_version=1, build=build, processes=len(campaign['records']), identities=identities,
                verified_copies=verified_copies, verified_bytes=verified_copies * COPY_BYTES,
                observed_submissions=observed_submissions, cells=list(cells.values()))


def run(args):
    source = ROOT/'benchmarks/vulkan_submit_baseline'
    adapters, records, hashes = collect(
        args.binary, args.output, jobs,
        lambda job: [job['adapter'], str(job['grouping']), job['mode']],
        layer='VkLayer_gfx_submit_baseline', layer_env='GFX_EXPECT_SUBMIT_LAYER',
        sources=dict(consumer=source/'main.cpp', observer_source=source/'Layer.cpp',
                     observer_dispatch=ROOT/'benchmarks/common/VulkanObserverDispatch.hpp'))
    build = dict(sdk=args.sdk, sdk_source_commit=args.sdk_source_commit, **hashes)
    campaign = dict(build=build, adapters=adapters, records=records)
    summary = summarize(campaign)
    output = args.output.resolve()
    archive = output/'campaign.zip'
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as package:
        package.writestr('campaign.json', json.dumps(campaign, separators=(',', ':')))
    save(output/'summary.json', dict(summary, archive_sha256=sha(archive)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    execute = commands.add_parser('run')
    for option in ('binary', 'output'):
        execute.add_argument('--'+option, required=True, type=pathlib.Path)
    execute.add_argument('--sdk', required=True)
    execute.add_argument('--sdk-source-commit', required=True)
    replay = commands.add_parser('check')
    replay.add_argument('--input', required=True, type=pathlib.Path)
    args = parser.parse_args()
    if args.action == 'run':
        run(args)
        return
    archive = args.input/'campaign.zip'
    summary = read(args.input/'summary.json')
    require(summary.pop('archive_sha256') == sha(archive), 'Raw campaign archive differs')
    with zipfile.ZipFile(archive) as package:
        require(summarize(json.loads(package.read('campaign.json'))) == summary, 'Replayed submit summary differs')
    print(f"PASS {summary['processes']} processes, {summary['verified_copies']} full copies and "
          f"{summary['observed_submissions']} native submissions")


if __name__ == '__main__':
    main()
