"""Fresh baseline/candidate/SDL buffer trials; native traces are separate from CPU timing."""
import argparse
import copy
import json
import os
import pathlib
import random
import subprocess
import time
import audit_licenses as audit
import run_vulkan_allocation as b
import run_vulkan_allocation_trace as t
ROOT=b.ROOT
SOURCE=ROOT/'out/vulkan-buffer-pool-source-v2'
VARIANTS=('baseline','candidate','sdl')
WORK=('buffer1','buffer1000')
def source_ids():return dict(baseline=audit.sdk_fingerprint(ROOT),candidate=audit.sdk_fingerprint(SOURCE))
def validate_result(r,variant,scenario,pref,validation,reference,ids):
    return b.validate_result(r,'sdl' if variant=='sdl' else 'graphicssdk',scenario,pref,1 if validation else 5,3 if validation else 30,validation,reference,b.fingerprint(),ids['candidate' if variant=='candidate' else 'baseline'],reference['SDL_commit'])
def validate_trace(r,variant,scenario,pref,reference,ids):
    alias=copy.deepcopy(r);alias.update(category='cpu_api',contract=b.CONTRACT);b.validate_identity(alias)
    sdk_source=ids['candidate' if variant=='candidate' else 'baseline'];n=b.WORK[scenario];native=n if variant=='baseline' else (n+15)//16
    expected=dict(category='native_allocation_trace',contract=t.CONTRACT,sdk='graphicssdk',scenario=scenario,preference=pref,validation=True,warmup=1,sample_count=3,source_fingerprint=t.fingerprint(),SDK_source_fingerprint=sdk_source,SDL_commit=reference['SDL_commit'],probe=False,readback_verified=True,resources_per_iteration=n,created_resources=4*n,released_resource_handles=4*n,used_resources=4*n,verified_resources=4*n,verified_buffers=4*n,verified_images=0,verified_bytes=4*n*4096,width=0,height=0,texel_bytes=0,buffer_bytes=4096,allocation_policy='public_device_resource_default_policy',usage_policy='sdk_general_device_buffer',requested_instance_api_version=4198400,GPU_use_in_timing=False,native_allocation_count_verified=True,timings_excluded=True,columns=[],samples_ms=[],gpu_ms=None,GPU_memory_reclamation_verified=False)
    b.require(all(k in r and r[k]==v and type(r[k]) is type(v) for k,v in expected.items()),'Wrong native trace trial metadata')
    b.require(all(r[k]==reference[k] for k in b.IDENTITY_KEYS),'Trace GPU/environment mismatch')
    b.require(bool(r.get('loaded_validation_layer')),'Required trace validation layer missing')
    x=r['native_trace'];b.require(type(x.get('schema_version')) is int and x['schema_version']==1 and x['source_fingerprint']==t.fingerprint() and all(type(x[k]) is int and x[k]==0 for k in ('dropped_events','errors','device_cleanup_resources','device_cleanup_bytes')),'Trace accounting failed')
    t.counts(x['outside']);t.live(x['live']);b.require(not any(x['live'].values()),'Trace native resources remain')
    fs=x['frames'];b.require([(f['phase'],f['iteration']) for f in fs]==[(p,i) for i in range(4) for p in range(1,6)]+[(6,4)],'Trace phase matrix differs')
    previous=None
    for f in fs:
        a,c,z=f['before'],f['counts'],f['after'];t.live(a);t.live(z);t.counts(c);s=c['successes']
        if previous is not None:b.require(previous==a,'Trace scope discontinuity')
        delta=dict(memories=s['AllocateMemory']-s['FreeMemory'],bytes=c['allocated_bytes']-c['freed_bytes'],buffers=s['CreateBuffer']-s['DestroyBuffer'],images=s['CreateImage']-s['DestroyImage'])
        b.require(all(z[k]-a[k]==delta[k] for k in t.LIVE),'Trace object/byte conservation')
        if f['phase']==1:
            b.require(s['CreateBuffer']==n and s['BindBufferMemory']==n and s['AllocateMemory']==native and s['CreateImage']==0,'Target native slab count differs')
            b.require(c['allocated_bytes']==(native*65536 if variant=='candidate' else n*4096),'Target reserved bytes differ')
        if f['phase']==4:b.require(s['DestroyBuffer']==n and s['FreeMemory']==native and not any(z.values()),'Empty slab did not free after final target')
        previous=z
    b.require(previous==x['live'],'Final native trace snapshot differs')
    return x

