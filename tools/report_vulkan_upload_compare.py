"""Reconcile every comparison result with raw output, command and separate timing scope."""
import argparse
import csv
import hashlib
import json
import pathlib
import compare_vulkan_upload as c
import run_vulkan_upload as u
import audit_licenses as a
import vulkan_upload_experiment as experiment

def profile_fingerprint(root=u.ROOT):
    names=sorted(['benchmarks/vulkan_upload_profile/main.cpp','benchmarks/vulkan_upload_profile/CMakeLists.txt','benchmarks/vulkan_upload/Identity.hpp','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp'])
    return hashlib.sha256(''.join(f'{p}|{hashlib.sha256((root/p).read_bytes()).hexdigest()}\n' for p in names).encode()).hexdigest()

def review(path):
    x=json.loads((path/'results.json').read_bytes());commands=json.loads((path/'commands.json').read_bytes())
    u.require(len(commands)==84 and all(r['returncode']==0 for r in commands),'Incomplete commands')
    by_name={r['name']:r for r in commands};u.require(len(by_name)==84,'Duplicate command')
    u.require(x['api']=='vulkan' and x['category']=='end_to_end' and x['contract']==u.CONTRACT,'Wrong scope')
    receipt,_=experiment.candidate()
    u.require(x['benchmark_source_fingerprint']==u.fingerprint() and x['after_SDK_source_fingerprint']==receipt['SDK_source_fingerprint'],'Comparison sources changed')
    baseline=json.loads((u.ROOT/'benchmarks/reports/2026-10-23-phase3-vulkan-upload/summary.json').read_bytes())
    u.require(x['before_SDK_source_fingerprint']==baseline['SDK_source_fingerprint'],'Historical SDK changed')
    u.require(a.sdk_fingerprint(u.ROOT)==baseline['SDK_source_fingerprint'],'Production SDK must exactly match baseline after rejecting candidate')
    u.require(x['SDL_commit']==baseline['SDL_commit'] and x['samples_removed']==0 and x['overall_SDK_ranking'] is False and x['validation_timings_excluded'] is True and x['diagnostic_timings_excluded'] is True,'Wrong evidence scope')
    for b in x['binaries'].values():u.require(u.record(u.ROOT/b['path'])==b,'Frozen executable changed')
    seen=set();correct=set();grouped={};images=total=0
    def raw(name,r,program,argv,validation):
        u.require(name not in seen,'Duplicate run');seen.add(name);cmd=by_name[name]
        u.require(json.loads((path/(name+'.stdout')).read_bytes())==r,'Raw result changed')
        u.require(cmd['program']==program and cmd['validation']==validation and pathlib.Path(cmd['argv'][0]).resolve()==(u.ROOT/x['binaries'][program]['path']).resolve() and cmd['argv'][1:]==argv,'Command/result mismatch')
        err=(path/(name+'.stderr')).read_bytes();u.require(b'Validation Error' not in err and b'SYNC-HAZARD' not in err,'Native validation error')
        return cmd
    for pref,p in x['probes'].items():
        u.validate_identity(p);raw('probe-'+pref,p,'before',['--probe','sdl',pref],False)
        u.require(p['source_fingerprint']==x['benchmark_source_fingerprint'] and p['SDK_source_fingerprint']==x['before_SDK_source_fingerprint'],'Probe executable attribution changed')
    u.require(set(x['probes'])=={'high','low'} and x['probes']['high']['device_uuid']!=x['probes']['low']['device_uuid'],'Adapter coverage changed')
    for run in x['runs']:
        r=run['result'];name=run['name'];validation=run['kind']=='correctness';variant=run['variant']
        u.require(run['kind'] in ('correctness','measurement') and variant in ('before','after','sdl'),'Unknown category')
        u.require((validation and variant=='after') or (not validation and r['sdk']==('sdl' if variant=='sdl' else 'graphicssdk')),'Wrong variant attribution')
        program='after' if variant=='after' else 'before';w,n=(1,3) if validation else (5,30)
        cmd=raw(name,r,program,[r['sdk'],r['scenario'],r['preference'],str(w),str(n),str(int(validation)),r['device_name']],validation)
        u.require(cmd['seconds']>=sum(r['samples_ms'])/1000,'Samples exceed process duration')
        u.validate_result(r,r['sdk'],r['scenario'],r['preference'],w,n,validation,x['probes'][r['preference']],x['benchmark_source_fingerprint'],x['after_SDK_source_fingerprint'] if variant=='after' else x['before_SDK_source_fingerprint'],x['SDL_commit'])
        if validation:
            key=(r['preference'],r['sdk'],r['scenario']);u.require(key not in correct,'Repeated correctness cell');correct.add(key)
        else:grouped.setdefault((r['preference'],r['scenario'],variant),[]).append(r)
        images+=r['verified_images'];total+=r['verified_bytes']
    u.require(correct=={(p,s,k) for p in ('high','low') for s in ('graphicssdk','sdl') for k in u.SHAPES},'Incomplete correctness matrix')
    cells={(p,k) for p in ('high','low') for k in u.SHAPES if k!='odd'}
    u.require(len(x['runs'])==70 and len(grouped)==18 and len(x['groups'])==6 and {(r['preference'],r['scenario']) for r in x['groups']}==cells,'Incomplete measurement matrix')
    for row in x['groups']:
        for variant in ('before','after','sdl'):
            rr=grouped[(row['preference'],row['scenario'],variant)]
            u.require(len(rr)==3 and row['statistics'][variant]==u.summarize(rr),'Wrong pooled/process statistics')
        u.require(row['cost_change_percent']==100*(row['statistics']['after']['p50_ms']/row['statistics']['before']['p50_ms']-1),'Wrong cost change')
    profiles=set();pi=pb=0
    for run in x['profiles']:
        r=run['result'];pref=run['preference'];variant=run['variant'];key=(pref,variant,r['scenario'])
        u.require(key not in profiles,'Duplicate profile');profiles.add(key)
        raw(run['name'],r,'profile-'+variant,[x['probes'][pref]['device_name'],r['scenario'],'3','15','0'],False)
        c.validate_profile(r,x['probes'][pref],x[variant+'_SDK_source_fingerprint'])
        u.require(r['profile_source_fingerprint']==profile_fingerprint(),'Diagnostic sources changed')
        pi+=r['verified_images'];pb+=r['verified_bytes']
    u.require(profiles=={(p,v,k) for p,k in cells for v in ('before','after')} and len(seen)==84,'Incomplete profile/command matrix')
    u.require((images,total)==(1954,69656525120)==(x['verified_images'],x['verified_bytes']) and (pi,pb)==(216,7763558400)==(x['profile_verified_images'],x['profile_verified_bytes']) and x['performance_samples']==1620,'Verification counts differ')
    return x

