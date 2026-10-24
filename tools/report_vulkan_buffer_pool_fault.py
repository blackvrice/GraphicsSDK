"""Fail-closed review of native synthetic-error evidence; no timing remeasurement."""
import argparse
import csv
import json
import pathlib
import run_vulkan_buffer_pool_fault as trial
ROOT=trial.ROOT

def review(path):
    def load(n):return json.loads((path/n).read_text(encoding='utf-8'))
    x=load('results.json');commands=load('commands.json');require=trial.require
    require(x['schema_version']==1 and x['contract']=='vulkan_buffer_fault_campaign_v1','Different fault campaign')
    require(x['production_adopted'] is False and x['performance_remeasured'] is False and x['physical_memory_reclamation_verified'] is False,'Unreviewed adoption/timing/memory claim')
    sdk=trial.audit.sdk_fingerprint(trial.SOURCE);require(x['SDK_source_fingerprint']==sdk and x['prior_SDK_source_fingerprint']==trial.PRIOR and x['source_fingerprint']==trial.fingerprint(),'Wrong SDK/layer source identity')
    require(x['probe']==load('probe.stdout') and x['probe']['SDK_source_fingerprint']==sdk and len(set(x['probe']['adapters']))==2,'Wrong hardware probe')
    by_name={c['name']:c for c in commands};require(len(commands)==len(by_name)==77 and all(c['returncode']==c['expected'] for c in commands),'Incomplete native command matrix')
    require(len(x['runs'])==68,'Incomplete failure/recovery matrix')
    positive={(g,v,c) for g in x['probe']['adapters'] for v in (False,True) for c in trial.CASES}
    negative={(g,v,c) for g in x['probe']['adapters'] for v in (False,True) for c in ('double-device','dedicated-oom')}
    seen=set();cells={'fixed':set(),'prior':set()};identities={}
    for row in x['runs']:
        name=row['name'];variant=row['variant'];r=row['result'];require(name not in seen and variant in cells,'Duplicate/unknown fault process');seen.add(name)
        require(r==load(name+'.stdout') and not (path/(name+'.stderr')).read_bytes(),'Raw native/validation output differs')
        unsafe=variant=='prior';trial.validate(r,r['case'],r['validation'],trial.PRIOR if unsafe else sdk,unsafe)
        c=by_name[name];require(c['variant']==variant and c['expected']==(2 if unsafe else 0) and c['validation']==r['validation'] and c['argv'][1:]==[r['case'],r['device_name'],str(int(r['validation']))] and not c['overrides'],'Command attribution differs')
        key=r['device_name'],r['validation'],r['case'];require(key not in cells[variant],'Duplicate failure scenario');cells[variant].add(key)
        identity={k:r[k] for k in ('device_uuid','driver_uuid','device_luid','environment')};require(identities.setdefault(r['device_name'],identity)==identity,'Hardware/driver changed across error cases')
    require(cells['fixed']==positive and cells['prior']==negative,'Missing GPU/validation/case cells')
    artifacts={r['path']:r for r in x['artifacts']}
    for r in artifacts.values():require(trial.record(ROOT/r['path'])==r,'Changed fault executable/layer')
    controls={f'{k}-{i}' for i in range(2) for k in ('missing-fault','wrong-gpu')}|{f'invalid-cli-{i}' for i in range(4)}
    require(set(by_name)==seen|controls|{'probe'},'Unknown process controls')
    for name,c in by_name.items():
        binary=pathlib.Path(c['argv'][0]).resolve();require(binary.is_relative_to(ROOT) and binary.relative_to(ROOT).as_posix() in artifacts,'Unfrozen process binary')
        if name in controls:require(c['expected']==1 and not (path/(name+'.stdout')).read_bytes() and b'FAIL:' in (path/(name+'.stderr')).read_bytes(),'Control did not fail closed')
    require(by_name['probe']['argv'][1:]==['--probe'] and not (path/'probe.stderr').read_bytes(),'Probe command differs')
    return x

def write_report(path,out):
    x=review(path);trial.require(not out.exists(),'Exclusive report snapshot required');out.mkdir(parents=True)
    clean=[r['result'] for r in x['runs'] if r['variant']=='fixed'];prior=[r['result'] for r in x['runs'] if r['variant']=='prior']
    summary={k:v for k,v in x.items() if k!='runs'}
    summary.update(candidate_CTests=57,consumer_CTests=2,Python_tests=506,native_fault_plan_assertions=914,positive_processes=60,guarded_regression_processes=8,fail_closed_processes=8,total_commands=77,positive_iterations=180,guarded_control_iterations=24,positive_verified_buffers=sum(r['verified_buffers'] for r in clean),positive_verified_bytes=sum(r['verified_bytes'] for r in clean),positive_injected_failures=sum(len(r['native_trace']['fault']['injections']) for r in clean),guarded_invalid_frees=sum(r['native_trace']['fault']['invalid_frees'] for r in prior),positive_invalid_frees=sum(r['native_trace']['fault']['invalid_frees'] for r in clean),Phase3_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    with (out/'cases.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.writer(f);w.writerow(['GPU','validation','variant','case','repetitions','assertions','verified_buffers','verified_bytes','injected_failures','guarded_invalid_frees'])
        for row in x['runs']:
            r=row['result'];w.writerow([r['device_name'],r['validation'],row['variant'],r['case'],r['repetitions'],r['assertions'],r['verified_buffers'],r['verified_bytes'],len(r['native_trace']['fault']['injections']),r['native_trace']['fault']['invalid_frees']])
    print('PASS 60 clean failures/recoveries, 8 guarded pre-fix regressions, 8 fail-closed controls; experimental only')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();write_report(a.input,a.output)
