"""Record and replay the Windows Vulkan binding snapshot baseline."""

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


COUNTS, PATTERNS = (1, 64, 256), ('same', 'alternating')
CHECKS, WARMUP, SAMPLES, BLOCKS, IMAGE_BYTES = 3, 4, 16, 4, 256 * 7 * 4
CHANNELS = ('bind', 'record', 'drop', 'wait_reclaim')
NATIVE = ('create', 'allocate', 'update', 'bind', 'destroy')
LAYER = 'VkLayer_gfx_descriptor_baseline'


def jobs(adapters):
    for mode in ('validation', 'observe'):
        for adapter, count, pattern in itertools.product(adapters, COUNTS, PATTERNS):
            yield dict(adapter=adapter, count=count, pattern=pattern, mode=mode, block=0)
    for block in range(BLOCKS):
        order = PATTERNS if block % 2 == 0 else PATTERNS[::-1]
        for adapter, count, pattern in itertools.product(adapters, COUNTS, order):
            yield dict(adapter=adapter, count=count, pattern=pattern, mode='timing', block=block)


def summarize(campaign):
    adapters = campaign['adapters']
    require(adapters and len(set(adapters)) == len(adapters), 'Expected unique hardware adapters')
    build = campaign['build']
    require(digest(build['sdk']) and digest(build['sdk_source_commit'], 40) and all(
            digest(build[key]) for key in ('binary', 'consumer', 'observer_binary', 'observer_source', 'vvl')),
            'Missing build identity')
    cells = {(a, n, p): dict(adapter=a, count=n, pattern=p, timing=[]) for a, n, p in
             itertools.product(adapters, COUNTS, PATTERNS)}
    identities = {}
    images = rejections = snapshots = 0
    for job, entry in zip(jobs(adapters), campaign['records'], strict=True):
        require(entry['job'] == job, 'Campaign input or order differs')
        row = entry['result']
        adapter, count, pattern, mode = (job[key] for key in ('adapter', 'count', 'pattern', 'mode'))
        require(row['sdk'] == build['sdk'] and all(row[key] == job[key] for key in ('adapter', 'count', 'pattern', 'mode')),
                'SDK, adapter or workload differs')
        identity = dict(sha256=row['device_driver_sha256'], driver=row['driver'])
        require(digest(identity['sha256']) and type(identity['driver']) is int and identity['driver'] > 0
                and identities.setdefault(adapter, identity) == identity, 'Device/driver identity differs')
        timing, observe, validation = mode == 'timing', mode == 'observe', mode == 'validation'
        frames = WARMUP + SAMPLES if timing else CHECKS
        rejects = CHECKS if not timing and count == COUNTS[-1] else 0
        require(row['verified_images'] == frames and row['verified_bytes'] == frames * IMAGE_BYTES
                and row['capacity_rejections'] == rejects and row['discarded_recordings'] == (0 if timing else 1)
                and row['final_sdk_ownership_zero'] is True, 'Reference, capacity or final ownership differs')
        images += frames
        rejections += rejects
        require(entry['pinned_layer_loaded'] is validation and
                (type(row['validation_errors']) is int and row['validation_errors'] == 0 if validation
                 else row['validation_errors'] is None), 'Validation layer state differs')
        require(('native' in row) == observe, 'Unexpected descriptor observer state')
        cell = cells[adapter, count, pattern]
        if observe:
            calls = CHECKS * count + 1
            require(row['native'] == dict.fromkeys(NATIVE, calls) | dict(live=0, errors=0),
                    'Native descriptor accounting differs')
            cell['native'] = row['native']
            snapshots += calls
        samples = row['samples']
        require(len(samples) == (SAMPLES if timing else 0), 'Wrong sample count')
        if timing:
            require(all(len(sample) == len(CHANNELS) and all(type(v) is int and v >= 0 for v in sample)
                        and 0 < sample[0] <= sample[1] for sample in samples), 'Invalid recording times')
            medians = dict(zip(CHANNELS, map(statistics.median, zip(*samples, strict=True)), strict=True))
            cell['timing'].append(medians)
    for cell in cells.values():
        cell['median_ns'] = {key: statistics.median(row[key] for row in cell['timing']) for key in CHANNELS}
    return dict(schema_version=1, build=build, processes=len(campaign['records']), identities=identities,
                images=images, verified_bytes=images * IMAGE_BYTES, capacity_rejections=rejections,
                observed_snapshots=snapshots, cells=list(cells.values()))


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
    vvl = next(ROOT/row['path'] for row in receipt['artifacts'] if row['path'].endswith('.dll'))
    vvl_manifest = next(ROOT/row['path'] for row in receipt['artifacts'] if row['path'].endswith('.json'))
    binary = args.binary.resolve()
    observer = binary.with_name(LAYER + '.dll')

    def manifest(source, library, folder):
        data = read(source)
        data['layer']['library_path'] = str(library)
        save(output/folder/source.name, data)
        return str(output/folder)

    environments = {
        'timing': env,
        'observe': dict(env, VK_LAYER_PATH=manifest(observer.with_suffix('.json'), observer, 'observer-layer'),
                        VK_INSTANCE_LAYERS='VK_LAYER_GFX_descriptor_baseline', GFX_EXPECT_DESCRIPTOR_LAYER=str(observer)),
        'validation': dict(env, VK_LAYER_PATH=manifest(vvl_manifest, vvl, 'validation-layer'), VK_LOADER_DEBUG='layer'),
    }
    source = ROOT/'benchmarks/vulkan_descriptor_baseline'
    inputs = dict(binary=binary, consumer=source/'main.cpp', observer_binary=observer,
                  observer_source=source/'Layer.cpp', vvl=vvl)
    hashes = {key: sha(path) for key, path in inputs.items()}

    def invoke(name, arguments, mode):
        proc = subprocess.run([str(binary), *arguments], cwd=ROOT, capture_output=True, timeout=120,
                              env=environments[mode])
        (output/f'{name}.stderr').write_bytes(proc.stderr)
        require(proc.returncode == 0, f'{name} failed: {proc.stderr[-1800:]}')
        if mode == 'validation':
            log = proc.stderr.decode('utf-8', errors='replace').replace('\\', '/').casefold()
            require(vvl.as_posix().casefold() in log and 'insert instance layer' in log, 'Pinned VVL module not loaded')
        else:
            require(not proc.stderr, f'{name}: unexpected diagnostics')
        return json.loads(proc.stdout)

    adapters = invoke('probe', ['--probe'], 'timing')
    records = []
    for index, job in enumerate(jobs(adapters)):
        adapter, count, pattern, mode = (job[key] for key in ('adapter', 'count', 'pattern', 'mode'))
        row = invoke(f'{index:02d}-{mode}', [adapter, str(count), pattern, mode], mode)
        ids = [row.pop(key) for key in ('device_uuid', 'driver_uuid')]
        require(all(digest(value, 32) for value in ids), 'Missing device or driver UUID')
        row['device_driver_sha256'] = hashlib.sha256(':'.join(ids).encode()).hexdigest()
        records.append(dict(job=job, result=row, pinned_layer_loaded=mode == 'validation'))
        print(adapter, count, pattern, mode, f"block={job['block']} PASS", flush=True)
    require(all(sha(path) == hashes[key] for key, path in inputs.items()), 'Campaign inputs changed')
    build = dict(sdk=args.sdk, sdk_source_commit=args.sdk_source_commit, **hashes)
    campaign = dict(build=build, adapters=adapters, records=records)
    summary = summarize(campaign)
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
        require(summarize(json.loads(package.read('campaign.json'))) == summary, 'Replayed descriptor summary differs')
    print(f"PASS {summary['processes']} processes, {summary['images']} images and {summary['observed_snapshots']} native snapshots")


if __name__ == '__main__':
    main()
