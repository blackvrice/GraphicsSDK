"""Run every SDK/SDL Linux workload case as software correctness diagnostics."""
import argparse
import hashlib
import json
import math
import os
import pathlib
import platform
import re
import shutil
import subprocess
import time
import zipfile

import linux_workloads as w
import linux_comparison as q
from linux_comparison import ROOT, require, read, save, sha, record

CONTRACT = 'benchmarks/phase3_comparison/contract.json'
WARMUP, SAMPLES = 1, 3


def strict_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, 'Duplicate JSON key')
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=pairs)


def exact(value, expected):
    require(all(k in value and type(value[k]) is type(v) and value[k] == v
                for k, v in expected.items()), 'Workload identity, work or scope differs')


def schedule(spec):
    jobs = [dict(profile=p, scenario=s, sdk=sdk, validation=validation)
            for p in w.port.PROFILES for s in spec['profiles'][p]['shapes']
            for sdk in q.SDKS for validation in (False, True)]
    return [dict(j, name=f"{i:02}-{j['profile']}-{j['scenario']}-{j['sdk']}-{'validation' if j['validation'] else 'plain'}")
            for i, j in enumerate(jobs)]


def arguments(job, r):
    return [r['readiness']['binaries'][job['profile']]['path'], job['sdk'], job['scenario'],
            'high', str(WARMUP), str(SAMPLES), str(int(job['validation'])),
            r['readiness']['qualification']['selected']['name']]


def environment(job, r):
    a = r['readiness']['qualification']
    result = q.overrides(pathlib.Path(r['output_directory']), a['icd']['path'],
                         a['validation_library'], job['validation'])
    result.update(GFX_COMPARISON_UUID=a['device_uuid'], GFX_COMPARISON_MODE='software-diagnostic')
    return result


def validate_result(value, job, r):
    p, scenario, sdk = job['profile'], job['scenario'], job['sdk']
    ready = r['readiness']; a = ready['qualification']; d = a['selected']
    spec = r['contract']['profiles'][p]
    shape = spec['shapes'][scenario]
    n = WARMUP + SAMPLES
    exact(value, dict(schema_version=1, sdk=sdk, api='vulkan', category=spec['category'],
        contract=spec['contract'], probe=False, scenario=scenario, preference='high', configuration='Release',
        validation=job['validation'], source_fingerprint=ready['profile_fingerprints'][p],
        SDK_source_fingerprint=a['SDK_source_fingerprint'], SDL_commit=a['SDL_commit'],
        device_name=d['name'], device_uuid=d['device_uuid'], driver_uuid=d['driver_uuid'], device_luid=None,
        native_vendor_id=d['vendor_id'], native_device_id=d['device_id'],
        native_driver_version=d['driver_version_raw'], native_api_version=d['api_version_raw'],
        identity_attribution=w.ATTRIBUTION, warmup=WARMUP, sample_count=SAMPLES,
        readback_verified=True, gpu_ms=None, loaded_validation_layer=a['validation_library'] if job['validation'] else '',
        width=shape[0], height=shape[1], texel_bytes=shape[2]))
    env = value['environment']
    exact(env, a['environment'])
    exact(env, dict(gpu=d['name'], software_renderer=True, device_uuid=d['device_uuid'],
                    driver_uuid=d['driver_uuid'], device_luid=None))
    rows = value['samples_ms']
    require(type(rows) is list and len(rows) == SAMPLES, 'Diagnostic row count differs')
    def number(x):
        return type(x) in (int, float) and math.isfinite(x) and x >= 0
    if p in ('pso', 'allocation'):
        require(value['columns'] == spec['columns'] and all(type(row) is list and len(row) == 2
            and all(number(x) for x in row) and row[0] > 0 for row in rows), 'Invalid public API intervals')
        exact(value, dict(GPU_use_in_timing=False, GPU_memory_reclamation_verified=False))
    else:
        require(all(number(x) and x > 0 for x in rows), 'Invalid CPU fence intervals')
    images, buffers = n, 0
    if p == 'draw':
        calls, instances = spec['work'][scenario]
        exact(value, dict(draw_calls_per_frame=calls, instances_per_call=instances,
            vertices_per_instance=3, vertices_per_frame=3*calls*instances,
            first_vertex=0, first_instance=0, scissor=spec['scissor'], submitted_draw_calls=n*calls))
        require(all(type(x) is int for x in value['scissor']), 'Scissor must use integers')
        intervals = value['CPU_intervals_ms']
        require(value['columns'] == spec['columns'] and type(intervals) is list and len(intervals) == SAMPLES,
                'Draw CPU partitions missing')
        require(all(type(row) is list and len(row) == 4 and all(number(x) for x in row)
            and row[-1] == total and abs(sum(row[:3])-total) <= max(1e-6, total*1e-10)
            for row, total in zip(intervals, rows)), 'Draw CPU intervals do not partition')
    if p in ('draw', 'pso'):
        exact(value, dict(shader_header_sha256=ready['shader_header_sha256']))
    if p != 'upload':
        exact(value, dict(requested_instance_api_version=4198400))
    if p == 'pso':
        count = spec['work'][scenario]
        exact(value, dict(pipelines_per_iteration=count, created_pipelines=n*count,
            released_pipeline_handles=n*count, used_pipeline_regions=n*count,
            includes_shader_object_create_release=True,
            cache_state='uncontrolled_driver_cache_repeated_public_creation'))
    if p == 'allocation':
        count = spec['work'][scenario]; texture = scenario.startswith('texture')
        images, buffers = (n*count, 0) if texture else (0, n*count)
        exact(value, dict(resource_kind='texture' if texture else 'buffer', resources_per_iteration=count,
            buffer_bytes=0 if texture else spec['buffer_bytes'],
            verified_buffers=buffers, verified_resources=n*count, created_resources=n*count,
            released_resource_handles=n*count, used_resources=n*count,
            allocation_policy='public_device_resource_default_policy',
            usage_policy='sampled_rgba8_2d' if texture else 'sdl_vertex_buffer' if sdk == 'sdl' else 'sdk_general_device_buffer',
            native_allocation_count_verified=False))
    size = shape[0]*shape[1]*shape[2] if images else r['contract']['profiles']['allocation']['buffer_bytes']
    exact(value, dict(verified_images=images, verified_bytes=(images+buffers)*size))
    for key, val in value.items():
        if key.startswith(('verified_', 'created_', 'released_', 'used_', 'submitted_')):
            require(type(val) is int, 'Work/readback counters must be integers')
    return value


