"""Separate Vulkan repeated PSO creation/public-release CPU API profile."""
import argparse
import hashlib
import json
import math
import os
import pathlib
import random
import re
import statistics
import struct
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONTRACT = 'vulkan_pipeline_create_public_release_use_checked_v1'
SHAPES = {k:(65,37,4) for k in ('pso1','pso8')}
WORK = {'pso1':1,'pso8':8}
COLUMNS = ('create_ms','public_release_ms')
IDENTITY_KEYS = ('device_name', 'device_uuid', 'driver_uuid', 'device_luid', 'native_vendor_id', 'native_device_id', 'native_driver_version', 'native_api_version', 'identity_attribution', 'environment')

def record(p):
    return dict(path=p.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest(), bytes=p.stat().st_size)

def fingerprint(root=ROOT):
    files=sorted(list((root/'benchmarks/vulkan_pso').glob('*.cpp'))+list((root/'benchmarks/vulkan_pso').glob('*.hpp'))+[root/n for n in ('benchmarks/vulkan_pso/CMakeLists.txt','benchmarks/vulkan_upload/Identity.hpp','tests/fixtures/SpirvFixtures.hpp','tests/fixtures/spirv/fullscreen.spvasm','tests/fixtures/spirv/red.spvasm','tests/fixtures/spirv/green.spvasm','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/competitors/sources.lock.json')],key=lambda p:p.as_posix())
    return hashlib.sha256(''.join(f'{p.relative_to(root).as_posix()}|{hashlib.sha256(p.read_bytes()).hexdigest()}\n' for p in files).encode()).hexdigest()

def require(condition, message):
    if not condition: raise ValueError(message)

def parse_output(stdout,stderr):
    require(not re.search(rb'Validation Error|SYNC-HAZARD|VUID-[^\s]+.*(?:ERROR|Error)',stdout+b'\n'+stderr),'Native validation error in process output')
    return json.loads(stdout)

def validate_identity(r):
    require(r.get('api')=='vulkan' and r.get('configuration')=='Release' and r.get('category')=='cpu_api' and r.get('contract')==CONTRACT, 'Different API/build/category/contract')
    require(r.get('identity_attribution')=='unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle', 'Unreviewed identity attribution')
    for key, size in (('device_uuid',32),('driver_uuid',32),('device_luid',16)):
        require(isinstance(r.get(key),str) and re.fullmatch('[0-9a-f]{'+str(size)+'}',r[key]) and int(r[key],16)!=0, 'Invalid native '+key)
    e=r['environment']; require(e.get('gpu')==r['device_name'] and e.get('software_renderer') is False and e.get('pointer_bits')==64 and e.get('driver_query_available') is True, 'Unidentified/software/environment mismatch')
    require(e['vendor_id']==r['native_vendor_id'] and e['device_id']==r['native_device_id'], 'Native/DXGI IDs differ')
    high,low=map(int,e['luid'].split(':'));require(struct.pack('<II',low,high&0xffffffff).hex()==r['device_luid'], 'Native/DXGI LUID differs')
    for key in ('source_fingerprint','SDK_source_fingerprint'):
        require(isinstance(r.get(key),str) and re.fullmatch('[0-9a-f]{64}',r[key]),'Missing fingerprint')

def validate_result(r,sdk,scenario,pref,warmup,samples,validation,reference,source,sdk_source,pin):
    validate_identity(r)
    expected=dict(schema_version=1,sdk=sdk,scenario=scenario,preference=pref,warmup=warmup,sample_count=samples,validation=validation,source_fingerprint=source,SDK_source_fingerprint=sdk_source,SDL_commit=pin,probe=False,readback_verified=True,gpu_ms=None)
    require(all(k in r and r[k]==v and type(r[k]) is type(v) for k,v in expected.items()), 'Result metadata differs from requested workload')
    require(all(r.get(k)==reference.get(k) for k in IDENTITY_KEYS),'Device/driver/environment changed')
    w,h,bpp=SHAPES[scenario]
    require((r.get('width'),r.get('height'),r.get('texel_bytes'))==(w,h,bpp),'Different image layout')
    require(r.get('verified_images')==warmup+samples and r.get('verified_bytes')==w*h*bpp*(warmup+samples),'Incomplete full-image verification')
    rows=r.get('samples_ms');require(isinstance(rows,list) and len(rows)==samples,'Wrong sample count')
    for row in rows:
        require(isinstance(row,list) and len(row)==2 and all(type(v) in (int,float) and math.isfinite(v) and v>=0 for v in row) and row[0]>0,'Invalid pipeline CPU interval')
    require(bool(r.get('loaded_validation_layer'))==validation,'Validation layer mode differs')
    count=WORK[scenario]
    expected=dict(requested_instance_api_version=4198400,pipelines_per_iteration=count,created_pipelines=(warmup+samples)*count,released_pipeline_handles=(warmup+samples)*count,used_pipeline_regions=(warmup+samples)*count,shader_header_sha256=hashlib.sha256((ROOT/'tests/fixtures/SpirvFixtures.hpp').read_bytes()).hexdigest(),includes_shader_object_create_release=True,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,cache_state='uncontrolled_driver_cache_repeated_public_creation',columns=list(COLUMNS))
    require(all(k in r and r[k]==v and type(r[k]) is type(v) for k,v in expected.items()),'Different pipeline/timing/cache/release contract')
    return rows


