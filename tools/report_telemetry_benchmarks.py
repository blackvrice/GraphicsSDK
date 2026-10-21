"""Verify and report the fixed Windows CPU/memory observation campaigns."""
import csv
import hashlib
import io
import json
import pathlib
import zipfile
import benchmark_telemetry as telemetry
from run_benchmarks import comparable, validate_result

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAMPAIGNS = {
    'allocation': (('llgl', 'sdl', 'graphicssdk'), ('buffers', 'textures'), 1000, 3, 20),
    'draw': (('llgl', 'sdl', 'graphicssdk'), ('draw', 'instanced'), 100000, 20, 120),
    'upload': (('sdl', 'graphicssdk'), ('upload1080', 'upload4k', 'upload4k16f'), 1, 20, 120),
    'pipeline': (('llgl', 'sdl', 'graphicssdk'), ('pipeline',), 1, 20, 120),
}


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def record(path): return {'path': path.relative_to(ROOT).as_posix(), 'sha256': digest(path)}
def read(path): return json.loads(path.read_text(encoding='utf-8'))


def main():
    rows = []; evidence = []; raw_count = 0; samples_total = 0
    for name, (sdks, scenarios, count, warmup, samples) in CAMPAIGNS.items():
        folder = ROOT / 'benchmarks/results' / f'2026-10-21-telemetry-{name}'
        summary = read(folder / 'summary.json')
        if {(r['sdk'], r['scenario']) for r in summary} != {(sdk, s) for sdk in sdks for s in scenarios} or len(summary) != len(sdks) * len(scenarios):
            raise ValueError('Missing/duplicate summary')
        for row in summary:
            runs = []
            for index in range(3):
                path = folder / f"{row['sdk']}-{row['scenario']}-{index}.json"
                result = read(path)
                validate_result(result, row['sdk'], row['scenario'], count, warmup, samples, False, True)
                if result['run'] != index or result['contract']['revision'] != 4 or result['contract'].get('telemetry') != telemetry.contract():
                    raise ValueError('Wrong process/observer contract')
                if not comparable(row, result) or result['executable_sha256'] != row['executable_sha256']:
                    raise ValueError('Different summary/workload identity')
                runs.append(result); raw_count += 1; samples_total += len(result['samples'])
                evidence.append(record(path))
            if row['process_metrics'] != telemetry.summarize(runs):
                raise ValueError('Process metric aggregation differs from raw observations')
        for scenario in scenarios:
            matching = [r for r in summary if r['scenario'] == scenario]
            if not all(comparable(matching[0], r) for r in matching):
                raise ValueError('Different physical environment/workload')
        rows += summary; evidence.append(record(folder / 'summary.json'))
    if len({r['executable_sha256'] for r in rows}) != 1 or digest(ROOT / 'out/bench/Release/gfx_bench.exe') != rows[0]['executable_sha256']:
        raise ValueError('Measurement executable changed')
    if any(r['environment'] != rows[0]['environment'] for r in rows):
        raise ValueError('Physical environment changed')
    paths = {p for pattern in ('*.cpp', '*.hpp', '*.h', '*.cmake') for p in (ROOT / 'benchmarks').rglob(pattern)}
    paths.update((ROOT / 'src').rglob('*.cpp')); paths.update((ROOT / 'include').rglob('*.hpp'))
    paths.update((ROOT / 'tools').glob('*.py')); paths.update((ROOT / 'tools').glob('*.ps1')); paths.update((ROOT / 'tests').glob('*.py'))
    paths.update((ROOT / 'benchmarks/competitors').glob('*.lock.json'))
    paths.update(p for p in (ROOT / 'cmake').rglob('*') if p.is_file())
    paths.update(ROOT / name for name in ('CMakeLists.txt', 'benchmarks/CMakeLists.txt', 'CMakePresets.json', 'LICENSE'))
    identity = hashlib.sha256(''.join(f'{p.relative_to(ROOT).as_posix()}|{digest(p)}\n' for p in sorted(paths)).encode()).hexdigest()
    archive = ROOT / 'benchmarks/reports/sources' / f'telemetry-{identity}.zip'
    members = {p.relative_to(ROOT).as_posix(): p.read_bytes() for p in paths}
    if archive.exists():
        with zipfile.ZipFile(archive) as saved:
            if set(saved.namelist()) != set(members) or any(saved.read(p) != data for p, data in members.items()):
                raise ValueError('Preserve existing source snapshot')
    else:
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as saved:
            for p, data in sorted(members.items()): saved.writestr(p, data)
    output = ROOT / 'benchmarks/reports/2026-10-21-telemetry'; output.mkdir(exist_ok=True)
    evidence += [record(ROOT / 'licenses/generated/windows-benchmarks-telemetry-release' / name) for name in ('inventory.json', 'THIRD_PARTY_NOTICES.txt')]
    (output / 'evidence.json').write_text(json.dumps({'process_count': raw_count, 'frame_samples': samples_total,
        'executable_sha256': rows[0]['executable_sha256'], 'source_archive': record(archive),
        'environment': rows[0]['environment'], 'files': evidence}, indent=2) + '\n', encoding='utf-8')
    stream = io.StringIO(newline=''); writer = csv.writer(stream)
    writer.writerow(['sdk', 'scenario', 'count', 'cpu_one_core_percent_median', 'cpu_percent_run_values', 'lifetime_peak_working_set_mib_max', 'lifetime_peak_commit_mib_max', 'checkpoint_gpu_local_mib_max', 'checkpoint_gpu_nonlocal_mib_max', 'batch_wall_ms_min'])
    for r in rows:
        m = r['process_metrics']
        writer.writerow([r['sdk'], r['scenario'], r['count'], m['cpu_one_core_percent']['median'], json.dumps(m['cpu_one_core_percent']['run_values']),
            *[m[k]['max'] / 1048576 for k in ('lifetime_peak_working_set_bytes', 'lifetime_peak_commit_bytes', 'observed_max_gpu_local_current_bytes', 'observed_max_gpu_nonlocal_current_bytes')], m['wall_ms']['min']])
    (output / 'process-metrics.csv').write_text(stream.getvalue(), encoding='utf-8', newline='')
    lines = ['# CPU/memory observation data', '',
        f'{raw_count} independent processes, {samples_total} frame/batch samples. CPU % is a process-batch observation; memory maxima have different scopes. See docs/CPU_MEMORY_BENCHMARK_REPORT.md.', '',
        '| SDK | Workload | Count | CPU one-core % median | CPU lifetime peak WS MiB max | CPU lifetime peak commit MiB max | GPU local checkpoint max MiB | GPU nonlocal checkpoint max MiB |',
        '|---|---|---:|---:|---:|---:|---:|---:|']
    for r in rows:
        m = r['process_metrics']
        values = [m[k]['max'] / 1048576 for k in ('lifetime_peak_working_set_bytes', 'lifetime_peak_commit_bytes', 'observed_max_gpu_local_current_bytes', 'observed_max_gpu_nonlocal_current_bytes')]
        lines.append(f"| {r['sdk']} | {r['scenario']} | {r['count']} | {m['cpu_one_core_percent']['median']:.2f} | " + ' | '.join(f'{v:.2f}' for v in values) + ' |')
    (output / 'TABLE.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'PASS: {raw_count} processes, {samples_total} samples; {archive.name}')


if __name__ == '__main__': main()