def write_report(path,out):
    x=review(path);u.require(not out.exists(),'Exclusive report snapshot required');out.mkdir(parents=True)
    summary={k:v for k,v in x.items() if k not in ('runs','profiles')}
    summary.update(profile_source_fingerprint=profile_fingerprint(),production_SDK_changed=False,SDK_source_fingerprint=x['before_SDK_source_fingerprint'],cached_candidate_adopted=False,candidate_selected_CTests=96,candidate_integrated=55,candidate_Vulkan_only=30,candidate_backends_off=11,memory_tests_excluded=3,candidate_installed_consumer_suites=9,candidate_CPU_memory_policy_assertions=57,Python_tests=252,Phase3_complete=False,Linux_native_verified=False,memory_acceptance=False,Intel_AV_cause_resolved=False,persistent_mapping_trial_adopted=False)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    with (out/'metrics.csv').open('w',newline='') as f:
        w=csv.writer(f);w.writerow(['gpu','scenario','variant','processes','samples','p50_ms','p95_ms','p99_ms','MAD_ms','min_ms','max_ms','process_medians_ms'])
        for row in x['groups']:
            for variant,s in row['statistics'].items():w.writerow([row['device_name'],row['scenario'],variant,*[s[k] for k in ('processes','samples','p50_ms','p95_ms','p99_ms','MAD_ms','min_ms','max_ms')],json.dumps(s['process_medians_ms'])])
    print('PASS: 84 raw commands reconciled; 1620 primary samples; CPU diagnostics excluded')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);x=p.parse_args();write_report(x.input,x.output)
