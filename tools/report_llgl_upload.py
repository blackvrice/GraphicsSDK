"""Verify fresh public LLGL buffer-to-texture upload observations."""
import csv
import hashlib
import io
import json
import pathlib
import zipfile
import benchmark_telemetry as telemetry
from run_benchmarks import comparable, llgl_fingerprint, upload_contract, validate_result, summarize
from compare_results import compare

ROOT=pathlib.Path(__file__).resolve().parents[1]
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def record(p): return {'path':p.relative_to(ROOT).as_posix(),'sha256':digest(p)}
def read(p): return json.loads(p.read_text(encoding='utf-8'))

def main():
    groups={}; evidence=[]; total=0
    for mode in ('plain','telemetry'):
        folder=ROOT/'benchmarks/results'/f'2026-10-21-llgl-upload-{mode}'
        rows=read(folder/'summary.json'); observed=mode=='telemetry'
        if len(rows)!=9 or {(r['sdk'],r['scenario']) for r in rows}!={(sdk,s) for sdk in ('llgl','sdl','graphicssdk') for s in ('upload1080','upload4k','upload4k16f')}:
            raise ValueError('Missing/duplicate upload summary')
        for row in rows:
            runs=[]
            for index in range(3):
                p=folder/f"{row['sdk']}-{row['scenario']}-{index}.json"; r=read(p)
                validate_result(r,row['sdk'],row['scenario'],1,20,120,False,observed)
                if r['run']!=index or r['contract']['revision']!=5 or any(r['contract'].get(k)!=v for k,v in upload_contract().items()): raise ValueError('Wrong upload contract')
                if not comparable(row,r) or r['executable_sha256']!=row['executable_sha256']: raise ValueError('Inconsistent process identity')
                if r['sdk']=='llgl' and r['sdk_build_source_sha256']!=llgl_fingerprint(): raise ValueError('Stale LLGL harness identity')
                if observed:
                    wire=read_stdout(p.with_suffix('.stdout'))
                    if any(r.get(k)!=v for k,v in wire.items()): raise ValueError('Raw stdout differs from enriched JSON')
                runs.append(r); total+=len(r['samples'])
            if row['metrics']!=summarize(runs) or (observed and row['process_metrics']!=telemetry.summarize(runs)): raise ValueError('Wrong metric aggregation')
        for scenario in ('upload1080','upload4k','upload4k16f'):
            matching=[r for r in rows if r['scenario']==scenario]
            if not all(comparable(matching[0],r) for r in matching): raise ValueError('Different workload/device contract')
        groups[mode]=rows; evidence.extend(record(p) for p in sorted(folder.iterdir()) if p.is_file())
    rows=groups['plain']+groups['telemetry']
    if len({r['executable_sha256'] for r in rows})!=1 or digest(ROOT/'out/bench/Release/gfx_bench.exe')!=rows[0]['executable_sha256']: raise ValueError('Multiple/stale binaries')
    if any(r['environment']!=rows[0]['environment'] for r in rows): raise ValueError('Physical environment changed')
    if comparable(groups['plain'][0],groups['telemetry'][0]): raise ValueError('Observer profiles must stay separate')
    paths={p for pattern in ('*.cpp','*.hpp','*.h','*.cmake') for p in (ROOT/'benchmarks').rglob(pattern)}
    paths.update((ROOT/'src').rglob('*.cpp')); paths.update((ROOT/'include').rglob('*.hpp'))
    paths.update((ROOT/'tools').glob('*.py')); paths.update((ROOT/'tools').glob('*.ps1')); paths.update((ROOT/'tests').glob('*.py'))
    paths.update((ROOT/'benchmarks/competitors').glob('*.lock.json')); paths.update(p for p in (ROOT/'cmake').rglob('*') if p.is_file())
    paths.update(ROOT/name for name in ('CMakeLists.txt','benchmarks/CMakeLists.txt','CMakePresets.json','LICENSE'))
    identity=hashlib.sha256(''.join(f'{p.relative_to(ROOT).as_posix()}|{digest(p)}\n' for p in sorted(paths)).encode()).hexdigest()
    archive=ROOT/'benchmarks/reports/sources'/f'llgl-upload-{identity}.zip'
    members={p.relative_to(ROOT).as_posix():p.read_bytes() for p in paths}
    if archive.exists():
        with zipfile.ZipFile(archive) as z:
            if set(z.namelist())!=set(members) or any(z.read(k)!=v for k,v in members.items()): raise ValueError('Preserve existing snapshot')
    else:
        with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
            for k,v in sorted(members.items()): z.writestr(k,v)
    out=ROOT/'benchmarks/reports/2026-10-21-llgl-upload'; out.mkdir(exist_ok=True)
    differences=[]
    for mode,group in groups.items():
        for sdk in ('llgl','sdl'): differences.extend({'profile':mode,**r} for r in compare(group,group,sdk,'graphicssdk'))
    stream=io.StringIO(newline=''); writer=csv.DictWriter(stream,fieldnames=list(differences[0])); writer.writeheader(); writer.writerows(differences)
    (out/'comparison.csv').write_text(stream.getvalue(),encoding='utf-8',newline='')
    lines=['# Fresh upload observations','','P50/P95/P99 in milliseconds. Observer profiles are separate. No whole-SDK ranking. See docs/LLGL_UPLOAD_BENCHMARK_REPORT.md.','',
           '| Profile | SDK | Workload | P50 | P95 | P99 | Run medians |','|---|---|---|---:|---:|---:|---|']
    for mode,group in groups.items():
        for r in group:
            v=r['metrics']['upload_latency_ms']; lines.append(f"| {mode} | {r['sdk']} | {r['scenario']} | {v['p50']:.6f} | {v['p95']:.6f} | {v['p99']:.6f} | {v['run_p50']} |")
    (out/'TABLE.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    evidence.extend(record(ROOT/'licenses/generated/windows-benchmarks-llgl-upload-release'/name) for name in ('inventory.json','THIRD_PARTY_NOTICES.txt'))
    (out/'evidence.json').write_text(json.dumps({'processes':54,'samples':total,'executable_sha256':rows[0]['executable_sha256'],
        'llgl_build_fingerprint':llgl_fingerprint(),'source_archive':record(archive),'environment':rows[0]['environment'],'files':evidence},indent=2)+'\n',encoding='utf-8')
    print(f'PASS: 54 processes, {total} samples; {archive.name}')

def read_stdout(path): return json.loads(path.read_text(encoding='utf-8').splitlines()[-1])
if __name__=='__main__': main()
