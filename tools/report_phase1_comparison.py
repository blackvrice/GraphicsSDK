"""Verify complete E-H paired observations, raw output, contracts and artifacts before reporting."""
import argparse
import hashlib
import json
import pathlib
import zipfile
import benchmark_telemetry as telemetry
from phase1_acceptance import evaluate
from run_benchmarks import ROOT,comparable,llgl_fingerprint,summarize
from run_phase1 import ADVANCED,ABI,comparison_contract,validate


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def record(path):return {'path':path.resolve().relative_to(ROOT).as_posix(),'sha256':digest(path)}


def verify_pairs(rows, processes, runs=3, samples=120):
    """Reject partial, instrumented, mismatched or reaggregated evidence, including duplicate identities."""
    keys={(sdk,name) for sdk in ('llgl','sdl') for name in ADVANCED}
    if len(rows)!=len(keys) or {(r['sdk'],r['scenario']) for r in rows}!=keys:
        raise ValueError('Missing or duplicate E-H SDK coverage')
    paired={}
    for row in rows:
        sdk,name=row['sdk'],row['scenario'];group=processes.get((sdk,name),[])
        if len(group)!=runs or sorted(r.get('run',-1) for r in group)!=list(range(runs)):
            raise ValueError('Missing or duplicate independent process repetitions')
        group=sorted(group,key=lambda r:r['run'])
        omitted={'samples','run','telemetry','derived','cache_stats'}
        if any(row.get(k)!=v for k,v in group[0].items() if k not in omitted):
            raise ValueError('Summary metadata differs from first independent process')
        for r in group:
            native={**r,'contract':r['native_contract']};validate(native,name,samples,False,sdk,True)
            if r.get('cpu_only') is not True or r.get('native_shader_abi')!=ABI[sdk] or r['contract']!=comparison_contract(native):
                raise ValueError('Wrong CPU comparison or shared workload identity')
            if not comparable(row,r) or row.get('executable_sha256')!=r.get('executable_sha256') or row.get('sdk_build_source_sha256')!=r.get('sdk_build_source_sha256'):
                raise ValueError('Environment, artifact or contract changed within workload')
        if row['metrics']!=summarize(group) or row['process_metrics']!=telemetry.summarize(group):
            raise ValueError('Summary differs from all original samples')
        paired.setdefault(name,{})[sdk]=row
    for pair in paired.values():
        if not comparable(pair['llgl'],pair['sdl']):raise ValueError('Unmatched physical GPU/compiler/workload or instrumentation')
    anchor=rows[0]
    if any(not comparable({**anchor,'scenario':r['scenario'],'contract':r['contract']},r) for r in rows):
        raise ValueError('Physical environment changed between workloads')
    return paired


def source_archive():
    paths={p for pattern in ('*.cpp','*.hpp','*.h','*.cmake') for p in (ROOT/'benchmarks').rglob(pattern)}
    paths.update((ROOT/'src').rglob('*.cpp'));paths.update((ROOT/'include').rglob('*.hpp'))
    for folder,pattern in [('tools','*.py'),('tools','*.ps1'),('tests','*.py'),('benchmarks/competitors','*.lock.json')]:paths.update((ROOT/folder).glob(pattern))
    paths.update(p for p in (ROOT/'cmake').rglob('*') if p.is_file());paths.update(ROOT/name for name in ('CMakeLists.txt','benchmarks/CMakeLists.txt','CMakePresets.json','LICENSE'))
    identity=hashlib.sha256(''.join(f'{p.relative_to(ROOT).as_posix()}|{digest(p)}\n' for p in sorted(paths)).encode()).hexdigest()
    archive=ROOT/'benchmarks/reports/sources'/f'phase1-{identity}.zip';members={p.relative_to(ROOT).as_posix():p.read_bytes() for p in paths}
    if archive.exists():
        with zipfile.ZipFile(archive) as z:
            if set(z.namelist())!=set(members) or any(z.read(k)!=v for k,v in members.items()):raise ValueError('Existing source archive differs')
    else:
        with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
            for k,v in sorted(members.items()):z.writestr(k,v)
    return archive


