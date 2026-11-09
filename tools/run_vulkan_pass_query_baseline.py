"""Measure and replay the Windows Vulkan render pass and query pool baseline."""

import argparse
import itertools
import json
import pathlib
import statistics
import zipfile

from linux_comparison import ROOT, read, require, save, sha
from run_vulkan_frame_slots import digest
from windows_vulkan_baseline import collect


PASSES, PROFILES = (1, 16), ('plain', 'timestamp')
CHECKS, WARMUP, SAMPLES, BLOCKS, IMAGE_BYTES = 3, 4, 16, 4, 33 * 9 * 4
CHANNELS = ('begin', 'pass_begin', 'record', 'wait', 'query_read', 'release')


def jobs(adapters):
    for mode in ('validation', 'observe'):
        for adapter, passes, profile in itertools.product(adapters, PASSES, PROFILES):
            yield dict(adapter=adapter, passes=passes, profile=profile, mode=mode, block=0)
    for block in range(BLOCKS):
        order = PROFILES if block % 2 == 0 else PROFILES[::-1]
        for adapter, passes, profile in itertools.product(adapters, PASSES, order):
            yield dict(adapter=adapter, passes=passes, profile=profile, mode='timing', block=block)


def summarize(campaign):
    adapters = campaign['adapters']
    require(adapters and len(adapters) == len(set(adapters)), 'Expected unique hardware adapters')
    build = campaign['build']
    require(digest(build['sdk_source_commit'], 40) and all(digest(build[key]) for key in
            ('sdk', 'binary', 'consumer', 'observer_binary', 'observer_source', 'observer_dispatch', 'vvl')),
            'Missing build identity')
    cells = {(a, n, p): dict(adapter=a, passes=n, profile=p, timing=[])
             for a, n, p in itertools.product(adapters, PASSES, PROFILES)}
    identities = {}
    images = gpu_pairs = query_pools = render_passes = 0
    for job, entry in zip(jobs(adapters), campaign['records'], strict=True):
        require(entry['job'] == job, 'Campaign input or order differs')
        row = entry['result']
        adapter, passes, profile, mode = (job[key] for key in ('adapter', 'passes', 'profile', 'mode'))
        require(row['sdk'] == build['sdk'] and all(row[key] == job[key] for key in ('adapter', 'passes', 'profile', 'mode')),
                'SDK, adapter or workload differs')
        identity = dict(sha256=row['device_driver_sha256'], driver=row['driver'])
        require(digest(identity['sha256']) and type(identity['driver']) is int and identity['driver'] > 0 and
                identities.setdefault(adapter, identity) == identity, 'Device/driver identity differs')
        timing, observe, validation = mode == 'timing', mode == 'observe', mode == 'validation'
        frames = WARMUP + SAMPLES if timing else CHECKS
        timestamps = frames if profile == 'timestamp' else 0
        require(row['verified_images'] == frames * passes and row['verified_bytes'] == frames * passes * IMAGE_BYTES and
                row['discarded_recordings'] == (0 if timing else 1) and row['final_sdk_ownership_zero'] is True,
                'Reference or final ownership differs')
        require(entry['pinned_layer_loaded'] is validation and
                (type(row['validation_errors']) is int and row['validation_errors'] == 0 if validation
                 else row['validation_errors'] is None), 'Validation layer state differs')
        images += frames * passes
        gpu = row['gpu_ns']
        require(len(gpu) == timestamps and all(type(n) is int and 0 <= n <= 30_000_000_000 for n in gpu),
                'Missing or invalid GPU timestamp interval')
        gpu_pairs += len(gpu)
        cell = cells[adapter, passes, profile]
        require(('native' in row) == observe, 'Unexpected pass/query observer state')
        if observe:
            queries, objects = frames + 1, frames * passes + 1
            expected = dict(queries=[queries, queries, 0], passes=[objects, objects, 0],
                            framebuffers=[objects, objects, 0], resets=queries, writes=2*timestamps,
                            reads=timestamps, errors=0)
            require(row['native'] == expected, 'Native pass/query accounting differs')
            cell['native'] = row['native']
            query_pools += queries
            render_passes += objects
        samples = row['samples']
        require(len(samples) == (SAMPLES if timing else 0), 'Wrong timing sample count')
        if timing:
            require(all(len(sample) == len(CHANNELS) and all(type(n) is int and n >= 0 for n in sample) and
                        0 < sample[0] + sample[1] <= sample[2] and (timestamps or sample[4] == 0)
                        for sample in samples), 'Invalid CPU intervals')
            medians = dict(zip(CHANNELS, map(statistics.median, zip(*samples, strict=True)), strict=True))
            if timestamps:
                medians['gpu'] = statistics.median(gpu[WARMUP:])
            cell['timing'].append(medians)
    for cell in cells.values():
        cell['median_ns'] = {key: statistics.median(row[key] for row in cell['timing'])
                             for key in cell['timing'][0]}
    return dict(schema_version=1, build=build, processes=len(campaign['records']), identities=identities,
                images=images, verified_bytes=images * IMAGE_BYTES, gpu_pairs=gpu_pairs,
                observed_query_pools=query_pools, observed_render_passes=render_passes, cells=list(cells.values()))


def run(args):
    source = ROOT/'benchmarks/vulkan_pass_query_baseline'
    adapters, records, hashes = collect(
        args.binary, args.output, jobs,
        lambda job: [job['adapter'], str(job['passes']), job['profile'], job['mode']],
        layer='VkLayer_gfx_pass_query_baseline', layer_env='GFX_EXPECT_PASS_QUERY_LAYER',
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
    actions = parser.add_subparsers(dest='action', required=True)
    execute = actions.add_parser('run')
    for option in ('binary', 'output'):
        execute.add_argument('--'+option, required=True, type=pathlib.Path)
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
        require(summarize(json.loads(package.read('campaign.json'))) == summary, 'Replayed pass/query summary differs')
    print(f"PASS {summary['processes']} processes, {summary['images']} images, "
          f"{summary['observed_query_pools']} query pools and {summary['observed_render_passes']} render passes")


if __name__ == '__main__':
    main()
