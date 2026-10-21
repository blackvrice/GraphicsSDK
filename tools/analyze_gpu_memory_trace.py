"""Reconstruct loss-free DxgKrnl process usage and allocation lifetimes; fail closed on gaps."""
import argparse
import hashlib
import json
import pathlib
import zipfile
from run_benchmarks import ROOT,llgl_fingerprint
from run_phase1 import validate

PROVIDER='802ec45a-1e99-4b83-9920-87c98277ba9d'
NAMES=('positive_control','resource_inflight','multi_upload','video_4k','video_p010')
VERSIONS={33:3,34:3,35:3,225:1,226:1,367:0,371:0}


def require(condition,message):
    if not condition:raise ValueError(message)
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path,value):path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
def uint(value):return type(value) is int and 0<=value<2**64


def validate_trace_metadata(trace):
    require(trace.get('clock')=='raw_QPC' and type(trace.get('clock_type')) is int and trace['clock_type']==1 and uint(trace.get('qpc_frequency')) and trace['qpc_frequency']>0,'Unsupported trace clock')
    for field in ('header_events_lost','consumer_events_lost','buffers_lost','process_trace_status'):
        require(type(trace.get(field)) is int and trace[field]==0,'Loss or incomplete trace processing')
    require(trace.get('decode_failed') is False,'TDH decoding failure')


def reconstruct(events,pid,frequency):
    """Track global allocations by (adapter, handle), including System-owned destruction.

    ProcessAllocation 225/226 describes CPU mappings and is never added to GPU usage.
    Usage/commitment are independent change streams; allocation sizes are a separate metric.
    """
    require(type(pid) is int and pid>0 and uint(frequency) and frequency>0,'Invalid process/clock identity')
    active={};lifetimes=[];filtered=[];streams={};adapters=set();peak_alloc=0;live_bytes=0;previous=-1
    for event in events:
        require(uint(event.get('qpc')) and event['qpc']>=previous,'ETW time inversion');previous=event['qpc']
        event_id=event.get('id');require(event_id in VERSIONS and event.get('version')==VERSIONS[event_id],'Unreviewed event schema/version')
        key=(event.get('adapter'),event.get('handle'))
        belongs=event.get('pid')==pid or (event_id==34 and key in active)
        if not belongs:continue
        require(uint(event.get('pid')),'Missing payload process ID')
        filtered.append({**event,'owner_pid':pid})
        if event_id in (33,34,35):
            require(all(uint(event.get(k)) for k in ('adapter','handle','bytes')) and event['adapter'] and event['handle'],'Invalid allocation event')
            adapters.add(event['adapter'])
            if event_id==35:raise ValueError('Rundown allocation without a fresh-process creation boundary')
            if event_id==33:
                require(key not in active,'Duplicate allocation create / live handle reuse')
                active[key]=event;live_bytes+=event['bytes'];peak_alloc=max(peak_alloc,live_bytes)
            else:
                require(key in active,'Destroy without allocation create')
                start=active.pop(key);require(start['bytes']==event['bytes'],'Allocation size changed at destroy')
                live_bytes-=start['bytes'];require(live_bytes>=0,'Negative allocation balance')
                lifetimes.append({'adapter':key[0],'handle':key[1],'bytes':start['bytes'],'create_qpc':start['qpc'],
                                  'destroy_qpc':event['qpc'],'destroy_payload_pid':event['pid'],
                                  'lifetime_ms':(event['qpc']-start['qpc'])/frequency*1000})
        elif event_id in (367,371):
            require(all(uint(event.get(k)) for k in ('adapter','physical_adapter','segment_group','old_bytes','new_bytes')),'Invalid memory change')
            require(event['physical_adapter']==0 and event['segment_group'] in (0,1),'Unreviewed GPU node or segment')
            adapters.add(event['adapter']);stream=(event_id,event['adapter'],event['physical_adapter'],event['segment_group'])
            series=streams.setdefault(stream,[])
            require(event['old_bytes']==(series[-1]['new_bytes'] if series else 0),'Broken old/new usage chain or missing process start')
            series.append(event)
    require(not active and live_bytes==0 and lifetimes,'Unreconciled or absent GPU allocation lifetimes')
    require(len(adapters)==1,'Process used multiple or unknown native GPU adapters')
    peaks={};counts={}
    for event_id,label in ((367,'usage'),(371,'commitment')):
        for segment,suffix in ((0,'local'),(1,'nonlocal')):
            matching=[v for k,v in streams.items() if k[0]==event_id and k[3]==segment]
            require(len(matching)==1 and matching[0][-1]['new_bytes']==0,'Missing memory stream or process teardown')
            series=matching[0];peaks[f'{label}_{suffix}_peak_bytes']=max(e['new_bytes'] for e in series);counts[f'{label}_{suffix}_changes']=len(series)
    return {'pid':pid,'allocation_lifetime_reconciled':True,'allocation_count':len(lifetimes),
            'system_context_destructions':sum(e['destroy_payload_pid']!=pid for e in lifetimes),
            'vidmm_allocation_live_size_peak_bytes':peak_alloc,**peaks,**counts,
            'scope':'complete_fresh_process_lifetime_including_device_setup_references_and_teardown',
            'allocation_metric':'registered_VidMm_global_allocation_sizes_not_resident_usage',
            'usage_metric':'DxgKrnl_VidMmProcessUsageChange_change_events_not_periodic_samples'},filtered,lifetimes