def percentile(values,q):
    require(bool(values) and 0<=q<=1,'Invalid percentile input')
    s=sorted(values);i=(len(s)-1)*q;lo=int(i);hi=min(lo+1,len(s)-1)
    return s[lo]+(s[hi]-s[lo])*(i-lo)

def summarize(runs):
    from run_vulkan_draw import summarize as scalar_statistics
    require(bool(runs),'No process runs')
    return {name:scalar_statistics([dict(samples_ms=[row[i] for row in r['samples_ms']]) for r in runs]) for i,name in enumerate(COLUMNS)}


def run(args):
    from audit_licenses import sdk_fingerprint,verify_checkout
    exe=args.exe.resolve();out=args.output.resolve()
    require(exe.is_file() and not out.exists(),'Executable missing or output already exists')
    require(out.is_relative_to(ROOT),'Output must be inside this repository')
    require(1<=args.warmup<=64 and 3<=args.samples<=256 and 3<=args.repetitions<=10,'Bounded independent processes/samples required')
    lock=json.loads((ROOT/'benchmarks/competitors/sources.lock.json').read_text());pin=lock['sdl']['commit'];verify_checkout(ROOT,'sdl',pin)
    source=fingerprint();sdk_source=sdk_fingerprint(ROOT)
    receipt=json.loads((ROOT/'out/vulkan-validation-runtime/receipt.json').read_text())
    def frozen():
        for r in receipt['artifacts']:require(record(ROOT/r['path'])==r,'Pinned runtime changed')
        require(record(exe)==binary,'Executable changed during run')
    binary=record(exe);dll=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    out.mkdir(parents=True);settings=out/'settings';settings.mkdir()
    (settings/'vk_layer_settings.txt').write_text('khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n',encoding='utf-8')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_VULKAN_'))}
    clean={**env,'VK_LOADER_LAYERS_DISABLE':'~implicit~'}
    validated={**clean,'VK_LAYER_PATH':str(dll.parent),'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','VK_LAYER_SETTINGS_PATH':str(settings),'GFX_EXPECT_VULKAN_LAYER':str(dll),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true'}
    (out/'environment.json').write_text(json.dumps(dict(removed_inherited_keys=sorted(set(os.environ)-set(env)),performance_overrides={k:v for k,v in clean.items() if k not in env},validation_overrides={k:v for k,v in validated.items() if k not in env}),indent=2)+'\n')
    commands=[];records=[]
    def call(name,argv,validation=False,expected=0,overrides=None):
        frozen();start=time.monotonic();child_env={**(validated if validation else clean),**(overrides or {})}
        c=subprocess.run([str(exe),*argv],cwd=ROOT,env=child_env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
        (out/(name+'.stdout')).write_bytes(c.stdout);(out/(name+'.stderr')).write_bytes(c.stderr)
        commands.append(dict(name=name,argv=[str(exe),*argv],validation=validation,expected=expected,returncode=c.returncode,seconds=time.monotonic()-start,overrides=overrides or {}))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        require(c.returncode==expected,name+' failed; inspect raw output')
        if expected: return None
        r=parse_output(c.stdout,c.stderr);frozen();print(name,'PASS',flush=True);return r
    probes={}
    for pref in ('high','low'):
        p=call('probe-'+pref,['--probe','sdl',pref]);validate_identity(p)
        require(p['source_fingerprint']==source and p['SDK_source_fingerprint']==sdk_source and p['SDL_commit']==pin,'Stale probe')
        probes[pref]=p
    # Exact matches only; preferences may select the same device on other machines.
    require(probes['high']['device_uuid']!=probes['low']['device_uuid'],'This two-adapter profile requires distinct high/low devices')
    for pref,probe in probes.items():
        for sdk in ('graphicssdk','sdl'):
            for scenario in SHAPES:
                name=f'validation-{pref}-{sdk}-{scenario}'
                r=call(name,[sdk,scenario,pref,'1','3','1',probe['device_name']],True,overrides={'VK_LOADER_DEBUG':'layer'} if sdk=='sdl' and scenario=='pso1' else None)
                validate_result(r,sdk,scenario,pref,1,3,True,probe,source,sdk_source,pin)
                records.append(dict(name=name,kind='correctness',result=r))
    jobs=[(pref,sdk,scenario,rep) for pref in probes for sdk in ('graphicssdk','sdl') for scenario in SHAPES for rep in range(args.repetitions)]
    random.Random(args.seed).shuffle(jobs)
    for pref,sdk,scenario,rep in jobs:
        name=f'measure-{pref}-{sdk}-{scenario}-{rep}'
        r=call(name,[sdk,scenario,pref,str(args.warmup),str(args.samples),'0',probes[pref]['device_name']])
        validate_result(r,sdk,scenario,pref,args.warmup,args.samples,False,probes[pref],source,sdk_source,pin)
        records.append(dict(name=name,kind='measurement',result=r))
    # Required-layer fallback and changed-device attribution must fail, not produce samples.
    missing=out/'absent-layers';missing.mkdir()
    for sdk in ('graphicssdk','sdl'):
        call('missing-layer-'+sdk,[sdk,'pso1','high','1','3','1',probes['high']['device_name']],True,1,{'VK_LAYER_PATH':str(missing)})
        call('wrong-gpu-'+sdk,[sdk,'pso1','high','1','3','0','absent-exact-GPU'],False,1)
    for n,argv in enumerate(([],['sdl','unknown','high','1','3','0','GPU'],['sdl','pso1','high','-1','3','0','GPU'],['sdl','pso1','high','1','2','0','GPU'],['sdl','pso1','high','1','3','2','GPU'],['sdl','pso1','high','1','3','0','GPU','extra'])):
        call('invalid-option-'+str(n),argv,False,1)
    groups=[]
    for pref in probes:
        for scenario in SHAPES:
            row=dict(preference=pref,scenario=scenario,device_name=probes[pref]['device_name'],device_uuid=probes[pref]['device_uuid'],statistics={})
            for sdk in ('graphicssdk','sdl'):
                data=[r['result'] for r in records if r['kind']=='measurement' and r['result']['preference']==pref and r['result']['scenario']==scenario and r['result']['sdk']==sdk]
                row['statistics'][sdk]=summarize(data)
            row['GraphicsSDK_over_SDL_p50_ratio']={k:row['statistics']['graphicssdk'][k]['p50_ms']/row['statistics']['sdl'][k]['p50_ms'] if row['statistics']['sdl'][k]['p50_ms'] else None for k in COLUMNS};groups.append(row)
    result=dict(schema_version=1,contract=CONTRACT,api='vulkan',category='cpu_api',source_fingerprint=source,SDK_source_fingerprint=sdk_source,SDL_commit=pin,binary=binary,repetitions=args.repetitions,warmup=args.warmup,samples_per_process=args.samples,seed=args.seed,
                probes=probes,groups=groups,runs=records,verified_images=sum(r['result']['verified_images'] for r in records),verified_bytes=sum(r['result']['verified_bytes'] for r in records),validation_timings_excluded=True,samples_removed=0,GPU_timestamp_measurement=False,native_pipeline_count_verified=False,actual_memory_reclamation_verified=False,driver_cache_state_known=False,overall_SDK_ranking=False,identity_native_handle_verified=False)
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    (out/'runtime-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f"PASS: {len(records)} GPU processes, {result['verified_images']} full images, {result['verified_bytes']} verified bytes",flush=True)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--repetitions',type=int,default=3);p.add_argument('--warmup',type=int,default=5);p.add_argument('--samples',type=int,default=30);p.add_argument('--seed',type=int,default=20261026)
    run(p.parse_args())
if __name__=='__main__':main()