def run(out):
    out=out.resolve();b.require(out.is_relative_to(ROOT) and not out.exists(),'Exclusive output root required')
    ids=source_ids();b.require(ids['baseline']=='44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25' and ids['candidate']!=ids['baseline'],'Invalid candidate/product source identity')
    exe=dict(baseline=ROOT/'out/vulkan-allocation-benchmark-final/Release/vulkan_allocation.exe',candidate=ROOT/'out/vulkan-buffer-pool-benchmark/Release/vulkan_allocation.exe');exe['sdl']=exe['baseline']
    trace=dict(baseline=ROOT/'out/vulkan-allocation-trace-verified/Release/vulkan_allocation_trace.exe',candidate=ROOT/'out/vulkan-buffer-pool-trace/Release/vulkan_allocation_trace.exe')
    dlls={k:p.parent/'VkLayer_gfx_allocation.dll' for k,p in trace.items()}
    artifacts=[b.record(p) for p in sorted(set(exe.values())|set(trace.values())|set(dlls.values())|{p.parent/'VkLayer_gfx_allocation.json' for p in trace.values()})]
    candidate=json.loads((SOURCE/'candidate-receipt.json').read_text(encoding='utf-8'));b.require(candidate['candidate_SDK_source_fingerprint']==ids['candidate'],'Stale isolated SDK')
    receipt=json.loads((ROOT/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));vvl=next(ROOT/a['path'] for a in receipt['artifacts'] if a['path'].endswith('.dll'))
    out.mkdir(parents=True);settings=out/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_text('khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n',encoding='utf-8')
    clean={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))};clean['VK_LOADER_LAYERS_DISABLE']='~implicit~'
    validated={**clean,'VK_LAYER_PATH':str(vvl.parent),'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','GFX_EXPECT_VULKAN_LAYER':str(vvl),'VK_LAYER_SETTINGS_PATH':str(settings),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true'}
    (out/'environment.json').write_text(json.dumps(dict(removed_inherited_keys=sorted(set(os.environ)-set(clean)),clean_overrides={k:v for k,v in clean.items() if k not in os.environ},validation_overrides={k:v for k,v in validated.items() if clean.get(k)!=v}),indent=2)+'\n')
    commands=[];runs=[];traces=[];probes={};source=b.fingerprint();trace_source=t.fingerprint()
    def frozen():
        b.require(source_ids()==ids and b.fingerprint()==source and t.fingerprint()==trace_source,'Sources changed during trial')
        for a in artifacts+receipt['artifacts']:b.require(b.record(ROOT/a['path'])==a,'Binary/runtime changed')
    def call(name,variant,args,validation=False,expected=0,is_trace=False,overrides=None):
        frozen();environment=validated if validation else clean;binary=trace[variant] if is_trace else exe[variant]
        if is_trace:environment={**validated,'VK_LAYER_PATH':str(dlls[variant].parent)+os.pathsep+str(vvl.parent),'VK_INSTANCE_LAYERS':'VK_LAYER_GFX_allocation_trace'+os.pathsep+'VK_LAYER_KHRONOS_validation','GFX_EXPECT_TRACE_LAYER':str(dlls[variant])}
        start=time.monotonic();p=subprocess.run([str(binary),*args],cwd=ROOT,env={**environment,**(overrides or {})},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=180)
        for suffix,data in (('stdout',p.stdout),('stderr',p.stderr)):(out/(name+'.'+suffix)).write_bytes(data)
        commands.append(dict(name=name,variant=variant,argv=[str(binary),*args],validation=validation,trace=is_trace,expected=expected,returncode=p.returncode,seconds=time.monotonic()-start,overrides=overrides or {}));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        b.require(p.returncode==expected,name+' failed; inspect raw output')
        if expected:b.require(not p.stdout and b'FAIL:' in p.stderr,'Negative control emitted results');return None
        r=b.parse_output(p.stdout,p.stderr);frozen();print(name,'PASS',flush=True);return r
    for pref in ('high','low'):
        r=call('probe-'+pref,'sdl',['--probe','sdl',pref]);b.validate_identity(r);b.require(r['SDK_source_fingerprint']==ids['baseline'] and r['source_fingerprint']==source,'Stale baseline probe');probes[pref]=r
    b.require(probes['high']['device_uuid']!=probes['low']['device_uuid'],'Two GPU profile required')
    for pref in probes:
        for variant in ('baseline','candidate'):
            for scenario in WORK:
                name=f'trace-{pref}-{variant}-{scenario}';r=call(name,variant,['graphicssdk',scenario,pref,'1','3','1',probes[pref]['device_name']],True,is_trace=True)
                validate_trace(r,variant,scenario,pref,probes[pref],ids);traces.append(dict(name=name,variant=variant,result=r))
    for pref in probes:
        for variant in VARIANTS:
            for scenario in WORK:
                name=f'validation-{pref}-{variant}-{scenario}';sdk='sdl' if variant=='sdl' else 'graphicssdk';r=call(name,variant,[sdk,scenario,pref,'1','3','1',probes[pref]['device_name']],True)
                validate_result(r,variant,scenario,pref,True,probes[pref],ids);runs.append(dict(name=name,variant=variant,kind='correctness',result=r))
    jobs=[(p,v,c,rep) for p in probes for v in VARIANTS for c in WORK for rep in range(3)];random.Random(20261029).shuffle(jobs)
    for pref,variant,scenario,rep in jobs:
        name=f'measure-{pref}-{variant}-{scenario}-{rep}';sdk='sdl' if variant=='sdl' else 'graphicssdk';r=call(name,variant,[sdk,scenario,pref,'5','30','0',probes[pref]['device_name']])
        validate_result(r,variant,scenario,pref,False,probes[pref],ids);runs.append(dict(name=name,variant=variant,kind='measurement',result=r))
    missing=out/'missing-layer';missing.mkdir()
    for variant in VARIANTS:
        sdk='sdl' if variant=='sdl' else 'graphicssdk'
        call('missing-layer-'+variant,variant,[sdk,'buffer1','high','1','3','1',probes['high']['device_name']],True,1,overrides={'VK_LAYER_PATH':str(missing)})
        call('wrong-gpu-'+variant,variant,[sdk,'buffer1','high','1','3','0','absent-exact-GPU'],False,1)
    for n,args in enumerate(([],['graphicssdk','other','high','1','3','0','GPU'],['graphicssdk','buffer1','high','-1','3','0','GPU'],['graphicssdk','buffer1','high','1','2','0','GPU'],['graphicssdk','buffer1','high','1','3','2','GPU'],['graphicssdk','buffer1','high','1','3','0','GPU','extra'])):call('invalid-option-'+str(n),'candidate',args,False,1)
    groups=[]
    for pref in probes:
        for scenario in WORK:
            row=dict(preference=pref,scenario=scenario,device_name=probes[pref]['device_name'],statistics={})
            for variant in VARIANTS:row['statistics'][variant]=b.summarize([r['result'] for r in runs if r['kind']=='measurement' and r['variant']==variant and r['result']['preference']==pref and r['result']['scenario']==scenario])
            row['candidate_over_baseline_p50_ratio']={c:row['statistics']['candidate'][c]['p50_ms']/row['statistics']['baseline'][c]['p50_ms'] for c in b.COLUMNS};groups.append(row)
    x=dict(schema_version=1,contract='vulkan_small_buffer_slab_candidate_trial_v1',product_SDK_source_fingerprint=ids['baseline'],candidate_SDK_source_fingerprint=ids['candidate'],benchmark_source_fingerprint=source,trace_source_fingerprint=trace_source,artifacts=artifacts,probes=probes,runs=runs,traces=traces,groups=groups,repetitions=3,warmup=5,samples=30,seed=20261029,candidate_only=True,production_adopted=False,physical_memory_reclamation_verified=False,samples_removed=0,GPU_timestamps=False)
    for k in ('created_resources','released_resource_handles','verified_resources','verified_buffers','verified_bytes'):x[k]=sum(r['result'][k] for r in runs)
    (out/'results.json').write_text(json.dumps(x,indent=2)+'\n');(out/'runtime-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS 48 benchmark processes + 8 separate trace processes',x['verified_buffers'],'verified buffers',flush=True)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);run(p.parse_args().output)