def validate_scope(r):
    a = r['readiness']['qualification']
    exact(a, dict(mode='software-diagnostic', physical_probe_qualified=False, P3_BEN_07_completed=False))
    require(type(a['selected']['device_type']) is int and a['selected']['device_type'] == 4,
            'Software matrix requires the qualified native CPU device')
    exact(r, dict(schema_version=1, task='P3-LBEN-readiness', mode='software-diagnostic',
        physical_GPU_accepted=False, performance_accepted=False, performance_samples=0,
        P3_BEN_07_completed=False, P3_BEN_08_completed=False, P3_LBEN_completed=False,
        LLGL_accepted=False, overall_SDK_ranking=False, diagnostic_timings_excluded=True))


def safe(folder, name):
    p = pathlib.PurePosixPath(name)
    require(not p.is_absolute() and '..' not in p.parts and '\\' not in name and ':' not in name,
            'Unsafe saved output path')
    resolved = (folder/name).resolve()
    require(resolved.is_relative_to(folder.resolve()) and resolved.is_file(), 'Saved output escapes evidence')
    return resolved


def loaded_libraries(stderr, a, live=False):
    paths = set(re.findall(r'calling init:\s+(/[^\n]+?)\s*$', stderr.decode(errors='replace'), re.M))
    # Ubuntu's loader trace may use /lib while the frozen file inventory resolves
    # the usr-merge symlink to /usr/lib. Live collection also checks real files.
    def normalized(path):
        return '/usr'+path if path.startswith('/lib/') else path
    require({normalized(i['path']) for i in a['runtime_libraries']}.issubset({normalized(p) for p in paths}),
            'Qualified actual loader/ICD not observed')
    loaded_validation = {p for p in paths if 'VkLayer_khronos_validation' in p}
    require(not loaded_validation or loaded_validation == {a['validation_library']}, 'Unexpected validation library')
    if live:
        require(q.driver_libraries(pathlib.Path(a['icd']['path']), stderr.decode(errors='replace'), a['loader_path'])
                == a['runtime_libraries'], 'Actual loader/ICD files changed')
    return bool(loaded_validation)


