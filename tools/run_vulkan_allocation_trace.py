"""Bounded, process-local Vulkan native allocation observations; no performance samples."""
import argparse
import copy
import hashlib
import json
import os
import pathlib
import subprocess
import time
import run_vulkan_allocation as base

ROOT=base.ROOT
BUILD=ROOT/'out/vulkan-allocation-trace-verified'
CONTRACT='vulkan_scoped_native_allocation_trace_v1'
CALLS=('CreateBuffer','DestroyBuffer','CreateImage','DestroyImage','AllocateMemory','FreeMemory','BindBufferMemory','BindImageMemory')
LIVE=('memories','bytes','buffers','images')
PHASES={1:'target_create',2:'helpers_and_gpu',3:'helper_release',4:'target_release',5:'post_release_idle',6:'device_teardown'}
record=base.record
require=base.require

def fingerprint(root=ROOT):
    paths=list((root/'benchmarks/vulkan_allocation_trace').glob('*.cpp'))+list((root/'benchmarks/vulkan_allocation_trace').glob('*.hpp'))+[root/n for n in ('benchmarks/vulkan_allocation_trace/CMakeLists.txt','benchmarks/vulkan_allocation/Payload.hpp','licenses/vulkan-allocation-trace-policy.json','benchmarks/vulkan_upload/Identity.hpp','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/competitors/sources.lock.json')]
    return hashlib.sha256(''.join(f'{p.relative_to(root).as_posix()}|{hashlib.sha256(p.read_bytes()).hexdigest()}\n' for p in sorted(paths,key=lambda p:p.as_posix())).encode()).hexdigest()

def uint(n):return type(n) is int and 0<=n<2**64

def live(x):
    require(set(x)==set(LIVE) and all(uint(v) for v in x.values()),'Invalid live object/byte snapshot')

def counts(c):
    require(set(c)=={'calls','successes','allocated_bytes','freed_bytes','errors'},'Different native count schema')
    for k in ('calls','successes'):require(set(c[k])==set(CALLS) and all(uint(v) for v in c[k].values()),'Invalid native call counters')
    require(all(uint(c[k]) for k in ('allocated_bytes','freed_bytes','errors')) and c['errors']==0,'Native accounting error')
    require(all(c['successes'][k]<=c['calls'][k] for k in CALLS),'More successes than calls')
    require(all(c['successes'][k]==c['calls'][k] for k in ('CreateBuffer','CreateImage','AllocateMemory','BindBufferMemory','BindImageMemory')),'Native operation failure')

