"""Recheck phase conservation and raw process attribution before reporting native counts."""
import argparse
import csv
import json
import pathlib
import run_vulkan_allocation_trace as u

def review(path):
    x=json.loads((path/'results.json').read_text(encoding='utf-8'));commands=json.loads((path/'commands.json').read_text(encoding='utf-8'))
    u.require(x['schema_version']==1 and x['contract']==u.CONTRACT and x['pilot'] is False and x['timings_excluded'] is True,'Different full diagnostic profile')
    for k in ('physical_memory_reclamation_verified','native_driver_internal_allocations_verified','production_SDK_changed'):u.require(x[k] is False,'Unreviewed scope claim')
    u.require(x['source_fingerprint']==u.fingerprint(),'Changed native diagnostic sources')
    u.require(len(commands)==50 and all(c['returncode']==c['expected'] for c in commands),'Incomplete command matrix/controls')
    by_name={c['name']:c for c in commands};u.require(len(by_name)==len(commands),'Duplicate command record')
    cells=set();names=set()
    for run in x['runs']:
        r=run['result'];name=run['name'];u.require(name not in names,'Duplicate process');names.add(name)
        raw=u.base.parse_output((path/(name+'.stdout')).read_bytes(),(path/(name+'.stderr')).read_bytes());u.require(raw==r,'Raw/normalized trace differs')
        p,s,c,v=r['preference'],r['sdk'],r['scenario'],r['validation'];u.require(p in x['probes'] and s in ('graphicssdk','sdl') and c in u.base.SHAPES and type(v) is bool,'Unexpected trace cell')
        cell=(p,s,c,v);u.require(cell not in cells,'Duplicate matrix cell');cells.add(cell)
        cmd=by_name[name];u.require(cmd['argv'][1:]==[s,c,p,'1','3',str(int(v)),r['device_name']] and cmd['validation']==v and cmd['expected']==0,'Command attribution differs')
        u.validate(r,s,c,p,v,x['probes'][p],x['source_fingerprint'],x['SDK_source_fingerprint'],x['SDL_commit'])
    u.require(cells=={(p,s,c,v) for p in ('high','low') for s in ('graphicssdk','sdl') for c in u.base.SHAPES for v in (True,False)},'Incomplete validation and unvalidated trace matrix')
    for k in ('created_resources','released_resource_handles','used_resources','verified_resources','verified_images','verified_buffers','verified_bytes'):u.require(x[k]==sum(r['result'][k] for r in x['runs']),'Wrong verification totals')
    controls={k+'-'+s for k in ('missing-trace','wrong-trace-path','invalid-phase','missing-vvl','wrong-gpu') for s in ('graphicssdk','sdl')}|{'invalid-option-'+str(i) for i in range(6)}
    u.require(set(by_name)==names|controls|{'probe-high','probe-low'},'Unexpected commands')
    artifacts={a['path']:a for a in x['artifacts']}
    for a in x['artifacts']:u.require(u.record(u.ROOT/a['path'])==a,'Changed diagnostic binary/manifest/baseline')
    for name,cmd in by_name.items():
        suffix='/vulkan_allocation.exe' if name.startswith('probe-') else '/vulkan_allocation_trace.exe'
        matches=[u.ROOT/p for p in artifacts if p.endswith(suffix)];u.require(len(matches)==1,'Unidentified command executable');expected=matches[0]
        u.require(pathlib.Path(cmd['argv'][0]).resolve()==expected,'Different executable in command')
        if name in controls:u.require(cmd['expected']==1 and not (path/(name+'.stdout')).read_bytes() and b'FAIL:' in (path/(name+'.stderr')).read_bytes(),'Negative control did not fail closed')
    import audit_licenses as audit
    u.require(audit.sdk_fingerprint(u.ROOT)==x['SDK_source_fingerprint'],'SDK production sources changed')
    for p in ('high','low'):
        probe=u.base.parse_output((path/f'probe-{p}.stdout').read_bytes(),(path/f'probe-{p}.stderr').read_bytes());u.base.validate_identity(probe)
        u.require(probe==x['probes'][p] and by_name['probe-'+p]['argv'][1:]==['--probe','sdl',p],'Probe attribution differs')
        u.require(probe['source_fingerprint']==u.base.fingerprint() and probe['SDK_source_fingerprint']==x['SDK_source_fingerprint'] and probe['SDL_commit']==x['SDL_commit'],'Stale baseline identity probe')
        for s in ('graphicssdk','sdl'):
            text=(path/f'trace-1-{p}-{s}-buffer1.stderr').read_text(encoding='utf-8')
            u.require('Inserted device layer "VK_LAYER_GFX_allocation_trace"' in text and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in text,'Required layer chain not observed')
    return x

def rows(x):
    data=[]
    for run in x['runs']:
        r=run['result']
        for f in r['native_trace']['frames']:
            c=f['counts'];data.append(dict(process=run['name'],sdk=r['sdk'],device_name=r['device_name'],device_uuid=r['device_uuid'],scenario=r['scenario'],validation=r['validation'],iteration=f['iteration'],phase=u.PHASES[f['phase']],**c['successes'],allocated_bytes=c['allocated_bytes'],freed_bytes=c['freed_bytes'],**{'after_'+k:v for k,v in f['after'].items()}))
    return data

def write_report(path,out):
    x=review(path);u.require(not out.exists(),'Exclusive report output required');out.mkdir(parents=True)
    summary={k:v for k,v in x.items() if k!='runs'};summary.update(profile_processes=32,scoped_records=672,CPU_CTests=2,Python_tests=417,performance_comparison=False,Phase3_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    data=rows(x)
    with (out/'native-counts.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
    print('PASS: 32 profile processes, 672 phase records, native object/byte conservation; no performance statistics')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();write_report(a.input,a.output)
