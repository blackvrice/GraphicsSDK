"""Revalidate raw Vulkan PSO CPU API results and write a narrowly scoped baseline report."""
import argparse
import csv
import json
import pathlib
import run_vulkan_pso as u

def review(path):
    x=json.loads((path/'results.json').read_text());commands=json.loads((path/'commands.json').read_text())
    u.require(len(commands)==2+8+8*x['repetitions']+10 and all(c['returncode']==c['expected'] for c in commands),'Incomplete process matrix/controls')
    u.require(x['api']=='vulkan' and x['category']=='cpu_api' and x['contract']==u.CONTRACT and x['validation_timings_excluded'] is True,'Different comparison contract')
    u.require(x['source_fingerprint']==u.fingerprint(),'Report executable sources changed')
    u.require(x['samples_removed']==0 and x['GPU_timestamp_measurement'] is False and x['overall_SDK_ranking'] is False and x['identity_native_handle_verified'] is False and x['native_pipeline_count_verified'] is False and x['actual_memory_reclamation_verified'] is False and x['driver_cache_state_known'] is False,'Unreviewed scope claim')
    names=set();grouped={};correctness=set();images=verified=0
    by_name={c['name']:c for c in commands};u.require(len(by_name)==len(commands),'Duplicate command record')
    for run in x['runs']:
        name=run['name'];r=run['result'];u.require(name not in names,'Duplicate process record');names.add(name)
        raw=u.parse_output((path/(name+'.stdout')).read_bytes(),(path/(name+'.stderr')).read_bytes());u.require(raw==r,'Raw stdout differs from normalized result')
        validation=run['kind']=='correctness';u.require(run['kind'] in ('correctness','measurement'),'Unknown run category')
        u.require(r['sdk'] in ('graphicssdk','sdl') and r['preference'] in ('high','low'),'Unknown SDK/device preference')
        w,n=(1,3) if validation else (x['warmup'],x['samples_per_process'])
        command=by_name[name]
        u.require(command['argv'][1:]==[r['sdk'],r['scenario'],r['preference'],str(w),str(n),str(int(validation)),r['device_name']] and command['validation']==validation and command['expected']==0,'Command/result attribution differs')
        u.require(command['seconds']>=sum(sum(row) for row in r['samples_ms'])/1000,'Sample intervals exceed enclosing process duration')
        u.validate_result(r,r['sdk'],r['scenario'],r['preference'],w,n,validation,x['probes'][r['preference']],x['source_fingerprint'],x['SDK_source_fingerprint'],x['SDL_commit'])
        if validation:
            key=(r['preference'],r['sdk'],r['scenario']);u.require(key not in correctness,'Duplicate correctness cell');correctness.add(key)
        if not validation:grouped.setdefault((r['preference'],r['scenario'],r['sdk']),[]).append(r)
        images+=r['verified_images'];verified+=r['verified_bytes']
    u.require(len(x['runs'])==8+8*x['repetitions'] and images==x['verified_images'] and verified==x['verified_bytes'],'Wrong verification totals')
    u.require(len(grouped)==8 and len(x['groups'])==4,'Incomplete comparison cells')
    u.require(correctness=={(p,s,c) for p in ('high','low') for s in ('graphicssdk','sdl') for c in u.SHAPES},'Incomplete required-validation matrix')
    u.require(len({(r['preference'],r['scenario']) for r in x['groups']})==4,'Duplicate summary row')
    for row in x['groups']:
        for sdk in ('graphicssdk','sdl'):
            data=grouped[(row['preference'],row['scenario'],sdk)];u.require(len(data)==x['repetitions'],'Insufficient independent process runs')
            u.require(u.summarize(data)==row['statistics'][sdk],'Changed summary statistics')
        u.require(row['GraphicsSDK_over_SDL_p50_ratio']=={k:row['statistics']['graphicssdk'][k]['p50_ms']/row['statistics']['sdl'][k]['p50_ms'] if row['statistics']['sdl'][k]['p50_ms'] else None for k in u.COLUMNS},'Changed ratio')
    u.require(u.record(u.ROOT/x['binary']['path'])==x['binary'],'Executable changed')
    import audit_licenses as a
    u.require(x['SDK_source_fingerprint']==a.sdk_fingerprint(u.ROOT),'Production SDK changed')
    u.require(set(x['probes'])=={'high','low'} and x['probes']['high']['device_uuid']!=x['probes']['low']['device_uuid'],'Probe coverage differs')
    for pref,r in x['probes'].items():
        name='probe-'+pref;u.validate_identity(r)
        u.require(json.loads((path/(name+'.stdout')).read_bytes())==r and by_name[name]['argv'][1:]==['--probe','sdl',pref],'Probe attribution differs')
        u.require(r['source_fingerprint']==x['source_fingerprint'] and r['SDK_source_fingerprint']==x['SDK_source_fingerprint'] and r['SDL_commit']==x['SDL_commit'],'Stale probe')
    controls={'missing-layer-'+sdk for sdk in ('sdl','graphicssdk')}|{'wrong-gpu-'+sdk for sdk in ('sdl','graphicssdk')}|{'invalid-option-'+str(i) for i in range(6)}
    u.require(set(by_name)==names|controls|{'probe-high','probe-low'},'Unexpected command matrix')
    for name,cmd in by_name.items():
        u.require(pathlib.Path(cmd['argv'][0]).resolve()==u.ROOT/x['binary']['path'],'Different executable in command')
        if name in controls:
            u.require(cmd['expected']==1 and not (path/(name+'.stdout')).read_bytes() and b'FAIL:' in (path/(name+'.stderr')).read_bytes(),'Control did not fail closed')
        else:
            u.require(cmd['expected']==0,'Unexpected success status')
            error=(path/(name+'.stderr')).read_bytes();u.require(b'Validation Error' not in error and b'SYNC-HAZARD' not in error,'Native validation error')
    return x

def write_report(path,out):
    x=review(path);u.require(not out.exists(),'Exclusive report snapshot required');out.mkdir(parents=True)
    summary={k:v for k,v in x.items() if k!='runs'}
    summary.update(production_SDK_changed=False,public_API_changed=False,SDK_rebuilt=False,SDL_rebuilt=False,CTests=3,Python_tests=339,correctness_GPU_processes=8,measurement_GPU_processes=8*x['repetitions'],measurement_iterations=8*x['repetitions']*x['samples_per_process'],CPU_interval_observations=16*x['repetitions']*x['samples_per_process'],full_SDK_CTests_rerun=False,
        prior_93_selected_CTests='Preserved historical evidence; not rerun for unchanged SDK',Linux_native_verified=False,Phase3_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False)
    summary.update({k:sum(r['result'][k] for r in x['runs']) for k in ('created_pipelines','released_pipeline_handles','used_pipeline_regions')})
    with (out/'metrics.csv').open('w',newline='') as f:
        w=csv.writer(f);w.writerow(['gpu','scenario','sdk','CPU_interval','processes','samples','p50_ms','p95_ms','p99_ms','MAD_ms','min_ms','max_ms','process_medians_ms'])
        for row in x['groups']:
            for sdk,channels in row['statistics'].items():
                for name,s in channels.items():w.writerow([row['device_name'],row['scenario'],sdk,name,*[s[k] for k in ('processes','samples','p50_ms','p95_ms','p99_ms','MAD_ms','min_ms','max_ms')],json.dumps(s['process_medians_ms'])])
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f"PASS: four PSO comparison cells / {x['verified_images']} full images; creation and public release remain separate CPU intervals")

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);x=p.parse_args();write_report(x.input,x.output)