def validate(r,sdk,scenario,pref,validation,reference,source,sdk_source,pin):
    require(r.get('category')=='native_allocation_trace' and r.get('contract')==CONTRACT,'Different diagnostic contract')
    alias=copy.deepcopy(r);alias.update(category='cpu_api',contract=base.CONTRACT);base.validate_identity(alias)
    expected=dict(schema_version=1,sdk=sdk,scenario=scenario,preference=pref,configuration='Release',validation=validation,source_fingerprint=source,SDK_source_fingerprint=sdk_source,SDL_commit=pin,warmup=1,sample_count=3,probe=False,readback_verified=True,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,native_allocation_count_verified=True,timings_excluded=True,gpu_ms=None,columns=[],samples_ms=[],requested_instance_api_version=4198400)
    require(all(k in r and r[k]==v and type(r[k]) is type(v) for k,v in expected.items()),'Different requested trace workload/timing/scope')
    require(all(r.get(k)==reference[k] for k in base.IDENTITY_KEYS),'GPU/driver/environment changed')
    require(bool(r.get('loaded_validation_layer'))==validation,'Validation layer mode differs')
    texture=scenario.startswith('texture');n=base.WORK[scenario];total=n*4
    expected=dict(resources_per_iteration=n,created_resources=total,released_resource_handles=total,used_resources=total,verified_resources=total,verified_images=total if texture else 0,verified_buffers=0 if texture else total,verified_bytes=total*(9620 if texture else 4096),width=65 if texture else 0,height=37 if texture else 0,texel_bytes=4 if texture else 0,buffer_bytes=0 if texture else 4096,resource_kind='texture' if texture else 'buffer',allocation_policy='public_device_resource_default_policy',usage_policy='sampled_rgba8_2d' if texture else 'sdl_vertex_buffer' if sdk=='sdl' else 'sdk_general_device_buffer')
    require(all(k in r and r[k]==v and type(r[k]) is type(v) for k,v in expected.items()),'Different resource or incomplete full readback')
    t=r['native_trace'];require(t.get('schema_version')==1 and t.get('source_fingerprint')==source,'Stale trace layer/schema')
    for k in ('dropped_events','errors','device_cleanup_resources','device_cleanup_bytes'):require(type(t.get(k)) is int and t[k]==0,'Dropped/invalid/unclosed native resource events')
    live(t['live']);require(not any(t['live'].values()),'Device teardown left observed live resources')
    counts(t['outside']);frames=t['frames'];require(len(frames)==21,'Incomplete scoped iterations')
    require([(f['phase'],f['iteration']) for f in frames]==[(p,i) for i in range(4) for p in range(1,6)]+[(6,4)],'Changed phase/iteration order')
    previous=None
    for f in frames:
        live(f['before']);live(f['after']);counts(f['counts']);a=f['before'];b=f['after'];c=f['counts'];s=c['successes']
        if previous is not None:require(previous==a,'Unscoped resource mutation between phases')
        previous=b
        delta=dict(memories=s['AllocateMemory']-s['FreeMemory'],bytes=c['allocated_bytes']-c['freed_bytes'],buffers=s['CreateBuffer']-s['DestroyBuffer'],images=s['CreateImage']-s['DestroyImage'])
        require(all(b[k]-a[k]==delta[k] for k in LIVE),'Native object/byte conservation differs')
        if f['phase']==1:
            require(s['CreateImage']==(n if texture else 0) and s['CreateBuffer']==(0 if texture else n),'Target create count differs from logical workload')
            if sdk=='graphicssdk':require(s['AllocateMemory']==n and s['BindImageMemory' if texture else 'BindBufferMemory']==n,'SDK dedicated allocation control differs')
        if f['phase']==4 and sdk=='graphicssdk':
            require(s['FreeMemory']==n and s['DestroyImage' if texture else 'DestroyBuffer']==n,'SDK release control differs')
    require(previous==t['live'],'Final live snapshot differs')
    return t

