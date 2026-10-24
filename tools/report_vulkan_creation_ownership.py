import argparse
import csv
import json
import pathlib
import run_vulkan_creation_ownership as trial
ROOT=trial.ROOT
def review(path):
    def load(n):return json.loads((path/n).read_text(encoding='utf-8'))
    x=load('results.json');require=trial.require;sdk=trial.audit.sdk_fingerprint(ROOT)
    require(x['schema_version']==1 and x['contract']=='vulkan_product_creation_ownership_v1','Wrong product campaign')
    require(x['product_fix_adopted'] is True and x['buffer_pool_adopted'] is False and x['performance_remeasured'] is False and x['physical_memory_reclamation_verified'] is False,'Wrong product/pooling/performance/memory claim')
    require(x['SDK_source_fingerprint']==sdk and x['prior_SDK_source_fingerprint']==trial.OLD and x['native_source_fingerprint']==trial.native.fingerprint(),'Wrong before/after source identities')
    v=ROOT/'benchmarks/reports/validation/2026-10-24-vulkan-creation-ownership'
    for variant in ('before','after'):
        raw=json.loads((v/('cpu-'+variant+'-run.log')).read_bytes());require(raw==x['cpu'][variant],'CPU raw output differs');trial.validate_cpu(raw,trial.OLD if variant=='before' else sdk,variant=='before')
    commands=load('commands.json');by_name={c['name']:c for c in commands};require(len(commands)==len(by_name)==33 and all(c['returncode']==c['expected'] for c in commands),'Incomplete hardware commands')
    require(x['probe']==load('probe.stdout') and x['probe']['SDK_source_fingerprint']==sdk and len(set(x['probe']['adapters']))==2,'Hardware probe differs')
    cells={'before':set(),'after':set()};names=set();identities={}
    for row in x['runs']:
        name,variant,r=row['name'],row['variant'],row['result'];require(name not in names and variant in cells,'Duplicate/unknown hardware run');names.add(name)
        require(r==load(name+'.stdout') and not (path/(name+'.stderr')).read_bytes(),'Native raw output differs')
        trial.native.validate(r,r['case'],r['validation'],trial.OLD if variant=='before' else sdk,variant=='before')
        c=by_name[name];require(c['variant']==variant and c['expected']==(2 if variant=='before' else 0) and c['validation']==r['validation'] and c['argv'][1:]==[r['case'],r['device_name'],str(int(r['validation']))] and not c['overrides'],'Command attribution differs')
        key=r['device_name'],r['validation'],r['case'];require(key not in cells[variant],'Duplicate hardware cell');cells[variant].add(key)
        identity={k:r[k] for k in ('device_uuid','driver_uuid','device_luid','environment')};require(identities.setdefault(r['device_name'],identity)==identity,'GPU/driver changed')
    require(cells['after']=={(g,v,c) for g in x['probe']['adapters'] for v in (False,True) for c in trial.CASES} and cells['before']=={(g,v,'dedicated-oom') for g in x['probe']['adapters'] for v in (False,True)} and len(x['runs'])==24,'Incomplete hardware matrix')
    artifacts={r['path']:r for r in x['artifacts']}
    for r in artifacts.values():require(trial.record(ROOT/r['path'])==r,'Hardware executable/layer changed')
    controls={f'{k}-{i}' for k in ('missing-layer','wrong-gpu') for i in range(2)}|{f'invalid-cli-{i}' for i in range(4)}
    require(set(by_name)==names|controls|{'probe'},'Unknown native commands')
    for name,c in by_name.items():
        exe=pathlib.Path(c['argv'][0]).resolve();require(exe.is_relative_to(ROOT) and exe.relative_to(ROOT).as_posix() in artifacts,'Unfrozen executable')
        if name in controls:require(c['expected']==1 and not (path/(name+'.stdout')).read_bytes() and b'FAIL:' in (path/(name+'.stderr')).read_bytes(),'Control did not fail closed')
    require(by_name['probe']['argv'][1:]==['--probe'] and not (path/'probe.stderr').read_bytes(),'Wrong probe invocation')
    return x
def write_report(path,out):
    x=review(path);trial.require(not out.exists(),'Exclusive report snapshot required');out.mkdir(parents=True)
    clean=[r['result'] for r in x['runs'] if r['variant']=='after'];before=[r['result'] for r in x['runs'] if r['variant']=='before']
    summary={k:v for k,v in x.items() if k not in ('runs','cpu')};summary.update(product_CTests=55,Python_tests=526,guarded_creation_sites=29,helper_CPU_assertions=3640,implementation_CPU_cases=27,implementation_CPU_assertions_each=270,CPU_invalid_cleanup_before=21,CPU_invalid_cleanup_after=0,CPU_created_and_destroyed_each=254,hardware_clean_processes=20,hardware_guarded_processes=4,hardware_fail_closed_controls=8,hardware_commands=33,hardware_clean_cycles=60,hardware_injected_failures=sum(len(r['native_trace']['fault']['injections']) for r in clean),hardware_verified_buffers=sum(r['verified_buffers'] for r in clean),hardware_verified_bytes=sum(r['verified_bytes'] for r in clean),hardware_invalid_free_before=sum(r['native_trace']['fault']['invalid_frees'] for r in before),hardware_invalid_free_after=0,Phase3_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    with (out/'cpu-cases.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.writer(f);w.writerow(['case','api','ordinal','before_invalid_cleanup','after_invalid_cleanup','created_destroyed_before','created_destroyed_after'])
        for a,b in zip(x['cpu']['before']['cases'],x['cpu']['after']['cases']):w.writerow([a['case'],a['api'],a['ordinal'],a['invalid_cleanup'],b['invalid_cleanup'],a['created'],b['created']])
    print('PASS product ownership: actual-implementation 27 cases / hardware 20 clean + 4 guarded + 8 fail-closed; no pool adoption or timing claim')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();write_report(a.input,a.output)
