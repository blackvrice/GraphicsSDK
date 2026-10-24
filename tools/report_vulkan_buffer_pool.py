"""Validate fresh trials and report the limited candidate's measured benefits and costs."""
import argparse
import csv
import json
import pathlib
import run_vulkan_buffer_pool as u

def review(path):
    x=json.loads((path/'results.json').read_text(encoding='utf-8'));commands=json.loads((path/'commands.json').read_text(encoding='utf-8'));b=u.b
    b.require(x['schema_version']==1 and x['contract']=='vulkan_small_buffer_slab_candidate_trial_v1' and x['candidate_only'] is True,'Different candidate trial')
    b.require(x['production_adopted'] is False and x['physical_memory_reclamation_verified'] is False and x['GPU_timestamps'] is False and x['samples_removed']==0,'Unreviewed promotion/memory/timing claim')
    ids=u.source_ids();b.require(x['product_SDK_source_fingerprint']==ids['baseline'] and x['candidate_SDK_source_fingerprint']==ids['candidate'] and x['benchmark_source_fingerprint']==b.fingerprint() and x['trace_source_fingerprint']==u.t.fingerprint(),'Stale executable source identities')
    b.require(x['repetitions']==3 and x['warmup']==5 and x['samples']==30 and x['seed']==20261029,'Different bounded measurement contract')
    by_name={c['name']:c for c in commands};b.require(len(commands)==70 and len(by_name)==70 and all(c['returncode']==c['expected'] for c in commands),'Incomplete command matrix/controls')
    names=set();cells={};trace_cells=set();correctness=set()
    for run in x['runs']+x['traces']:
        name=run['name'];r=run['result'];variant=run['variant'];b.require(name not in names and variant in u.VARIANTS,'Duplicate or unknown process');names.add(name)
        raw=b.parse_output((path/(name+'.stdout')).read_bytes(),(path/(name+'.stderr')).read_bytes());b.require(raw==r,'Raw native output differs')
        is_trace='kind' not in run;validation=True if is_trace else run['kind']=='correctness';pref=r['preference'];scenario=r['scenario'];b.require(pref in x['probes'] and scenario in u.WORK,'Unknown GPU/workload')
        c=by_name[name];sdk='sdl' if variant=='sdl' else 'graphicssdk';w,n=(1,3) if validation else (5,30)
        b.require(c['argv'][1:]==[sdk,scenario,pref,str(w),str(n),str(int(validation)),r['device_name']] and c['variant']==variant and c['validation']==validation and c['trace']==is_trace and c['expected']==0,'Command attribution differs')
        if is_trace:
            b.require(variant in ('baseline','candidate'),'SDL trace not part of trial');u.validate_trace(r,variant,scenario,pref,x['probes'][pref],ids);trace_cells.add((pref,variant,scenario))
        else:
            b.require(run['kind'] in ('correctness','measurement'),'Unknown run kind');u.validate_result(r,variant,scenario,pref,validation,x['probes'][pref],ids)
            if validation:correctness.add((pref,variant,scenario))
            else:cells.setdefault((pref,variant,scenario),[]).append(r)
    expected={(p,v,s) for p in ('high','low') for v in u.VARIANTS for s in u.WORK}
    b.require(len(x['runs'])==48 and len(x['traces'])==8 and correctness==expected and set(cells)==expected and trace_cells=={(p,v,s) for p in ('high','low') for v in ('baseline','candidate') for s in u.WORK},'Incomplete independent native matrices')
    b.require(len(x['groups'])==4 and len({(g['preference'],g['scenario']) for g in x['groups']})==4,'Incomplete summaries')
    for row in x['groups']:
        pref,scenario=row['preference'],row['scenario'];b.require(row['device_name']==x['probes'][pref]['device_name'],'Summary GPU differs')
        for variant in u.VARIANTS:
            data=cells[pref,variant,scenario];b.require(len(data)==3 and b.summarize(data)==row['statistics'][variant],'Independent runs/statistics differ')
        b.require(row['candidate_over_baseline_p50_ratio']=={c:row['statistics']['candidate'][c]['p50_ms']/row['statistics']['baseline'][c]['p50_ms'] for c in b.COLUMNS},'Changed comparison ratios')
    for k in ('created_resources','released_resource_handles','verified_resources','verified_buffers','verified_bytes'):b.require(x[k]==sum(r['result'][k] for r in x['runs']),'Incorrect benchmark verification totals')
    artifacts={a['path']:a for a in x['artifacts']}
    for a in artifacts.values():b.require(b.record(u.ROOT/a['path'])==a,'Changed trial executable/runtime')
    controls={k+'-'+v for k in ('missing-layer','wrong-gpu') for v in u.VARIANTS}|{'invalid-option-'+str(i) for i in range(6)}
    b.require(set(by_name)==names|controls|{'probe-high','probe-low'},'Unexpected commands')
    for name,c in by_name.items():
        actual=pathlib.Path(c['argv'][0]).resolve();b.require(actual.is_relative_to(u.ROOT) and actual.relative_to(u.ROOT).as_posix() in artifacts,'Unfrozen executable')
        if name in controls:b.require(c['expected']==1 and not (path/(name+'.stdout')).read_bytes() and b'FAIL:' in (path/(name+'.stderr')).read_bytes(),'Negative control did not fail closed')
    for pref,r in x['probes'].items():
        b.validate_identity(r);b.require(json.loads((path/f'probe-{pref}.stdout').read_bytes())==r and r['SDK_source_fingerprint']==ids['baseline'],'Probe identity differs')
    return x

def write_report(path,out):
    x=review(path);u.b.require(not out.exists(),'Exclusive report snapshot required');out.mkdir(parents=True)
    summary={k:v for k,v in x.items() if k not in ('runs','traces')};summary.update(candidate_CTests=57,Python_tests=444,measurement_processes=36,correctness_processes=12,native_trace_processes=8,measurement_iterations=1080,CPU_interval_observations=2160,trace_buffers=sum(r['result']['verified_buffers'] for r in x['traces']),trace_bytes=sum(r['result']['verified_bytes'] for r in x['traces']),native_allocation_calls_1000_baseline=1000,native_allocation_calls_1000_candidate=63,requested_bytes_1000_baseline=4096000,requested_bytes_1000_candidate=4128768,requested_bytes_1_baseline=4096,requested_bytes_1_candidate=65536,overall_SDK_ranking=False,Phase3_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    with (out/'metrics.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.writer(f);w.writerow(['GPU','scenario','variant','CPU_interval','processes','samples','p50_ms','p95_ms','p99_ms','MAD_ms','min_ms','max_ms','process_medians_ms'])
        for row in x['groups']:
            for variant,channels in row['statistics'].items():
                for c,s in channels.items():w.writerow([row['device_name'],row['scenario'],variant,c,*[s[k] for k in ('processes','samples','p50_ms','p95_ms','p99_ms','MAD_ms','min_ms','max_ms')],json.dumps(s['process_medians_ms'])])
    print('PASS fresh candidate/baseline/SDL CPU intervals and separate 63-block native proof; experimental only')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();write_report(a.input,a.output)