def run(args):
    from audit_licenses import sdk_fingerprint,verify_checkout
    out=args.output.resolve();require(out.is_relative_to(ROOT) and not out.exists(),'Exclusive repository output root required')
    build=args.build.resolve();require(build.is_relative_to(ROOT),'Build root must be inside this repository')
    exe=build/'Release/vulkan_allocation_trace.exe';dll=build/'Release/VkLayer_gfx_allocation.dll';manifest=build/'Release/VkLayer_gfx_allocation.json'
    baseline=ROOT/'out/vulkan-allocation-benchmark-final/Release/vulkan_allocation.exe'
    artifacts=[record(p) for p in (exe,dll,manifest,baseline)];source=fingerprint();sdk_source=sdk_fingerprint(ROOT)
    pin=json.loads((ROOT/'benchmarks/competitors/sources.lock.json').read_text(encoding='utf-8'))['sdl']['commit'];verify_checkout(ROOT,'sdl',pin)
    receipt=json.loads((ROOT/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));vvl=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    out.mkdir(parents=True);settings=out/'settings';settings.mkdir()
    (settings/'vk_layer_settings.txt').write_text('khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n',encoding='utf-8')
    clean={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))};clean['VK_LOADER_LAYERS_DISABLE']='~implicit~'
    common={**clean,'VK_LAYER_PATH':str(dll.parent)+os.pathsep+str(vvl.parent),'GFX_EXPECT_TRACE_LAYER':str(dll),'VK_INSTANCE_LAYERS':'VK_LAYER_GFX_allocation_trace'}
    validated={**common,'VK_INSTANCE_LAYERS':'VK_LAYER_GFX_allocation_trace'+os.pathsep+'VK_LAYER_KHRONOS_validation','GFX_EXPECT_VULKAN_LAYER':str(vvl),'VK_LAYER_SETTINGS_PATH':str(settings),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true'}
    (out/'environment.json').write_text(json.dumps(dict(removed_inherited_keys=sorted(set(os.environ)-set(clean)),trace_overrides={k:v for k,v in common.items() if clean.get(k)!=v},validation_overrides={k:v for k,v in validated.items() if clean.get(k)!=v}),indent=2)+'\n')
    commands=[];runs=[];probes={}
    def frozen():
        require(fingerprint()==source and sdk_fingerprint(ROOT)==sdk_source,'Sources changed during trace')
        for a in artifacts+receipt['artifacts']:require(record(ROOT/a['path'])==a,'Frozen executable/runtime changed')
    def call(name,argv,validation=False,expected=0,overrides=None,probe=False):
        frozen();start=time.monotonic();env=clean if probe else validated if validation else common;actual=baseline if probe else exe
        p=subprocess.run([str(actual),*argv],cwd=ROOT,env={**env,**(overrides or {})},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=180)
        (out/(name+'.stdout')).write_bytes(p.stdout);(out/(name+'.stderr')).write_bytes(p.stderr)
        commands.append(dict(name=name,argv=[str(actual),*argv],validation=validation,expected=expected,returncode=p.returncode,overrides=overrides or {},seconds=time.monotonic()-start));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        require(p.returncode==expected,name+' failed; inspect raw output')
        if expected:require(not p.stdout and b'FAIL:' in p.stderr,'Control emitted results or lacked failure');return None
        r=base.parse_output(p.stdout,p.stderr);frozen();print(name,'PASS',flush=True);return r
    for pref in ('high','low'):
        r=call('probe-'+pref,['--probe','sdl',pref],probe=True);base.validate_identity(r);probes[pref]=r
        require(r['source_fingerprint']==base.fingerprint() and r['SDK_source_fingerprint']==sdk_source and r['SDL_commit']==pin,'Stale baseline identity probe')
    require(probes['high']['device_uuid']!=probes['low']['device_uuid'],'Two distinct adapters required')
    matrix=[('high','graphicssdk','buffer1',True),('high','sdl','texture1',True)] if args.pilot else [(p,s,c,v) for v in (True,False) for p in probes for s in ('graphicssdk','sdl') for c in base.SHAPES]
    for pref,sdk,scenario,validation in matrix:
        name=f'trace-{int(validation)}-{pref}-{sdk}-{scenario}'
        r=call(name,[sdk,scenario,pref,'1','3',str(int(validation)),probes[pref]['device_name']],validation,overrides={'VK_LOADER_DEBUG':'layer'} if validation and scenario=='buffer1' else None)
        validate(r,sdk,scenario,pref,validation,probes[pref],source,sdk_source,pin);runs.append(dict(name=name,result=r))
    if not args.pilot:
        for sdk in ('graphicssdk','sdl'):
            argv=[sdk,'buffer1','high','1','3','1',probes['high']['device_name']]
            call('missing-trace-'+sdk,argv,True,1,{'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation'})
            call('wrong-trace-path-'+sdk,argv,True,1,{'GFX_EXPECT_TRACE_LAYER':str(vvl)})
            call('invalid-phase-'+sdk,argv,True,1,{'GFX_TRACE_INVALID_PHASE':'1'})
            call('missing-vvl-'+sdk,argv,True,1,{'VK_LAYER_PATH':str(dll.parent)})
            call('wrong-gpu-'+sdk,[sdk,'buffer1','high','1','3','0','absent-exact-GPU'],False,1)
        for n,argv in enumerate(([],['sdl','unknown','high','1','3','0','GPU'],['sdl','buffer1','high','-1','3','0','GPU'],['sdl','buffer1','high','1','2','0','GPU'],['sdl','buffer1','high','1','3','2','GPU'],['sdl','buffer1','high','1','3','0','GPU','extra'])):call('invalid-option-'+str(n),argv,False,1)
    x=dict(schema_version=1,contract=CONTRACT,source_fingerprint=source,SDK_source_fingerprint=sdk_source,SDL_commit=pin,artifacts=artifacts,probes=probes,runs=runs,pilot=args.pilot,timings_excluded=True,physical_memory_reclamation_verified=False,native_driver_internal_allocations_verified=False,production_SDK_changed=False)
    for k in ('created_resources','released_resource_handles','used_resources','verified_resources','verified_images','verified_buffers','verified_bytes'):x[k]=sum(v['result'][k] for v in runs)
    (out/'results.json').write_text(json.dumps(x,indent=2)+'\n');(out/'runtime-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS',len(runs),'profile processes',x['verified_resources'],'verified resources',x['verified_bytes'],'bytes',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--build',type=pathlib.Path,default=BUILD);p.add_argument('--pilot',action='store_true');run(p.parse_args())
