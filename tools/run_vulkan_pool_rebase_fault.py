"""Fresh full allocator fault matrix and mixed-context exact fallback on the rebased SDK."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import struct
import subprocess
import audit_licenses as audit
import run_vulkan_buffer_pool_fault as regression
import run_vulkan_pool_churn as churn
ROOT=churn.ROOT
SDK=churn.SDK
CANDIDATE=churn.CANDIDATE
SOURCE=churn.SOURCE
require=churn.require
same=churn.same
MIXED={
 'mixed-host':(-1,0,0,True,False,False), 'mixed-device':(-2,0,0,True,False,False),
 'mixed-double':(-1,-2,0,False,False,False), 'mixed-unknown':(-13,0,0,False,False,False),
 'held-fallback-bind':(-2,0,-1,False,False,True),
 'reuse-then-host':(-1,0,0,True,True,False), 'reuse-then-device':(-2,0,0,True,True,False),
}
FILES=('experiments/vulkan_pool_rebase_fault/main.cpp','experiments/vulkan_pool_rebase_fault/CMakeLists.txt','experiments/vulkan_pool_churn/Layer.cpp','experiments/vulkan_pool_churn/Snapshot.hpp','experiments/vulkan_buffer_pool_fault/Layer.cpp','experiments/vulkan_buffer_pool_fault/Accounting.hpp','experiments/vulkan_buffer_pool_fault/FaultPlan.hpp','experiments/vulkan_buffer_pool_fault/Client.hpp','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/Identity.hpp')
def fingerprint():return hashlib.sha256(''.join(f'{name}|{audit.sha256((ROOT/name).read_bytes())}\n' for name in sorted(FILES)).encode()).hexdigest()
def record(p):return audit.record_file(ROOT,p)
def identity(r,validation):
    require(bool(r['loaded_validation_layer'])==validation,'Validation mode')
    for key,size in (('device_uuid',32),('driver_uuid',32),('device_luid',16)):require(type(r[key]) is str and re.fullmatch('[0-9a-f]{'+str(size)+'}',r[key]) and int(r[key],16)!=0,'Native identity')
    e=r['environment'];require(e['gpu']==r['device_name'] and e['software_renderer'] is False and e['driver_query_available'] is True and e['pointer_bits']==64,'Hardware environment')
    hi,lo=map(int,e['luid'].split(':'));require(struct.pack('<II',lo,hi&0xffffffff).hex()==r['device_luid'],'Native/DXGI identity')
def snapshots(s):
    require(set(s)=={'memories','bytes','buffers','images','bindings'} and all(churn.integer(s[k]) for k in ('memories','bytes','buffers','images')),'Snapshot counters')
    rows=s['bindings'];require(len(rows)==s['buffers'] and s['images']==0,'Complete buffer binding set');seen={};memories={}
    for e in rows:
        require(set(e)=={'buffer','memory','offset','required','alignment','allocation','type','type_bits','dedicated'} and all(churn.integer(v) for v in e.values()),'Binding schema/types')
        require(e['buffer'] and e['memory'] and e['buffer'] not in seen and e['required'] and e['alignment'] and e['offset']%e['alignment']==0 and e['offset']<e['allocation'] and e['required']<=e['allocation']-e['offset'] and e['type']<32 and e['type_bits']&(1<<e['type']) and e['dedicated'] in (0,1),'Valid binding range/type')
        if e['memory'] in memories:require(memories[e['memory']]==e['allocation'],'Shared allocation size')
        else:memories[e['memory']]=e['allocation']
        for old in seen.values():
            if old['memory']==e['memory']:require(old['type']==e['type'] and (old['offset']+old['required']<=e['offset'] or e['offset']+e['required']<=old['offset']),'Live ranges overlap')
        seen[e['buffer']]=e
    require(s['memories']==len(memories) and s['bytes']==sum(memories.values()),'Native memory conservation/no empty cache');return seen
def validate_mixed(r,case,validation):
    a,b,bind,success,reuse,held=MIXED[case]
    expected=dict(schema_version=1,contract='vulkan_mixed_fallback_v1',case=case,validation=validation,SDK_source_fingerprint=CANDIDATE,source_fingerprint=fingerprint(),verified_buffers=3*(7+3*reuse),verified_bytes=3*(28800+12288*reuse))
    require(all(k in r and type(r[k]) is type(v) and r[k]==v for k,v in expected.items()),'Mixed fault metadata/readback work');identity(r,validation)
    expected_checks=(85 if held else 79 if not success or reuse else 61)+int(validation)
    require(type(r['assertions']) is int and r['assertions']==expected_checks and len(r['cycles'])==3,'Mixed assertions/cycles')
    def added(before,after,allocation,offset=0):
        require(set(before)<set(after) and len(after)==len(before)+1 and all(same(after[h],v) for h,v in before.items()),'Existing native bindings changed')
        e=after[next(iter(set(after)-set(before)))];require(e['offset']==offset and e['required']==4096 and e['allocation']==allocation,'Exact target binding/bytes');return e
    for cycle in r['cycles']:
        require(set(cycle)=={'before','reused','after_target','retry','clean','pending_after_reuse'},'Cycle schema')
        before,reused,after,retry,clean=(snapshots(cycle[k]) for k in ('before','reused','after_target','retry','clean'))
        s=cycle['before'];require(s['buffers']==s['memories']==3+reuse+held and s['bytes']==12352+8192*reuse+4096*held,'Mixed guard preparation')
        require(sorted(e['required'] for e in before.values())==sorted([64,4096,8192]+([4096] if reuse or held else [])),'Actual guard sizes')
        require(type(cycle['pending_after_reuse']) is int and cycle['pending_after_reuse']==(4 if reuse else 0),'No allocation fault consumed by hole reuse')
        if reuse:
            e=added(before,reused,8192,4096);require(any(v['memory']==e['memory'] and v['required']==4096 and v['allocation']==8192 for v in before.values()),'Existing pool hole actually reused')
        else:require(same(before,reused),'Unexpected reuse activity')
        if success:
            e=added(reused,after,4096);require(e['memory'] not in {v['memory'] for v in reused.values()},'Fallback must use exact new standalone memory');require(same(after,retry),'Successful target reallocated on retry')
        else:require(same(before,after),'Failed creation changed guard owners');e=added(before,retry,8192);require(e['memory'] not in {v['memory'] for v in before.values()},'Retry restores growth policy')
        require(not clean and not any(cycle['clean'][k] for k in ('memories','bytes','buffers','images')),'Native release remainder')
    t=r['native_trace'];require(type(t['schema_version']) is int and t['schema_version']==1 and t['source_fingerprint']==fingerprint() and all(type(t[k]) is int and t[k]==0 for k in ('errors','dropped_events','device_cleanup_resources','device_cleanup_bytes')),'Layer identity/accounting')
    frames=t['frames'];require(all(type(f[k]) is int for f in frames for k in ('phase','iteration')) and [(f['phase'],f['iteration']) for f in frames]==[(p,i) for i in range(3) for p in range(1,6)]+[(6,3)],'Native phase matrix')
    injected=[]
    for i in range(3):
        injected.append(dict(iteration=i,call='AllocateMemory',result=a,bytes_or_offset=16384 if reuse else 8192,poisoned=True))
        if b:injected.append(dict(iteration=i,call='AllocateMemory',result=b,bytes_or_offset=4096,poisoned=True))
        if bind:injected.append(dict(iteration=i,call='BindBufferMemory',result=bind,bytes_or_offset=0,poisoned=False))
    fault=t['fault'];require(all(type(fault[k]) is int and fault[k]==0 for k in ('pending','invalid_frees','guard_errors')) and same(fault['injections'],injected),'Exact failures/bytes consumed without invalid cleanup')
    previous=dict(memories=0,bytes=0,buffers=0,images=0);totals={k:0 for k in churn.CALLS};allocated=freed=0
    def counts(c):
        require(set(c)=={'calls','successes','allocated_bytes','freed_bytes','errors'} and type(c['errors']) is int and c['errors']==0 and churn.integer(c['allocated_bytes']) and churn.integer(c['freed_bytes']),'Native counter shape')
        require(set(c['calls'])==set(c['successes'])==set(churn.CALLS) and all(churn.integer(c[k][n]) for k in ('calls','successes') for n in churn.CALLS),'Native call types')
        require(all(c['successes'][n]<=c['calls'][n] for n in churn.CALLS),'Successes exceed attempts')
    counts(t['outside']);require(not any(t['outside']['calls'].values()) and not t['outside']['allocated_bytes'] and not t['outside']['freed_bytes'],'Outside resource activity')
    for f in frames:
        c=f['counts'];counts(c);before=f['before'];after=f['after'];calls=c['calls'];ok=c['successes'];phase=f['phase'];i=f['iteration']
        for live in (before,after):require(set(live)==set(previous) and all(churn.integer(v) for v in live.values()),'Live scalar types')
        require(same(before,previous),'Interval continuity');previous=after
        require(after['buffers']==before['buffers']+ok['CreateBuffer']-ok['DestroyBuffer'] and after['memories']==before['memories']+ok['AllocateMemory']-ok['FreeMemory'] and after['bytes']==before['bytes']+c['allocated_bytes']-c['freed_bytes'] and after['images']==before['images']==0,'Native interval conservation')
        require(not any(calls[k] for k in ('CreateImage','DestroyImage','BindImageMemory')),'Unexpected image work')
        if phase==1:require(same(after,{k:r['cycles'][i]['before'][k] for k in previous}),'Trace guard checkpoint differs')
        if phase==2:
            successes=int(success or bool(bind));require(calls['CreateBuffer']==ok['CreateBuffer']==1+reuse and calls['AllocateMemory']==1+bool(b)+successes and ok['AllocateMemory']==successes,'Exact block/fallback attempts')
            require(calls['BindBufferMemory']==int(success or bool(bind))+reuse and ok['BindBufferMemory']==int(success)+reuse and calls['DestroyBuffer']==ok['DestroyBuffer']==int(not success),'Exact target binding/cleanup')
            require(calls['FreeMemory']==ok['FreeMemory']==int(bool(bind)) and c['allocated_bytes']==4096*successes and c['freed_bytes']==4096*int(bool(bind)),'Fallback allocation/free bytes')
            require(same(after,{k:r['cycles'][i]['after_target'][k] for k in previous}),'Trace target checkpoint differs')
        elif phase==3:
            helpers=5+3*reuse-held;retry_count=int(not success)
            require(same(calls,ok) and calls['CreateBuffer']==calls['BindBufferMemory']==helpers+retry_count and calls['DestroyBuffer']==calls['FreeMemory']==helpers and calls['AllocateMemory']==helpers+retry_count,'Exact retry/GPU helper work')
            require(c['allocated_bytes']==20544+12288*reuse-4096*held+8192*retry_count and c['freed_bytes']==20544+12288*reuse-4096*held,'Retry/helper byte intervals')
            require(same(after,{k:r['cycles'][i]['retry'][k] for k in previous}),'Trace retry checkpoint differs')
        else:require(same(calls,ok),'Unexpected failure outside injection interval')
        if phase>=4:require(not any(after.values()),'Release/idle/teardown remainder')
        if phase>=5:require(not any(calls.values()),'Idle/teardown hid cleanup')
        for k in totals:totals[k]+=calls[k]
        allocated+=c['allocated_bytes'];freed+=c['freed_bytes']
    require(same(t['live'],previous) and not any(previous.values()) and totals['CreateBuffer']==totals['DestroyBuffer'] and totals['AllocateMemory']-totals['FreeMemory']==len(injected)-3*int(bool(bind)) and allocated==freed,'Final object/byte conservation')
    return dict(verified_buffers=r['verified_buffers'],verified_bytes=r['verified_bytes'],injections=len(injected),buffers=totals['CreateBuffer'],allocation_attempts=totals['AllocateMemory'],native_frees=totals['FreeMemory'],allocated_bytes=allocated)
def builds():return {kind:ROOT/f'out/vulkan-pool-rebase-fault-{kind}/Release' for kind in ('regression','mixed')}
def controls(gpu):
    for kind,case in (('regression','control'),('mixed','mixed-host')):
        args=[case,gpu,'0']
        yield kind+'-missing-layer',kind,args,'missing-layer'
        yield kind+'-wrong-layer',kind,args,'wrong-layer'
        yield kind+'-wrong-gpu',kind,[case,'absent GPU','0'],None
        for i,a in enumerate(([],['unknown',gpu,'0'],[case,gpu,'2'],args+['extra'])):yield kind+'-cli-'+str(i),kind,a,None
def run(out,pilot=False):
    out=out.resolve();require(out.is_relative_to(ROOT) and not out.exists(),'Exclusive output root');out.mkdir(parents=True)
    require(audit.sdk_fingerprint(ROOT)==SDK and audit.sdk_fingerprint(SOURCE)==CANDIDATE,'Unmodified reviewed product/candidate');fp=fingerprint();oldfp=regression.fingerprint();folders=builds()
    artifacts=[record(p) for d in folders.values() for p in sorted(d.iterdir()) if p.suffix.lower() in ('.exe','.dll','.json')]
    receipt=json.loads((ROOT/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));vvl=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    env0={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))};env0['VK_LOADER_LAYERS_DISABLE']='~implicit~';settings=out/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_text('# SDK pNext validation controls.\n');commands=[];runs=[]
    def call(name,kind,args,validation=False,expected=0,overrides=None):
        require(fingerprint()==fp and regression.fingerprint()==oldfp and audit.sdk_fingerprint(ROOT)==SDK and audit.sdk_fingerprint(SOURCE)==CANDIDATE,'Source drift')
        for r in artifacts+receipt['artifacts']:require(record(ROOT/r['path'])==r,'Runtime artifact drift')
        d=folders[kind];exe=d/('vulkan_buffer_fault.exe' if kind=='regression' else 'vulkan_rebase_fault.exe');env={**env0,'VK_LAYER_PATH':str(d)+(os.pathsep+str(vvl.parent) if validation else ''),'VK_INSTANCE_LAYERS':'VK_LAYER_GFX_buffer_fault','GFX_EXPECT_FAULT_LAYER':str(d/'VkLayer_gfx_buffer_fault.dll'),'VK_LAYER_SETTINGS_PATH':str(settings)}
        if validation:env['GFX_EXPECT_VULKAN_LAYER']=str(vvl)
        p=subprocess.run([str(exe),*args],cwd=ROOT,env={**env,**(overrides or {})},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
        for suffix,data in (('stdout',p.stdout),('stderr',p.stderr)):(out/(name+'.'+suffix)).write_bytes(data)
        commands.append(dict(name=name,kind=kind,argv=[str(exe),*args],validation=validation,expected=expected,returncode=p.returncode,overrides=overrides or {}));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');print(name,p.returncode,p.stderr.decode(errors='replace')[:180],flush=True)
        require(p.returncode==expected,name+' unexpected exit')
        if expected:require(not p.stdout and b'FAIL:' in p.stderr,'Control emitted valid result');return
        require(not p.stderr,'Positive process diagnostics');return json.loads(p.stdout)
    probe=call('probe','regression',['--probe']);require(probe['SDK_source_fingerprint']==CANDIDATE and len(set(probe['adapters']))==2,'Two GPUs required')
    for i,gpu in enumerate(probe['adapters'][:1] if pilot else probe['adapters']):
        for validation in ((False,) if pilot else (False,True)):
            for kind,cases in (('regression',regression.CASES),('mixed',MIXED)):
                if pilot and kind=='regression':cases=('control',)
                for case in cases:
                    name=f'{kind}-gpu{i}-v{int(validation)}-{case}';r=call(name,kind,[case,gpu,str(int(validation))],validation)
                    if kind=='regression':regression.validate(r,case,validation,CANDIDATE)
                    else:validate_mixed(r,case,validation)
                    require(r['device_name']==gpu,'Exact adapter differs');runs.append(dict(name=name,kind=kind,result=r))
    if not pilot:
        missing=out/'missing';missing.mkdir()
        for name,kind,args,override in controls(probe['adapters'][0]):
            env={'VK_LAYER_PATH':str(missing)} if override=='missing-layer' else {'GFX_EXPECT_FAULT_LAYER':str(missing/'absent.dll')} if override=='wrong-layer' else None
            call(name,kind,args,False,1,env)
    x=dict(schema_version=1,contract='vulkan_pool_rebase_fault_matrix_v1',SDK_source_fingerprint=SDK,candidate_SDK_source_fingerprint=CANDIDATE,source_fingerprint=fp,regression_source_fingerprint=oldfp,production_adopted=False,performance_remeasured=False,physical_memory_reclamation_verified=False,pilot=pilot,probe=probe,artifacts=artifacts,runs=runs)
    (out/'results.json').write_text(json.dumps(x,indent=2)+'\n');(out/'runtime-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--pilot',action='store_true');a=p.parse_args();run(a.output,a.pilot)
