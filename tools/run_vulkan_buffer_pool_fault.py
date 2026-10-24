"""Bounded synthetic failures and full GPU readback; no performance or physical-memory claim."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import struct
import subprocess
import audit_licenses as audit
import run_vulkan_allocation_trace as trace
ROOT=trace.ROOT
SOURCE=ROOT/'out/vulkan-buffer-pool-fault-source'
PRIOR='4f4e63639037f18613227e71cc5635956fa3a2b095807621aadb6fcba6aecb43'
# name: guards, allocation errors, bind error, target bytes, host memory, succeeds, held recording
CASES={
 'control':(0,(),0,4096,False,True,False),
 'pool-host':(1,(-1,),0,4096,False,True,False),
 'pool-device':(1,(-2,),0,4096,False,True,False),
 'double-host':(1,(-1,-1),0,4096,False,False,False),
 'double-device':(1,(-2,-2),0,4096,False,False,False),
 'mixed-oom':(1,(-1,-2),0,4096,False,False,False),
 'pool-unknown':(1,(-13,),0,4096,False,False,False),
 'bind-fresh':(0,(),-2,4096,False,False,False),
 'bind-grow':(1,(),-1,4096,False,False,False),
 'bind-reuse':(2,(),-2,4096,False,False,False),
 'fallback-bind':(1,(-2,),-1,4096,False,False,False),
 'dedicated-oom':(1,(-2,),0,8192,False,False,False),
 'upload-oom':(1,(-1,),0,4096,True,False,False),
 'readback-oom':(1,(-2,),0,4096,True,False,False),
 'held-bind':(2,(),-2,4096,False,False,True),
}
def require(b,message):
    if not b:raise ValueError(message)
def fingerprint():
    paths=list((ROOT/'experiments/vulkan_buffer_pool_fault').glob('*.cpp'))+list((ROOT/'experiments/vulkan_buffer_pool_fault').glob('*.hpp'))+[ROOT/p for p in ('experiments/vulkan_buffer_pool_fault/CMakeLists.txt','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/Identity.hpp')]
    return hashlib.sha256(''.join(f'{p.relative_to(ROOT).as_posix()}|{audit.sha256(p.read_bytes())}\n' for p in sorted(paths,key=lambda p:p.as_posix())).encode()).hexdigest()
def record(p):return audit.record_file(ROOT,p)

def validate(r,case,validation,sdk,unsafe=False):
    g,alloc,bind,size,host,success,held=CASES[case]
    expected=dict(schema_version=1,contract='vulkan_buffer_fault_v1',case=case,validation=validation,SDK_source_fingerprint=sdk,source_fingerprint=fingerprint(),repetitions=3,verified_buffers=3*(2*g+1),verified_bytes=3*(g*8192+size),regression_detected=unsafe)
    require(all(k in r and r[k]==v and type(r[k]) is type(v) for k,v in expected.items()),'Fault process metadata/readback differs')
    require(type(r['assertions']) is int and r['assertions']>=20,'Missing native assertions')
    require(bool(r['loaded_validation_layer'])==validation,'Wrong validation mode')
    for k,n in (('device_uuid',32),('driver_uuid',32),('device_luid',16)):
        require(isinstance(r[k],str) and re.fullmatch('[0-9a-f]{'+str(n)+'}',r[k]) and int(r[k],16)!=0,'Invalid hardware identity')
    env=r['environment'];require(env['gpu']==r['device_name'] and env['software_renderer'] is False and env['pointer_bits']==64 and env['driver_query_available'] is True,'Unidentified test environment')
    hi,lo=map(int,env['luid'].split(':'));require(struct.pack('<II',lo,hi&0xffffffff).hex()==r['device_luid'],'DXGI/native LUID differs')
    t=r['native_trace'];require(type(t['schema_version']) is int and t['schema_version']==1 and t['source_fingerprint']==fingerprint(),'Fault layer identity differs')
    require(all(type(t[k]) is int and t[k]==0 for k in ('errors','dropped_events','device_cleanup_resources','device_cleanup_bytes')),'Layer accounting/implicit cleanup failed')
    fault=t['fault'];require(fault['pending']==0 and type(fault['pending']) is int and fault['guard_errors']==0 and type(fault['guard_errors']) is int,'Unused plan or binding/ownership guard failure')
    require(type(fault['invalid_frees']) is int and fault['invalid_frees']==(3 if unsafe else 0),'Unexpected invalid memory free')
    require(not unsafe or case in ('double-device','dedicated-oom'),'Unreviewed unsafe control')
    expected_events=[]
    for i in range(3):
        for j,code in enumerate(alloc):
            requested=size if host or size>4096 else (8192 if j==0 else 4096)
            expected_events.append(dict(iteration=i,call='AllocateMemory',result=code,bytes_or_offset=requested,poisoned=True))
        if bind:expected_events.append(dict(iteration=i,call='BindBufferMemory',result=bind,bytes_or_offset=4096 if g==2 else 0,poisoned=False))
    require(fault['injections']==expected_events and all(type(e[k]) is int for e in fault['injections'] for k in ('iteration','result','bytes_or_offset')) and all(type(e['poisoned']) is bool for e in fault['injections']),'Injected plan/bytes/offsets differ')
    frames=t['frames'];require(all(type(f['phase']) is int and type(f['iteration']) is int for f in frames) and [(f['phase'],f['iteration']) for f in frames]==[(p,i) for i in range(3) for p in range(1,6)]+[(6,3)],'Fault phase matrix differs')
    def counts(c):
        require(set(c)=={'calls','successes','allocated_bytes','freed_bytes','errors'},'Different native counters')
        for k in ('calls','successes'):
            require(set(c[k])==set(trace.CALLS) and all(trace.uint(v) for v in c[k].values()),'Invalid call counters')
        require(all(trace.uint(c[k]) for k in ('allocated_bytes','freed_bytes','errors')) and c['errors']==0,'Counter accounting failure')
        require(all(c['successes'][k]<=c['calls'][k] for k in trace.CALLS),'More successes than attempts')
    previous=None
    for f in frames:
        before,c,after=f['before'],f['counts'],f['after'];trace.live(before);trace.live(after);counts(c);s=c['successes'];a=c['calls'];phase=f['phase']
        require(previous is None or previous==before,'Native scope discontinuity')
        delta=dict(memories=s['AllocateMemory']-s['FreeMemory'],bytes=c['allocated_bytes']-c['freed_bytes'],buffers=s['CreateBuffer']-s['DestroyBuffer'],images=s['CreateImage']-s['DestroyImage'])
        require(all(after[k]-before[k]==delta[k] for k in trace.LIVE),'Native conservation failed')
        require(not any(a[k] for k in ('CreateImage','DestroyImage','BindImageMemory')),'Unexpected image work')
        if phase==1:
            require(after==dict(memories=g+int(held),bytes=(0 if g==0 else 4096 if g==1 else 12288)+4096*held,buffers=g+int(held),images=0),'Guard preparation differs')
        if phase==2:
            shared=g==2 and not alloc;has_memory=success or bool(bind)
            successful_alloc=int(has_memory and not shared)
            attempts=len(alloc)+successful_alloc
            allocated=(4096 if alloc else 4096 if g==0 else 8192)*successful_alloc
            target_memory=int(has_memory and not shared and success)
            require(a['CreateBuffer']==s['CreateBuffer']==1 and a['AllocateMemory']==attempts and s['AllocateMemory']==successful_alloc,'Failure allocation attempts differ')
            require(a['BindBufferMemory']==int(has_memory) and s['BindBufferMemory']==int(success),'Failure binding calls differ')
            require(a['DestroyBuffer']==s['DestroyBuffer']==int(not success),'Failed buffer was not destroyed')
            require(s['FreeMemory']==int(bool(bind) and not shared) and a['FreeMemory']==s['FreeMemory']+int(unsafe),'Failed memory/slot cleanup differs')
            require(c['allocated_bytes']==allocated and c['freed_bytes']==(allocated if bind else 0),'Failure allocation/free bytes differ')
            require(after['buffers']==before['buffers']+int(success) and after['memories']==before['memories']+target_memory and after['bytes']==before['bytes']+(allocated if success else 0),'Failed creation changed existing resources')
        if phase==3:
            helpers=(1 if host else 2)+g-int(held);retry=int(not success);new_alloc=retry*int(not(g==2 and not host))
            require(a==s,'Unexpected native failure during retry/GPU verification')
            require(s['CreateBuffer']==s['BindBufferMemory']==helpers+retry and s['DestroyBuffer']==s['FreeMemory']==helpers and s['AllocateMemory']==helpers+new_alloc,'Retry lost/reallocated a shared slot')
        if phase==4:require(not any(after.values()) and a==s,'Final release leaked native objects')
        if phase in (5,6):require(not any(after.values()) and not any(a.values()),'Idle/teardown hid a leak')
        previous=after
    counts(t['outside']);require(not any(t['outside']['calls'].values()) and t['live']==previous and not any(previous.values()),'Outside activity/final native remainder')
    return r

def run(out):
    out=out.resolve();require(out.is_relative_to(ROOT) and not out.exists(),'Exclusive output root required');out.mkdir(parents=True)
    sdk=audit.sdk_fingerprint(SOURCE);require(sdk!=PRIOR,'Unchanged failure candidate')
    builds={v:ROOT/('out/vulkan-buffer-pool-fault-'+v)/'Release' for v in ('prior','fixed')}
    artifacts=[record(p) for d in builds.values() for p in sorted(d.iterdir()) if p.suffix.lower() in ('.exe','.dll','.json')]
    receipt=json.loads((ROOT/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));vvl=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    clean={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))};clean['VK_LOADER_LAYERS_DISABLE']='~implicit~'
    settings=out/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_text('# SDK pNext settings enable synchronization validation.\n')
    commands=[];runs=[];source=fingerprint();ids=dict(prior=PRIOR,fixed=sdk)
    def call(name,variant,args,validation=False,expected=0,overrides=None):
        require(fingerprint()==source and audit.sdk_fingerprint(SOURCE)==sdk,'Sources changed during fault campaign')
        for r in artifacts+receipt['artifacts']:require(record(ROOT/r['path'])==r,'Fault binary/runtime changed')
        d=builds[variant];env={**clean,'VK_LAYER_PATH':str(d)+(os.pathsep+str(vvl.parent) if validation else ''),'VK_INSTANCE_LAYERS':'VK_LAYER_GFX_buffer_fault','GFX_EXPECT_FAULT_LAYER':str(d/'VkLayer_gfx_buffer_fault.dll'),'VK_LAYER_SETTINGS_PATH':str(settings)}
        if validation:env.update(GFX_EXPECT_VULKAN_LAYER=str(vvl))
        p=subprocess.run([str(d/'vulkan_buffer_fault.exe'),*args],cwd=ROOT,env={**env,**(overrides or {})},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
        for suffix,data in (('stdout',p.stdout),('stderr',p.stderr)):(out/(name+'.'+suffix)).write_bytes(data)
        commands.append(dict(name=name,variant=variant,argv=[str(d/'vulkan_buffer_fault.exe'),*args],validation=validation,expected=expected,returncode=p.returncode,overrides=overrides or {}));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        require(p.returncode==expected,name+' exit differs; inspect raw output')
        if expected==1:require(not p.stdout and b'FAIL:' in p.stderr,'Invalid control emitted results');return
        require(not p.stderr,name+' unexpected native/validation diagnostics');print(name,'PASS',flush=True);return json.loads(p.stdout)
    probe=call('probe','fixed',['--probe']);require(probe['SDK_source_fingerprint']==sdk and len(probe['adapters'])==2 and len(set(probe['adapters']))==2,'Two hardware GPUs required')
    for index,gpu in enumerate(probe['adapters']):
        for validation in (False,True):
            for case in CASES:
                name=f'gpu{index}-validation{int(validation)}-{case}';r=call(name,'fixed',[case,gpu,str(int(validation))],validation);validate(r,case,validation,sdk);runs.append(dict(name=name,variant='fixed',result=r))
            for case in ('double-device','dedicated-oom'):
                name=f'prior-gpu{index}-validation{int(validation)}-{case}';r=call(name,'prior',[case,gpu,str(int(validation))],validation,2);validate(r,case,validation,PRIOR,True);runs.append(dict(name=name,variant='prior',result=r))
    missing=out/'missing-layer';missing.mkdir()
    for index,gpu in enumerate(probe['adapters']):
        call(f'missing-fault-{index}','fixed',['control',gpu,'0'],False,1,{'VK_LAYER_PATH':str(missing)})
        call(f'wrong-gpu-{index}','fixed',['control','absent-GPU','0'],False,1)
    for index,args in enumerate(([],['other',probe['adapters'][0],'0'],['control',probe['adapters'][0],'2'],['control',probe['adapters'][0],'0','extra'])):call('invalid-cli-'+str(index),'fixed',args,False,1)
    x=dict(schema_version=1,contract='vulkan_buffer_fault_campaign_v1',source_fingerprint=source,SDK_source_fingerprint=sdk,prior_SDK_source_fingerprint=PRIOR,production_adopted=False,performance_remeasured=False,physical_memory_reclamation_verified=False,probe=probe,artifacts=artifacts,runs=runs)
    (out/'results.json').write_text(json.dumps(x,indent=2)+'\n');(out/'runtime-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS 60 clean fault processes / 8 guarded pre-fix regression controls / 8 fail-closed controls')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);run(p.parse_args().output)