def positive_control(control,events,lifetimes,frequency):
    require(control.get('revision')==1 and control.get('kind')=='transient_gpu_memory_positive_control','Wrong positive control')
    require(control.get('validation') is True and control.get('readback_verified') is True and control.get('reference_checks')==3,'Positive control reference failed')
    require(control.get('buffer_bytes')==32*1024*1024 and control.get('simultaneous_buffer_count')==2,'Wrong positive control payload')
    require(control.get('qpc_frequency')==frequency,'Control/ETL QPC clocks differ')
    intervals=control.get('intervals',[]);points=control.get('telemetry',{}).get('checkpoints',[])
    require(len(intervals)==3 and len(points)==4 and points[0]['phase']=='baseline','Missing positive control intervals/checkpoints')
    checkpoint_max=max(p['gpu_local_current_bytes'] for p in points);results=[]
    previous=0
    for index,interval in enumerate(intervals):
        before,live,after=(interval[k] for k in ('before_qpc','live_qpc','after_qpc'))
        require(all(uint(v) for v in (before,live,after)) and previous<before<live<after,'Invalid control interval');previous=after
        require(points[index+1]['phase']=='sample_end' and points[index+1]['sample']==index,'Unexpected live observation in checkpoint control')
        live_allocs=[a for a in lifetimes if before<=a['create_qpc']<live<a['destroy_qpc']<=after and a['bytes']>=control['buffer_bytes']]
        require(len(live_allocs)>=2,'Two overlapping transient GPU allocations not reconstructed inside checkpoint gap')
        changes=[e for e in events if e['id']==367 and e['segment_group']==0 and before<=e['qpc']<=after]
        require(changes,'No local usage changes inside positive interval')
        peak=max(e['new_bytes'] for e in changes)
        require(peak>=checkpoint_max+control['buffer_bytes'],'Trace did not detect a transient peak missed by all checkpoints')
        results.append({'index':index,'overlapping_transient_allocations':len(live_allocs),'trace_local_peak_bytes':peak,
                        'checkpoint_local_max_bytes':checkpoint_max,'missed_by_checkpoint_bytes':peak-checkpoint_max,
                        'interval_ms':(after-before)/frequency*1000})
    return results