def check(folder, live=False):
    r = strict_json((folder/'receipt.json').read_bytes())
    exact(r, dict(status='completed', inputs_unchanged=True, all_SDK_SDL_software_cases_verified=True))
    validate_scope(r)
    require(sha(folder/'readiness/receipt.json') == r['readiness_sha256']
            and read(folder/'readiness/receipt.json') == r['readiness'], 'Readiness receipt changed')
    w.check(folder/'readiness')
    require(r['source_commit'] == r['readiness']['source_commit'], 'Different readiness revision')
    require((folder/'settings/vk_layer_settings.txt').read_bytes() ==
            (folder/'readiness/settings/vk_layer_settings.txt').read_bytes(), 'Sync settings changed')
    require(sha(folder/'source.zip') == r['source_zip_sha256'], 'Executed source archive changed')
    with zipfile.ZipFile(folder/'source.zip') as archive:
        require(len(archive.namelist()) == len(set(archive.namelist())) == len(r['source_inputs']),
                'Duplicate or missing source archive entry')
        require(set(archive.namelist()) == {x['path'] for x in r['source_inputs']}, 'Source inventory changed')
        for item in r['source_inputs']:
            require(hashlib.sha256(archive.read(item['path'])).hexdigest() == item['sha256'], 'Archived source changed')
        require(strict_json(archive.read(CONTRACT)) == r['contract'], 'Executed contract differs')
        archived = {i['path']: i['sha256'] for i in r['source_inputs']}
        for item in r['readiness']['source_inputs'] + r['readiness']['qualification']['source_inputs']:
            name = pathlib.PurePosixPath(item['path']).relative_to(r['source_root']).as_posix()
            require(archived.get(name) == item['sha256'], 'Source archive does not cover qualified input')
    expected = schedule(r['contract'])
    require(len(expected) == 52 and len(r['runs']) == len(expected), 'Full 13-case SDK/SDL matrix incomplete')
    last = 0; totals = dict(verified_images=0, verified_buffers=0, verified_bytes=0, diagnostic_rows=0)
    for row, job in zip(r['runs'], expected):
        require(row['job'] == job and row['argv'] == arguments(job, r) and row['overrides'] == environment(job, r),
                'Native scenario, order, command or environment changed')
        require(type(row['job']['validation']) is bool and type(row['pid']) is int and row['pid'] > 0
                and all(type(row[k]) in (int, float) and math.isfinite(row[k]) for k in ('started', 'finished'))
                and last <= row['started'] <= row['finished'],
                'Native processes overlap or PID is missing')
        last = row['finished']
        stdout, stderr = (safe(folder, row[k]).read_bytes() for k in ('stdout', 'stderr'))
        require(hashlib.sha256(stdout).hexdigest() == row['stdout_sha256']
                and hashlib.sha256(stderr).hexdigest() == row['stderr_sha256'], 'Raw native output changed')
        require(type(row['returncode']) is int and row['returncode'] == 0 and not re.search(
            rb'Validation Error|SYNC-HAZARD|VUID-.*(?:ERROR|Error)', stdout+b'\n'+stderr), 'Native workload or validation failed')
        value = validate_result(strict_json(stdout), job, r)
        require(value == row['result'], 'Parsed result differs from raw native output')
        # LD_DEBUG proves the same ICD/loader and validation-library isolation.
        a = r['readiness']['qualification']
        require(loaded_libraries(stderr, a) == job['validation'], 'Actual validation library mode differs')
        for key in ('verified_images', 'verified_buffers', 'verified_bytes'):
            totals[key] += value.get(key, 0)
        totals['diagnostic_rows'] += len(value['samples_ms'])
    require(r['totals'] == totals, 'Saved readback/diagnostic totals differ')
    if live:
        w.check(pathlib.Path(r['readiness_evidence']), live=True)
        require(q.unchanged(r['frozen_inputs']) and q.git(ROOT, 'rev-parse', 'HEAD') == r['source_commit'],
                'Current source/build/runtime differs')
    return dict(processes=52, scenarios=13, sdks=list(q.SDKS), modes=['plain', 'required-validation'],
                **totals, performance_samples=0, physical_GPU_accepted=False, P3_LBEN_completed=False)


