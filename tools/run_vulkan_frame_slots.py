"""Measure and replay the installed Windows Vulkan offscreen frame-slot baseline."""

import argparse
import hashlib
import json
import os
import pathlib
import re
import statistics
import subprocess
import zipfile

from linux_comparison import ROOT, read, require, save, sha


DEPTHS = (1, 2, 4)
WIDTH, HEIGHT = 257, 129
WARMUP, FRAMES = 13, 121
CHANNELS = ('record', 'submit', 'wait', 'read_compare')
ORDERS = (DEPTHS, DEPTHS[::-1], (2, 1, 4), (4, 1, 2))


def jobs(adapters):
    for adapter in adapters:
        for depth in DEPTHS:
            yield dict(adapter=adapter, depth=depth, mode='validation', block=0)
    for block, order in enumerate(ORDERS):
        for adapter in adapters:
            for depth in order:
                yield dict(adapter=adapter, depth=depth, mode='timing', block=block)


def digest(value, length=64):
    return isinstance(value, str) and re.fullmatch(f'[0-9a-f]{{{length}}}', value) is not None and int(value, 16) != 0


def summarize(campaign):
    adapters = campaign['adapters']
    require(adapters and len(set(adapters)) == len(adapters), 'Expected unique hardware adapters')
    require(all(digest(campaign[key]) for key in ('sdk', 'binary_sha256', 'validation_layer_sha256',
                                                 'consumer_sha256')) and digest(campaign['sdk_source_commit'], 40),
            'Missing build identity')
    cells = {(adapter, depth): dict(adapter=adapter, depth=depth, runs=[]) for adapter in adapters for depth in DEPTHS}
    identities = {}
    verified = 0
    for job, entry in zip(jobs(adapters), campaign['records'], strict=True):
        require(entry['job'] == job, 'Unexpected campaign order or inputs')
        row = entry['result']
        adapter, depth = job['adapter'], job['depth']
        timing = job['mode'] == 'timing'
        require(row['SDK'] == campaign['sdk'] and all(row[key] == job[key] for key in ('adapter', 'depth', 'mode')),
                'Build, adapter or slot depth differs')
        identity = {key: row[key] for key in ('device_driver_sha256', 'vendor', 'device', 'driver', 'api')}
        require(digest(identity['device_driver_sha256']) and all(type(identity[key]) is int and identity[key] > 0
                for key in ('vendor', 'device', 'driver', 'api')) and identities.setdefault(adapter, identity) == identity,
                'Invalid or changed device/driver identity')
        count = FRAMES if timing else WARMUP
        verified_frames = FRAMES + WARMUP if timing else WARMUP
        require(row['verified_frames'] == verified_frames and row['verified_bytes'] == verified_frames * WIDTH * HEIGHT * 4,
                'Incomplete pixel coverage')
        verified += verified_frames
        require(row['peak_outstanding'] == depth and row['reuse_waits'] == count - depth and row['drain_waits'] == depth,
                'Slot occupancy or fence schedule differs')
        require(row['final_sdk_ownership_zero'] is True, 'SDK ownership remains')
        cell = cells[adapter, depth]
        footprint = row['sdk_resource_bytes']
        require(type(footprint) is int and footprint >= depth * WIDTH * HEIGHT * 8 and
                cell.setdefault('sdk_resource_bytes', footprint) == footprint, 'SDK slot footprint changed')
        samples = row['samples']
        wall = row['wall_ns']
        if not timing:
            require(entry['pinned_layer_loaded'] is True and type(row['validation_errors']) is int and
                    row['validation_errors'] == 0 and not samples and wall == 0, 'Invalid validation run')
            continue
        require(entry['pinned_layer_loaded'] is False and row['validation_errors'] is None and
                type(wall) is int and wall > 0 and len(samples) == FRAMES, 'Invalid timing run')
        require(all(len(sample) == len(CHANNELS) and all(type(n) is int and n >= 0 for n in sample) and
                    sample[0] > 0 and sample[1] > 0 for sample in samples) and
                sum(map(sum, samples)) <= wall, 'Invalid or overlapping timing intervals')
        medians = dict(zip(CHANNELS, map(statistics.median, zip(*samples, strict=True)), strict=True))
        cell['runs'].append(dict(block=job['block'], wall_ns=wall, median_ns=medians))
    for cell in cells.values():
        runs = cell['runs']
        one_slot = cells[cell['adapter'], 1]['runs']
        cell['median_wall_ns'] = statistics.median(row['wall_ns'] for row in runs)
        cell['median_ns'] = {key: statistics.median(row['median_ns'][key] for row in runs) for key in CHANNELS}
        cell['paired_wall_delta_ns'] = [row['wall_ns'] - base['wall_ns'] for row, base in zip(runs, one_slot, strict=True)]
    return dict(schema_version=1, sdk=campaign['sdk'], sdk_source_commit=campaign['sdk_source_commit'],
                consumer_sha256=campaign['consumer_sha256'], binary_sha256=campaign['binary_sha256'],
                validation_layer_sha256=campaign['validation_layer_sha256'], identities=identities,
                workload=dict(width=WIDTH, height=HEIGHT, warmup=WARMUP, frames=FRAMES),
                processes=len(campaign['records']), verified_frames=verified, verified_bytes=verified * WIDTH * HEIGHT * 4,
                cells=list(cells.values()))