def verify_historical_baseline():
    folder=ROOT/'benchmarks/reports/2026-10-21-phase1-final';evidence=read(folder/'evidence.json')
    for item in evidence['files']+[evidence['source_archive']]:
        if digest(ROOT/item['path'])!=item['sha256']:raise ValueError('Historical baseline evidence changed')
    if evidence['processes']!=66 or evidence['samples']!=7920:raise ValueError('Historical baseline coverage differs')
    return folder,evidence


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--results',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--inventory',type=pathlib.Path,required=True);p.add_argument('--validation',type=pathlib.Path,required=True);args=p.parse_args()
    for field in ('results','output','inventory','validation'):
        path=getattr(args,field).resolve();path.relative_to(ROOT);setattr(args,field,path)
    if args.output.exists():p.error('Preserve the previous report; use a new output directory')
    folder=args.results;rows=read(folder/'summary.json');processes={};lock=read(ROOT/'benchmarks/competitors/sources.lock.json')
    jobs=read(folder/'schedule.json');expected={(sdk,name,i) for sdk in ('llgl','sdl') for name in ADVANCED for i in range(3)}
    if len(jobs)!=84 or set(map(tuple,jobs))!=expected:raise ValueError('Incomplete randomized process schedule')
    expected_files={'schedule.json','support.json','summary.json','summary.csv'}
    for sdk,name,index in jobs:
        base=folder/f'{sdk}-{name}-{index}';r=read(base.with_suffix('.json'));wire=read(base.with_suffix('.stdout'))
        validate(wire,name,120,False,sdk,True)
        if r['sdk']!=sdk or r['scenario']!=name or r['run']!=index:raise ValueError('Wrong file/process identity')
        native={**r,'contract':r['native_contract']}
        if any(native.get(k)!=v for k,v in wire.items()):raise ValueError('Native stdout differs from observation')
        if r['sdk_build_source_sha256']!=llgl_fingerprint(sdk=sdk) or r['sdk_commit']!=lock[sdk]['commit'] or r['executable_sha256']!=digest(ROOT/f'out/bench/Release/phase1_{sdk}.exe'):
            raise ValueError('Stale artifact/source or dependency identity')
        processes.setdefault((sdk,name),[]).append(r)
        expected_files.update(base.with_suffix(suffix).name for suffix in ('.json','.stdout','.log'))
    if {p.name for p in folder.iterdir()}!=expected_files:raise ValueError('Unexpected/missing raw campaign files')
    paired=verify_pairs(rows,processes)
    validation_summary=read(args.validation/'native/summary.json')
    if len(validation_summary)!=28:raise ValueError('Missing validation workload')
    for sdk in ('llgl','sdl'):
        for name in ADVANCED:
            path=args.validation/f'native/{sdk}-{name}-0.json';r=read(path);wire=read(path.with_suffix('.stdout'))
            validate(wire,name,5,True,sdk,True)
            native={**r,'contract':r['native_contract']};validate(native,name,5,True,sdk,True)
            if any(native.get(k)!=v for k,v in wire.items()) or r['sdk_build_source_sha256']!=llgl_fingerprint(sdk=sdk) or r['executable_sha256']!=digest(ROOT/f'out/bench/Release/phase1_{sdk}.exe'):
                raise ValueError('Validation artifact/raw mismatch')
    old_folder,old=verify_historical_baseline()
    archive=source_archive();args.output.mkdir(parents=True)
    lines=['# LLGL / SDL3 E-H CPU completion observations','',
           '14 workloads × 2 SDKs × 3 independent processes × 120 samples = 84 processes / 10,080 samples. GPU queries disabled in both. Fixed seed interleaving; no outliers removed.',
           '','Public semantic workloads match; native shader binding ABI differs. LLGL structured uint32 vs SDL raw byte-address buffers. Integer video planes use LLGL sampled SRV vs SDL read-only storage SRV. Fragment register spaces and mesh input semantics follow each public API.',
           '','CPU times include SDK-managed staging/copies/record/submit/completion. Untimed full CPU readbacks run before and after each process. Headless, one batch in flight. Telemetry checkpoints run outside frame timing, inside process batch. Driver/OS caches, GPU clocks and background load uncontrolled.',
           '','SDL / LLGL is a cost ratio for this workload and machine, not a universal speedup or a matched GPU timestamp comparison.',
           '','| Workload | LLGL CPU P50 ms | SDL CPU P50 ms | SDL / LLGL | LLGL P95 / P99 ms | SDL P95 / P99 ms |',
           '|---|---:|---:|---:|---:|---:|']
    for name in ADVANCED:
        a=paired[name]['llgl']['metrics']['frame_ms'];b=paired[name]['sdl']['metrics']['frame_ms']
        lines.append(f"| {name} | {a['p50']:.6f} | {b['p50']:.6f} | {b['p50']/a['p50']:.3f} | {a['p95']:.6f} / {a['p99']:.6f} | {b['p95']:.6f} / {b['p99']:.6f} |")
    lines+=['','| SDK / workload | CPU process % median | Lifetime CPU peak WS MiB max | GPU local checkpoint MiB max | CPU frame MAD ms | Independent run P50 ms |',
            '|---|---:|---:|---:|---:|---|']
    for row in rows:
        m=row['process_metrics'];f=row['metrics']['frame_ms'];lines.append(f"| {row['sdk']} / {row['scenario']} | {m['cpu_one_core_percent']['median']:.2f} | {m['lifetime_peak_working_set_bytes']['max']/1048576:.2f} | {m['observed_max_gpu_local_current_bytes']['max']/1048576:.2f} | {f['mad']:.6f} | "+', '.join(f'{v:.6f}' for v in f['run_p50'])+' |')
    lines+=['','GPU memory maxima are checkpoint lower bounds. Short-batch process CPU values are limited by OS accounting resolution. No continuous transient GPU peak, actual presentation drops, decoder or external texture import acceptance. Phase 1 remains incomplete.']
    (args.output/'TABLE.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    acceptance={'advanced_native_cpu_references':True,'repeated_advanced_observations':True,
                'application_cache_cold_warm_hit_miss':True,'matching_advanced_sdk_comparisons':len(paired)==14,
                'historical_cache_and_resource_baseline':record(old_folder/'evidence.json'),
                'gpu_memory_trace':{'status':'blocked','wpr_error':'0xc5585011','elevation_result':'Prior Windows operation canceled; not retried'},
                'limits':['Semantic public-workload CPU comparison; buffer view and binding ABI differ','No SDL public GPU timestamp path; both CPU profiles disable queries',
                          'GPU memory checkpoint lower bounds, continuous allocation trace missing','Other advanced SDK adapters remain unimplemented; SDL cache/resource/binding adapters remain unimplemented']}
    (args.output/'acceptance-evidence.json').write_text(json.dumps(acceptance,indent=2)+'\n',encoding='utf-8')
    (args.output/'acceptance.json').write_text(json.dumps(evaluate(acceptance),indent=2)+'\n',encoding='utf-8')
    files=[record(p) for p in sorted(folder.iterdir()) if p.is_file()]
    files += [record(p) for p in sorted(args.validation.rglob('*')) if p.is_file()]
    files += [record(args.inventory/name) for name in ('inventory.json','THIRD_PARTY_NOTICES.txt')]
    evidence={'processes':84,'samples':10080,'workloads':14,'matching_level':'public_semantic_workload_CPU_completion_binding_ABI_differences_explicit',
              'executables':{s:{'sha256':digest(ROOT/f'out/bench/Release/phase1_{s}.exe'),'source_fingerprint':llgl_fingerprint(sdk=s),'sdk_commit':lock[s]['commit']} for s in ('llgl','sdl')},
              'source_archive':record(archive),'historical_baseline':record(old_folder/'evidence.json'),'files':files}
    (args.output/'evidence.json').write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf-8')
    print(f'PASS: 14 E-H pairs, 84 processes, 10080 samples. Phase 1 complete: {evaluate(acceptance)["phase1_complete"]}. {archive.name}')


if __name__=='__main__':main()
