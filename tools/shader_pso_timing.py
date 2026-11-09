"""Collect and replay paired shader and graphics pipeline CPU wall timings."""

import pathlib
import platform
import re
import statistics
import subprocess
import tempfile
import time

from linux_comparison import read, require, save, sha


REPETITIONS = 10
STAGES = {'vert': 'vertexMain', 'frag': 'pixelMain'}
CASES = ('uncached', 'miss', 'hit')


def timed_commands(out, times):
    def invoke(name, *args):
        start = time.perf_counter_ns()
        result = subprocess.run(args, cwd=out, capture_output=True, timeout=120)
        times[name] = time.perf_counter_ns() - start
        if result.returncode:
            raise RuntimeError(f'{name} failed: {result.stderr.decode("utf-8", errors="replace")}')
    return invoke


def measure_dxc(out, work, source, extension, build):
    timing = out/'timing'
    timing.mkdir()
    samples = []
    for index in range(REPETITIONS + 1):
        row = {}
        with tempfile.TemporaryDirectory(dir=work, prefix='objects-') as directory:
            cache = pathlib.Path(directory)
            for stage, entry in STAGES.items():
                output = timing/f'{stage}.{extension}'
                row[stage] = {}
                for state in ('miss', 'hit'):
                    times = {}
                    invoke = timed_commands(out, times)
                    start = time.perf_counter_ns()
                    result = build(source, entry, stage, output, invoke, cache)
                    total = time.perf_counter_ns() - start
                    row[stage][state] = dict(**result, total_ns=total, commands_ns=times)
        if index:
            samples.append(row)
    save(timing/'compiler.json', dict(input_sha256=sha(source), samples=samples))


def measure_metal(out, work, metal, xcrun):
    timing = out/'timing'
    timing.mkdir()
    samples = []
    for index in range(REPETITIONS + 1):
        times = {}
        invoke = timed_commands(out, times)
        with tempfile.TemporaryDirectory(dir=work, prefix='offline-') as directory:
            output = pathlib.Path(directory)
            for stage in STAGES:
                invoke(stage, *metal, out/f'{stage}.metal', '-o', output/f'{stage}.air')
            library = output/'draw.metallib'
            invoke('link', *xcrun, 'metallib', output/'vert.air', output/'frag.air', '-o', library)
            require(library.stat().st_size > 0, 'Measured Metal compiler emitted an empty library')
        if index:
            samples.append(times)
    save(timing/'compiler.json',
         dict(input_sha256={stage: sha(out/f'{stage}.metal') for stage in STAGES}, samples=samples))


def compiler_metrics(out, extension):
    timing = out/'timing'
    evidence = read(timing/'compiler.json')
    samples = evidence['samples']
    require(len(samples) == REPETITIONS, 'Missing shader compilation samples')
    if extension == 'metal':
        require(evidence['input_sha256'] == {stage: sha(out/f'{stage}.metal') for stage in STAGES},
                'Measured MSL source differs from the runtime input')
        require(all(set(row) == {*STAGES, 'link'} and all(type(v) is int and v > 0 for v in row.values())
                    for row in samples), 'Invalid Metal compile or link sample')
        return {f'{name}_ns': [row[name] for row in samples] for name in (*STAGES, 'link')}

    source = out/'consumer-source/triangle.hlsl' if extension == 'dxil' else out/'triangle.hlsl'
    require(evidence['input_sha256'] == sha(source), 'Measured HLSL input differs')
    metrics = {}
    for stage in STAGES:
        object_hash = sha(timing/f'{stage}.{extension}')
        key = samples[0][stage]['miss']['key']
        require(re.fullmatch('[0-9a-f]{64}', key), 'Invalid shader cache key')
        for state in ('miss', 'hit'):
            rows = [row[stage][state] for row in samples]
            commands = {f'{stage}-preprocess', f'{stage}-validate'}
            if state == 'miss':
                commands.add(f'{stage}-compile')
            for row in rows:
                times = row['commands_ns']
                require(row['state'] == state and row['key'] == key and row['object_sha256'] == object_hash,
                        'Measured compiler cache state or shader bytes differ')
                require(set(times) == commands and all(type(v) is int and v > 0 for v in times.values()) and
                        type(row['total_ns']) is int and row['total_ns'] >= sum(times.values()),
                        'Measured compiler activity or timing differs')
            metrics[f'{stage}_{state}_total_ns'] = [row['total_ns'] for row in rows]
            for name in sorted(commands):
                phase = name.removeprefix(stage+'-')
                metrics[f'{stage}_{state}_{phase}_ns'] = [row['commands_ns'][name] for row in rows]
    return metrics


def timing_summary(out, extension, reference):
    timing = out/'timing'
    name = 'timed-consumer' if extension == 'spv' else 'consumer'
    stdout = (out/f'{name}.stdout').read_text(encoding='utf-8')
    rows = [line.split()[1:] for line in stdout.splitlines() if line.startswith('PIPELINE_NS ')]
    require(len(rows) == REPETITIONS and all(len(row) == len(CASES) and
            all(re.fullmatch('[0-9]+', value) for value in row) and int(row[0]) > 0 and int(row[1]) > 0
            for row in rows), 'Missing or invalid pipeline timing sample')
    suffix = 'bin' if extension == 'metal' else 'rgba'
    for case in CASES:
        require((timing/f'{case}.{suffix}').read_bytes() == reference,
                f'Measured {case} pipeline did not reproduce the reference')
    return dict(schema_version=1, target=extension, repetitions=REPETITIONS,
                readback_sha256=sha(timing/f'hit.{suffix}'), readback_bytes=len(reference),
                compiler={key: statistics.median(values) for key, values in compiler_metrics(out, extension).items()},
                pipeline={case+'_ns': statistics.median(int(row[index]) for row in rows)
                          for index, case in enumerate(CASES)},
                pipeline_samples_ns=[[int(value) for value in row] for row in rows])


def finish_timing(out, extension, reference):
    save(out/'timing/summary.json', dict(**timing_summary(out, extension, reference),
         system=platform.platform(), architecture=platform.machine()))


def check_timing(out, extension, reference):
    timing = out/'timing'
    if timing.is_dir():
        summary = read(timing/'summary.json')
        require(summary.pop('system') and summary.pop('architecture'), 'Timing host identity missing')
        require(summary == timing_summary(out, extension, reference), 'Timing summary differs from raw evidence')
