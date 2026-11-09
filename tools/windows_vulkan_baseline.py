"""Collect isolated timing, observer and pinned validation runs."""

import hashlib
import json
import os
import subprocess

from linux_comparison import ROOT, read, require, save, sha
from run_vulkan_frame_slots import digest


def collect(binary, output, jobs, arguments, *, layer, layer_env, sources):
    require(os.name == 'nt', 'Windows Vulkan is required')
    output = output.resolve()
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
    binary = binary.resolve()
    observer = binary.with_name(layer + '.dll')

    def manifest(source, library, folder):
        data = read(source)
        data['layer']['library_path'] = str(library)
        save(output/folder/source.name, data)
        return str(output/folder), data['layer']['name']

    observer_dir, observer_name = manifest(observer.with_suffix('.json'), observer, 'observer-layer')
    validation_dir, _ = manifest(vvl_manifest, vvl, 'validation-layer')
    environments = {
        'timing': env,
        'observe': dict(env, VK_LAYER_PATH=observer_dir, VK_INSTANCE_LAYERS=observer_name,
                        **{layer_env: str(observer)}),
        'validation': dict(env, VK_LAYER_PATH=validation_dir, VK_LOADER_DEBUG='layer'),
    }
    inputs = dict(binary=binary, observer_binary=observer, vvl=vvl, **sources)
    hashes = {key: sha(path) for key, path in inputs.items()}

    def invoke(name, args, mode):
        proc = subprocess.run([str(binary), *args], cwd=ROOT, capture_output=True, timeout=120,
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
        mode = job['mode']
        row = invoke(f'{index:02d}-{mode}', arguments(job), mode)
        ids = [row.pop(key) for key in ('device_uuid', 'driver_uuid')]
        require(all(digest(value, 32) for value in ids), 'Missing device or driver UUID')
        row['device_driver_sha256'] = hashlib.sha256(':'.join(ids).encode()).hexdigest()
        records.append(dict(job=job, result=row, pinned_layer_loaded=mode == 'validation'))
        print(*arguments(job), f"block={job['block']} PASS", flush=True)
    require(all(sha(path) == hashes[key] for key, path in inputs.items()), 'Campaign inputs changed')
    return adapters, records, hashes