def run(a):
    require(platform.system() == 'Linux', 'Actual Linux host required')
    ready = a.readiness.resolve(); w.check(ready, live=True); prior = read(ready/'receipt.json')
    require(q.git(ROOT, 'rev-parse', 'HEAD') == prior['source_commit']
            and not q.git(ROOT, 'status', '--porcelain', '--untracked-files=no'), 'Clean same-revision sources required')
    out = a.output.resolve()
    require(out.is_relative_to(ROOT/'out') and not out.exists() and not out.is_relative_to(ready),
            'Fresh independent output under out/ required')
    r = dict(schema_version=1, task='P3-LBEN-readiness', mode='software-diagnostic', status='running',
        source_commit=prior['source_commit'], source_root=ROOT.as_posix(), readiness_evidence=ready.as_posix(),
        readiness=prior, readiness_sha256=sha(ready/'receipt.json'), output_directory=out.as_posix(),
        contract=read(ROOT/CONTRACT), physical_GPU_accepted=False, performance_accepted=False, performance_samples=0,
        P3_BEN_07_completed=False, P3_BEN_08_completed=False, P3_LBEN_completed=False, LLGL_accepted=False,
        overall_SDK_ranking=False, diagnostic_timings_excluded=True, all_SDK_SDL_software_cases_verified=False,
        totals=dict(verified_images=0, verified_buffers=0, verified_bytes=0, diagnostic_rows=0), runs=[])
    validate_scope(r)
    out.mkdir(parents=True); shutil.copytree(ready, out/'readiness')
    for name in ('commands', 'empty', 'settings', 'xdg'):
        (out/name).mkdir(mode=0o700)
    shutil.copyfile(ready/'settings/vk_layer_settings.txt', out/'settings/vk_layer_settings.txt')
    sources = {pathlib.Path(x['path']) for x in prior['source_inputs']+prior['qualification']['source_inputs']}
    sources.update(ROOT/p for p in (CONTRACT, 'tools/linux_workload_matrix.py'))
    r['source_inputs'] = [dict(path=p.relative_to(ROOT).as_posix(), sha256=sha(p)) for p in sorted(sources)]
    with zipfile.ZipFile(out/'source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for p in sorted(sources):
            archive.write(p, p.relative_to(ROOT).as_posix())
    r['source_zip_sha256'] = sha(out/'source.zip')
    r['frozen_inputs'] = prior['frozen_inputs'] + prior['qualification']['frozen_inputs'] + [record(p) for p in sorted(sources)]
    r['frozen_inputs'] += [record(out/'settings/vk_layer_settings.txt'), record(out/'source.zip')]
    base = {k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_', 'VVL_', 'SDL_', 'GFX_', 'LD_'))
            and k not in ('DISPLAY', 'WAYLAND_DISPLAY')}
    save(out/'receipt.json', r)
    try:
        for job in schedule(r['contract']):
            require(q.unchanged(r['frozen_inputs']), 'Frozen inputs changed before execution')
            argv, env = arguments(job, r), environment(job, r)
            start = time.monotonic()
            child = subprocess.Popen(argv, cwd=ROOT, env={**base, **env}, stdin=subprocess.DEVNULL,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            timeout = False
            try:
                stdout, stderr = child.communicate(timeout=600)
            except subprocess.TimeoutExpired:
                timeout = True; child.kill(); stdout, stderr = child.communicate()
            row = dict(job=job, argv=argv, overrides=env, pid=child.pid, started=start,
                       finished=time.monotonic(), returncode=child.returncode)
            for key, data in (('stdout', stdout), ('stderr', stderr)):
                path = out/'commands'/(job['name']+'.'+key); path.write_bytes(data)
                row[key] = path.relative_to(out).as_posix(); row[key+'_sha256'] = sha(path)
            r['runs'].append(row); save(out/'receipt.json', r)
            require(not timeout and child.returncode == 0, job['name']+' failed; raw logs retained')
            value = validate_result(strict_json(stdout), job, r); row['result'] = value
            require(not re.search(rb'Validation Error|SYNC-HAZARD|VUID-.*(?:ERROR|Error)', stderr), 'Native validation error')
            require(loaded_libraries(stderr, prior['qualification'], live=True) == job['validation'],
                    'Actual validation module differs')
            for key in ('verified_images', 'verified_buffers', 'verified_bytes'):
                r['totals'][key] += value.get(key, 0)
            r['totals']['diagnostic_rows'] += len(value['samples_ms'])
            save(out/'receipt.json', r); print(job['name']+' PASS', flush=True)
        require(q.unchanged(r['frozen_inputs']), 'Frozen inputs changed after execution')
        r.update(status='completed', inputs_unchanged=True, all_SDK_SDL_software_cases_verified=True)
        save(out/'receipt.json', r); print(json.dumps(check(out, live=True)), flush=True)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        r.update(status='failed', error=str(exc)); save(out/'receipt.json', r); raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('run', 'check'))
    parser.add_argument('--readiness', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--live', action='store_true')
    a = parser.parse_args()
    if a.stage == 'check':
        print(json.dumps(check(a.output, a.live), indent=2)); return
    require(a.readiness is not None, 'Qualified workload readiness is required')
    run(a)


if __name__ == '__main__':
    main()