def source_archive():
    paths=[*sorted((ROOT/'tools/gpu_trace').glob('*')),ROOT/'tools/capture_phase1_gpu_lifetime.ps1',ROOT/'tools/analyze_gpu_memory_trace.py',ROOT/'tools/phase1_acceptance.py',ROOT/'tests/test_gpu_memory_trace.py']
    paths += [ROOT/'benchmarks/common'/n for n in ('Adapter.hpp','Environment.cpp','Telemetry.hpp','Telemetry.cpp')]
    paths += [ROOT/'tools'/n for n in ('run_benchmarks.py','run_phase1.py','benchmark_telemetry.py','audit_licenses.py')]
    paths += [ROOT/'licenses/policy.json',ROOT/'benchmarks/competitors/sources.lock.json',ROOT/'LICENSE']
    identity=hashlib.sha256(''.join(f'{p.relative_to(ROOT).as_posix()}|{digest(p)}\n' for p in paths).encode()).hexdigest()
    archive=ROOT/'benchmarks/reports/sources'/f'gpu-memory-{identity}.zip'
    if archive.exists():
        with zipfile.ZipFile(archive) as z:require(all(z.read(p.relative_to(ROOT).as_posix())==p.read_bytes() for p in paths),'Existing source archive differs')
    else:
        with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
            for p in paths:z.writestr(p.relative_to(ROOT).as_posix(),p.read_bytes())
    return {'path':archive.relative_to(ROOT).as_posix(),'sha256':digest(archive)}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--capture',type=pathlib.Path,required=True);parser.add_argument('--output',type=pathlib.Path,required=True);args=parser.parse_args()
    capture=args.capture.resolve();output=args.output.resolve();capture.relative_to(ROOT);output.relative_to(ROOT)
    require(not output.exists(),'Preserve existing report')
    manifest=read(capture/'capture.json');trace=read(capture/'trace.json')
    require(manifest.get('session_started') is True and manifest.get('session_stopped') is True and manifest.get('provider')==PROVIDER and manifest.get('keywords')=='0xc5','Incomplete capture/session')
    validate_trace_metadata(trace)
    processes=manifest.get('processes',[]);require([p['name'] for p in processes]==list(NAMES) and len({p['pid'] for p in processes})==5,'Missing or duplicate captured processes')
    exporter=ROOT/'out/gpu-trace/Release/gpu_etl_export.exe';require(digest(exporter)==manifest['exporter_sha256'],'Exporter artifact changed')
    events=[json.loads(line) for line in (capture/'events.jsonl').read_text().splitlines()]
    require(len(events)==trace.get('exported_events'),'Event export truncated')
    summaries=[];filtered_all=[];all_lifetimes={};control=None;environment=None
    for process in processes:
        require(type(process.get('exit_code')) is int and process['exit_code']==0,'Captured child exit code missing/failed')
        exe=pathlib.Path(process['exe']);require(digest(exe)==process['executable_sha256'],'Captured executable changed')
        native=read(capture/(process['name']+'.stdout'))
        if environment is None:environment=native['environment']
        require(native['environment']==environment,'Physical environment changed')
        if process['name']=='positive_control':
            require(native['pid']==process['pid'],'Positive control process mismatch');control=native
        else:
            require(process['arguments']==[process['name'],'120','0','--cpu-only'],'Wrong captured benchmark arguments')
            validate(native,process['name'],120,False,'llgl',True)
            require(native['sdk_build_source_sha256']==llgl_fingerprint(),'Stale benchmark source identity')
        result,filtered,lifetimes=reconstruct(events,process['pid'],trace['qpc_frequency'])
        result['scenario']=process['name'];result['executable_sha256']=process['executable_sha256'];summaries.append(result);filtered_all+=filtered;all_lifetimes[process['name']]=lifetimes
    require(trace['first_qpc']<control['intervals'][0]['before_qpc']<control['intervals'][-1]['after_qpc']<trace['last_qpc'],'Trace does not bracket control')
    controls=positive_control(control,[e for e in filtered_all if e['owner_pid']==control['pid']],all_lifetimes['positive_control'],trace['qpc_frequency'])
    filtered_all.sort(key=lambda e:e['qpc']);filtered_bytes=''.join(json.dumps(e,sort_keys=True,separators=(',',':'))+'\n' for e in filtered_all).encode()
    summary={'revision':1,'status':'verified','event_loss':0,'allocation_lifetime_reconciled':True,'transient_positive_control':True,
             'raw_etl_sha256':digest(capture/'gpu.etl'),'process_filtered_event_sha256':hashlib.sha256(filtered_bytes).hexdigest(),
             'environment':environment,'processes':summaries,'positive_control':controls,'qpc_frequency':trace['qpc_frequency'],
             'scope':'Windows_D3D12_LLGL_process_lifetime_VidMm_usage_and_allocation_events',
             'limits':['Raw system ETL retained locally; only target-process events are versioned',
                       'Allocation lifetime sizes and residency/accounted process usage are separate series',
                       'No frame-scoped peak claim; device setup, CPU references and teardown included',
                       'No SDL/other backend continuous-memory comparison or stress/device-loss acceptance']}
    from phase1_acceptance import evaluate
    previous_folder=ROOT/'benchmarks/reports/2026-10-21-phase1-sdl'
    prior=read(previous_folder/'evidence.json')
    for item in prior['files']+[prior['source_archive'],prior['historical_baseline']]:require(digest(ROOT/item['path'])==item['sha256'],'Previous comparison evidence changed')
    historical=read(ROOT/prior['historical_baseline']['path'])
    for item in historical['files']+[historical['source_archive']]:require(digest(ROOT/item['path'])==item['sha256'],'Historical baseline changed')
    previous=previous_folder/'acceptance-evidence.json';acceptance=read(previous)
    require(evaluate(acceptance)['unmet']==['continuous_transient_gpu_memory'],'Other Phase 1 gates are not satisfied')
    acceptance['gpu_memory_trace']=summary
    acceptance['limits']=[x for x in acceptance['limits'] if 'trace missing' not in x]
    acceptance['previous_comparison_evidence']={'path':previous.relative_to(ROOT).as_posix(),'sha256':digest(previous)}
    archive=source_archive();output.mkdir(parents=True)
    (output/'process-events.jsonl').write_bytes(filtered_bytes);write(output/'memory.json',summary);write(output/'lifetimes.json',all_lifetimes)
    write(output/'acceptance-evidence.json',acceptance);write(output/'acceptance.json',evaluate(acceptance))
    native=output/'native';native.mkdir()
    for name in ('capture.json','trace.json',*[p['name']+suffix for p in processes for suffix in ('.stdout','.log')]):
        (native/name).write_bytes((capture/name).read_bytes())
    lines=['# Phase 1 GPU memory event trace acceptance','',
           'Windows D3D12 / LLGL, five complete fresh process lifetimes. TDH-decoded private DxgKrnl session, zero events/buffers lost. Process usage/commitment are change-event peaks; registered allocation sizes are a separate metric. Setup, readback and teardown are included; no frame-only or physical VRAM occupancy claim.',
           '','| Process | GPU local usage peak MiB | GPU nonlocal usage peak MiB | VidMm live allocation size peak MiB | Allocations fully paired | Destroyed in System/other context |',
           '|---|---:|---:|---:|---:|---:|']
    for r in summaries:lines.append(f"| {r['scenario']} | {r['usage_local_peak_bytes']/1048576:.6f} | {r['usage_nonlocal_peak_bytes']/1048576:.6f} | {r['vidmm_allocation_live_size_peak_bytes']/1048576:.6f} | {r['allocation_count']} | {r['system_context_destructions']} |")
    lines+=['','| Positive interval | Overlapping 32MiB+ allocations | Trace local peak MiB | All-checkpoint local maximum MiB | Peak missed by checkpoints MiB |',
            '|---|---:|---:|---:|---:|']
    for r in controls:lines.append(f"| {r['index']} | {r['overlapping_transient_allocations']} | {r['trace_local_peak_bytes']/1048576:.6f} | {r['checkpoint_local_max_bytes']/1048576:.6f} | {r['missed_by_checkpoint_bytes']/1048576:.6f} |")
    lines+=['','Raw system ETL and full-system decoded events stay local under repository out/. This report versions only target-process events and native outputs, plus hashes of every raw input. The earlier CPU/GPU timing observations remain unchanged. This trace run is correctness/memory evidence, not a new timing comparison.']
    (output/'TABLE.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    inputs=[{'path':p.relative_to(ROOT).as_posix(),'sha256':digest(p),'bytes':p.stat().st_size} for p in sorted(capture.iterdir()) if p.is_file()]
    write(output/'evidence.json',{'raw_inputs_retained_locally':inputs,'source_archive':archive,'exporter_sha256':manifest['exporter_sha256'],
          'previous_evidence':{'path':(previous_folder/'evidence.json').relative_to(ROOT).as_posix(),'sha256':digest(previous_folder/'evidence.json')},
          'report_files':[{'path':p.relative_to(output).as_posix(),'sha256':digest(p)} for p in sorted(output.rglob('*')) if p.is_file()]})
    print(f'PASS: {len(processes)} process lifetimes, {len(filtered_all)} target events, loss 0, 3 transient positive controls. Phase 1 complete: {evaluate(acceptance)["phase1_complete"]}')


if __name__=='__main__':main()