def run(args):
    require(os.name == 'nt', 'Windows Vulkan is required')
    output = args.output.resolve()
    require(output.is_relative_to(ROOT/'out') and not output.exists(), 'Use a fresh out/ path')
    empty = output/'empty-layers'
    empty.mkdir(parents=True)
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(('VK_', 'VVL_', 'GFX_EXPECT_', 'GFX_TRACE_'))}
    env.update(VK_LAYER_PATH=str(empty), VK_LOADER_LAYERS_DISABLE='~implicit~')
    receipt = read(ROOT/'out/vulkan-validation-runtime/receipt.json')
    require(receipt['lock_sha256'] == sha(ROOT/'tools/vulkan-validation.lock.json') and all(
            sha(ROOT/row['path']) == row['sha256'] for row in receipt['artifacts']), 'Pinned VVL runtime differs')
    layer = next(ROOT/row['path'] for row in receipt['artifacts'] if row['path'].endswith('.dll'))
    manifest = next(ROOT/row['path'] for row in receipt['artifacts'] if row['path'].endswith('.json'))
    settings = read(manifest)
    settings['layer']['library_path'] = str(layer)
    validation_dir = output/'validation-layer'
    save(validation_dir/manifest.name, settings)
    validation_env = dict(env, VK_LAYER_PATH=str(validation_dir), VK_LOADER_DEBUG='layer')

    binary = args.binary.resolve()
    consumer = ROOT/'benchmarks/vulkan_frame_slots/main.cpp'
    binary_hash, consumer_hash, layer_hash = sha(binary), sha(consumer), sha(layer)

    def invoke(name, arguments, validation):
        result = subprocess.run([str(binary), *arguments], cwd=ROOT, capture_output=True, timeout=120,
                                env=validation_env if validation else env)
        (output/f'{name}.stderr').write_bytes(result.stderr)
        require(result.returncode == 0, f'{name} failed: {result.stderr[-1800:]}')
        if validation:
            log = result.stderr.decode('utf-8', errors='replace').replace('\\', '/').casefold()
            require(layer.as_posix().casefold() in log and 'insert instance layer' in log,
                    'Pinned Vulkan validation module not loaded')
        else:
            require(not result.stderr, f'{name}: unexpected diagnostics')
        return json.loads(result.stdout)

    adapters = invoke('probe', ['--probe'], False)
    records = []
    for index, job in enumerate(jobs(adapters)):
        validation = job['mode'] == 'validation'
        row = invoke(f'{index:02d}-{job["mode"]}', [job['adapter'], str(job['depth']), job['mode']], validation)
        ids = [row.pop(key) for key in ('device_uuid', 'driver_uuid')]
        require(all(digest(value, 32) for value in ids), 'Missing device or driver UUID')
        row['device_driver_sha256'] = hashlib.sha256(':'.join(ids).encode()).hexdigest()
        records.append(dict(job=job, result=row, pinned_layer_loaded=validation))
        print(job['adapter'], job['depth'], job['mode'], f"block={job['block']} PASS", flush=True)

    require((sha(binary), sha(consumer), sha(layer)) == (binary_hash, consumer_hash, layer_hash),
            'Campaign inputs changed')
    campaign = dict(sdk=args.sdk, sdk_source_commit=args.sdk_source_commit, consumer_sha256=consumer_hash,
                    binary_sha256=binary_hash, validation_layer_sha256=layer_hash, adapters=adapters, records=records)
    summary = summarize(campaign)
    archive = output/'campaign.zip'
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as package:
        package.writestr('campaign.json', json.dumps(campaign, separators=(',', ':')))
    save(output/'summary.json', dict(summary, archive_sha256=sha(archive)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    execute = actions.add_parser('run')
    for key in ('binary', 'output'):
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
        require(summarize(json.loads(package.read('campaign.json'))) == summary, 'Replayed slot summary differs')
    print(f"PASS {summary['processes']} processes, {summary['verified_frames']} full frames and paired timing replay")


if __name__ == '__main__':
    main()
