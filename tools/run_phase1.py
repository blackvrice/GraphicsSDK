"""Run validated LLGL D3D12 Phase 1 extension baselines; keep unsupported adapters explicit."""
import argparse
import csv
import datetime
import hashlib
import json
import math
import pathlib
import random
import subprocess
import benchmark_telemetry as telemetry
from run_benchmarks import ROOT, llgl_fingerprint, summarize, comparable, verify_source

SCENARIOS=('shader_cold','shader_warm','pso_hit','pso_miss','compute_blur','compute_color','compute_reduce','compute_buffer','compute_graphics',
           'sprites_10000','sprites_100000','mesh_draw','mesh_instanced','multi_upload','resource_reuse','resource_inflight','switch_binding',
           'video_1080','video_4k','video_p010','video_layers','video_filter')
GPU_SCOPE='explicit_llgl_command_buffer_including_clear_copies_draws_excluding_WriteBuffer_internal_submissions_v1'


def validate(result, scenario, samples, validation):
    expected={'schema_version':1,'sdk':'llgl','api':'d3d12','category':'phase1_extension','scenario':scenario,'warmup':3,
              'validation':validation,'reference_verified':True,
              'readback_verified':None if scenario.startswith('shader_') else True,
              'reference_kind':'compiler_binary_exact_match' if scenario.startswith('shader_') else 'full_gpu_readback_CPU_reference'}
    if any(type(result.get(k)) is not type(v) or result.get(k)!=v for k,v in expected.items()) or len(result.get('samples',[]))!=samples:
        raise ValueError('Wrong Phase 1 workload or missing reference result')
    if type(result.get('reference_checks')) is not int or result['reference_checks']<2:
        raise ValueError('Both pre/post CPU references are required')
    sizes={
        'multi_upload':4*1920*1080*4,'resource_reuse':64*16384*4,'resource_inflight':8*16384*4,
        'video_1080':1920*1080*3//2,'video_4k':3840*2160*3//2,'video_p010':3840*2160*3,
        'video_layers':1920*1080*3,'video_filter':1920*1080*3//2}
    payload=256*256*4 if scenario.startswith('compute_') else sizes.get(scenario,0)
    dispatch=1 if scenario.startswith('compute_') else 0
    draws={'compute_graphics':1,'sprites_10000':1,'sprites_100000':1,'mesh_draw':1001,'mesh_instanced':2,'switch_binding':256,
           'video_1080':1,'video_4k':1,'video_p010':1,'video_layers':2,'video_filter':2}.get(scenario,0)
    if any(type(result.get(key)) is not int or result[key]!=value for key,value in [('payload_bytes',payload),('dispatch_calls',dispatch),('draw_calls',draws)]):
        raise ValueError('Wrong workload byte count or public draw/dispatch count')
    if scenario=='video_layers' and 'two_independent_NV12_videos' not in result.get('contract',''):
        raise ValueError('Two independently uploaded videos are required; a solid overlay is a different workload')
    gpu=not scenario.startswith(('shader_','pso_','resource_'))
    if result.get('gpu_scope')!=(GPU_SCOPE if gpu else 'disabled'):
        raise ValueError('Wrong GPU marker interval')
    for sample in result['samples']:
        for name in ('frame_ms','gpu_ms'):
            value=sample.get(name)
            if name=='gpu_ms' and not gpu:
                if value is not None: raise ValueError('Unexpected GPU instrumentation')
            elif isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value) or value<0:
                raise ValueError('Invalid completed timing value')
    stats=result.get('cache_stats')
    if scenario.startswith(('shader_','pso_')):
        hit=scenario in ('shader_warm','pso_hit'); count=samples+4
        if not isinstance(stats,dict) or type(stats.get('hits')) is not int or type(stats.get('misses')) is not int or stats['hits']!=(count if hit else 0) or stats['misses']!=(0 if hit else count):
            raise ValueError('Unexpected actual application cache hit/miss counts')
        expected_layer='application_shader_bytecode' if scenario.startswith('shader_') else 'application_pso_handle'
        if stats.get('layer')!=expected_layer: raise ValueError('Wrong cache layer')
    elif stats is not None: raise ValueError('Unexpected cache classification')
    if not isinstance(result.get('contract'),str) or not result['contract']: raise ValueError('Missing explicit workload contract')
    telemetry.validate(result,True)


