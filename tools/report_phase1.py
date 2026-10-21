"""Verify the 22 LLGL advanced baselines and explicitly retain incomplete Phase 1 gates."""
import csv
import hashlib
import io
import json
import pathlib
import zipfile
import benchmark_telemetry as telemetry
from run_phase1 import SCENARIOS,validate
from run_benchmarks import llgl_fingerprint,summarize
from phase1_acceptance import evaluate

ROOT=pathlib.Path(__file__).resolve().parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def record(p):return {'path':p.relative_to(ROOT).as_posix(),'sha256':digest(p)}
def read(p):return json.loads(p.read_text(encoding='utf-8'))


def main():
    folder=ROOT/'benchmarks/results/2026-10-21-phase1-final';rows=read(folder/'summary.json')
    if len(rows)!=len(SCENARIOS) or {r['scenario'] for r in rows}!=set(SCENARIOS):raise ValueError('Missing workload coverage')
    count=0
    for row in rows:
        runs=[]
        for index in range(3):
            p=folder/f"{row['scenario']}-{index}.json";r=read(p)
            native={**r,'contract':r['native_contract']};validate(native,row['scenario'],120,False)
            if r['run']!=index or r['sdk_build_source_sha256']!=llgl_fingerprint():raise ValueError('Wrong process/source identity')
            wire=json.loads(p.with_suffix('.stdout').read_text(encoding='utf-8').splitlines()[-1])
            if any(native.get(k)!=v for k,v in wire.items()):raise ValueError('Raw stdout differs from validated observation')
            if r['contract']!=row['contract'] or r['environment']!=row['environment']:raise ValueError('Different run workload/environment')
            runs.append(r);count+=len(r['samples'])
        if row['metrics']!=summarize(runs) or row['process_metrics']!=telemetry.summarize(runs):raise ValueError('Aggregation mismatch')
    if len({r['executable_sha256'] for r in rows})!=1 or digest(ROOT/'out/bench/Release/phase1_llgl.exe')!=rows[0]['executable_sha256']:raise ValueError('Different/stale executable')
    if any(r['environment']!=rows[0]['environment'] for r in rows):raise ValueError('Physical environment changed')
    output=ROOT/'benchmarks/reports/2026-10-21-phase1-final';output.mkdir(exist_ok=True)
    lines=['# Advanced LLGL observations','','22 scenarios, 66 processes, 7,920 samples. Other advanced adapters are unimplemented. This is not Phase 1 completion. GPU and CPU columns have different intervals.','',
           '| Workload | CPU frame P50 ms | GPU explicit-list P50 ms | CPU process % median | Lifetime CPU peak WS MiB max | GPU local checkpoint MiB max |',
           '|---|---:|---:|---:|---:|---:|']
    for r in rows:
        m=r['process_metrics'];gpu=r['metrics']['gpu_ms'];lines.append(f"| {r['scenario']} | {r['metrics']['frame_ms']['p50']:.6f} | "+(f"{gpu['p50']:.6f}" if gpu else 'null')+f" | {m['cpu_one_core_percent']['median']:.2f} | {m['lifetime_peak_working_set_bytes']['max']/1048576:.2f} | {m['observed_max_gpu_local_current_bytes']['max']/1048576:.2f} |")
    (output/'TABLE.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    acceptance={'advanced_native_cpu_references':True,'repeated_advanced_observations':count==7920,
                'application_cache_cold_warm_hit_miss':True,'matching_advanced_sdk_comparisons':False,
                'gpu_memory_trace':{'status':'blocked','wpr_error':'0xc5585011','elevation_result':'Windows reported operation canceled; no elevated capture obtained'},
                'limits':['LLGL advanced adapter only; SDL/GraphicsSDK/Diligent/bgfx/Forge advanced adapters are unimplemented',
                          'Application caches controlled; driver/OS caches uncontrolled','GPU process memory is checkpoint observation, not continuous transient peak',
                          'External texture import and presentation drop accounting unimplemented; decode/encode excluded']}
    (output/'acceptance-evidence.json').write_text(json.dumps(acceptance,indent=2)+'\n',encoding='utf-8')
    (output/'acceptance.json').write_text(json.dumps(evaluate(acceptance),indent=2)+'\n',encoding='utf-8')
    paths={p for pattern in ('*.cpp','*.hpp','*.h','*.cmake') for p in (ROOT/'benchmarks').rglob(pattern)}
    paths.update((ROOT/'src').rglob('*.cpp'));paths.update((ROOT/'include').rglob('*.hpp'));paths.update((ROOT/'tools').glob('*.py'));paths.update((ROOT/'tools').glob('*.ps1'));paths.update((ROOT/'tests').glob('*.py'))
    paths.update((ROOT/'benchmarks/competitors').glob('*.lock.json'));paths.update(p for p in (ROOT/'cmake').rglob('*') if p.is_file());paths.update(ROOT/name for name in ('CMakeLists.txt','benchmarks/CMakeLists.txt','CMakePresets.json','LICENSE'))
    identity=hashlib.sha256(''.join(f'{p.relative_to(ROOT).as_posix()}|{digest(p)}\n' for p in sorted(paths)).encode()).hexdigest()
    archive=ROOT/'benchmarks/reports/sources'/f'phase1-{identity}.zip';members={p.relative_to(ROOT).as_posix():p.read_bytes() for p in paths}
    if archive.exists():
        with zipfile.ZipFile(archive) as z:
            if set(z.namelist())!=set(members) or any(z.read(k)!=v for k,v in members.items()):raise ValueError('Preserve existing source archive')
    else:
        with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
            for k,v in sorted(members.items()):z.writestr(k,v)
    files=[record(p) for p in sorted(folder.iterdir()) if p.is_file()]+[record(ROOT/'licenses/generated/windows-benchmarks-phase1-final-release'/name) for name in ('inventory.json','THIRD_PARTY_NOTICES.txt')]
    (output/'evidence.json').write_text(json.dumps({'processes':66,'samples':count,'executable_sha256':rows[0]['executable_sha256'],'llgl_source_fingerprint':llgl_fingerprint(),'source_archive':record(archive),'files':files},indent=2)+'\n',encoding='utf-8')
    print(f'PASS: 66 processes, {count} samples verified. Phase 1 complete: {evaluate(acceptance)["phase1_complete"]}. {archive.name}')


if __name__=='__main__':main()