def support():
    return {sdk:{name:('implemented_in_this_harness' if sdk=='llgl' else 'adapter_not_implemented_in_this_harness') for name in SCENARIOS}
            for sdk in ('llgl','sdl','graphicssdk','diligent_core','bgfx','the_forge')}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=pathlib.Path,default=ROOT/'out/bench/Release/phase1_llgl.exe')
    p.add_argument('--sdk',choices=['llgl','sdl','graphicssdk','diligent_core','bgfx','the_forge'],default='llgl')
    p.add_argument('--scenarios',nargs='+',choices=SCENARIOS,default=list(SCENARIOS))
    p.add_argument('--samples',type=int,default=120);p.add_argument('--runs',type=int,default=3)
    p.add_argument('--validation',action='store_true');p.add_argument('--output',type=pathlib.Path,required=True)
    args=p.parse_args()
    if args.sdk!='llgl': p.error('The advanced Phase 1 adapter is implemented for LLGL only; do not substitute native work for another SDK')
    if args.output.exists(): p.error('Output exists; preserve original observations')
    if not 1<=args.samples<=10000 or not 1<=args.runs<=100: p.error('Invalid process/sample count')
    lock=json.loads((ROOT/'benchmarks/competitors/sources.lock.json').read_text());verify_source(ROOT/'.deps/LLGL',lock['llgl']['commit'])
    exe=args.exe.resolve();fingerprint=llgl_fingerprint();binary=exe.read_bytes()
    if fingerprint.encode() not in binary: raise ValueError('Stale compiled Phase 1 source fingerprint')
    available=json.loads(subprocess.check_output([str(exe),'--list'],text=True))
    if tuple(available)!=SCENARIOS: raise ValueError('Executable scenario registry differs from runner')
    jobs=[(name,i) for name in args.scenarios for i in range(args.runs)];random.Random(20261021).shuffle(jobs)
    args.output.mkdir(parents=True);(args.output/'schedule.json').write_text(json.dumps(jobs,indent=2)+'\n')
    (args.output/'support.json').write_text(json.dumps(support(),indent=2)+'\n')
    groups={};exe_hash=hashlib.sha256(binary).hexdigest()
    for name,index in jobs:
        proc=subprocess.run([str(exe),name,str(args.samples),str(int(args.validation))],cwd=exe.parent,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=300)
        base=args.output/f'{name}-{index}';base.with_suffix('.stdout').write_text(proc.stdout,encoding='utf-8');base.with_suffix('.log').write_text(proc.stderr,encoding='utf-8')
        if proc.returncode: raise RuntimeError(f'{name}: failed ({proc.returncode}): {proc.stderr[-1600:]}')
        result=json.loads(proc.stdout.splitlines()[-1]);validate(result,name,args.samples,args.validation)
        if result['sdk_build_source_sha256']!=fingerprint: raise ValueError('Compiled identity differs from current source')
        native_contract=result['contract']
        result.update(run=index,executable_sha256=exe_hash,sdk_commit=lock['llgl']['commit'],configuration='Release',count=1,
            measured_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),native_contract=native_contract,
            contract={'revision':1,'workload':native_contract,'gpu_scope':result['gpu_scope'],'telemetry':telemetry.contract(),
                      'comparison_scope':'single_LLGL_advanced_baseline_other_adapters_unimplemented','driver_cache':'uncontrolled',
                      'vsync':'not_applicable_headless','frames_in_flight':4 if name=='resource_inflight' else 1})
        result['derived']={'effective_payload_gib_per_second':[result['payload_bytes']/1073741824/(s['frame_ms']/1000) if s['frame_ms']>0 and result['payload_bytes'] else None for s in result['samples']],
                           'headless_completed_batches_per_second':[1000/s['frame_ms'] if s['frame_ms']>0 else None for s in result['samples']],
                           'video_batches_over_16_666667_ms':sum(s['frame_ms']>1000/60 for s in result['samples']) if name.startswith('video_') else None,
                           'presentation_dropped_frames':None,'driver_internal_copy_count':None}
        base.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');groups.setdefault(name,[]).append(result)
        print(f'{name} process {index+1}: {args.samples} samples',flush=True)
    summary=[]
    for name,runs in sorted(groups.items()):
        runs.sort(key=lambda r:r['run'])
        if not all(comparable(runs[0],r) for r in runs): raise ValueError('Environment/contract changed within workload')
        row={k:v for k,v in runs[0].items() if k not in ('samples','run','telemetry','derived','cache_stats')}
        row.update(metrics=summarize(runs),process_metrics=telemetry.summarize(runs),cache_stats_by_run=[r['cache_stats'] for r in runs])
        summary.append(row)
    (args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    with (args.output/'summary.csv').open('w',newline='',encoding='utf-8') as file:
        writer=csv.writer(file);writer.writerow(['scenario','metric','p50_ms','p95_ms','p99_ms','mad_ms','run_p50_ms'])
        for r in summary:
            for metric,v in r['metrics'].items():
                if v:writer.writerow([r['scenario'],metric,v['p50'],v['p95'],v['p99'],v['mad'],json.dumps(v['run_p50'])])
    print(args.output/'summary.json')


if __name__=='__main__':main()
