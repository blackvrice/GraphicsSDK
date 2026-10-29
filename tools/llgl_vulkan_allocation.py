"""P3-CMP-06 LLGL resource lifecycle correctness, complete payload references, and strict input audit."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import time
import functools
import check_llgl_pso_profile as prior_profile

from prepare_vulkan_comparison import ROOT,read,record,require,save,sha,unchanged,pin,validate_compiler_flags
from audit_licenses import parse_tlog,source_notices
from audit_vulkan import system_input
from llgl_vulkan_upload import candidate_inputs,PIN
from llgl_vulkan_probe import IDENTITY
import check_llgl_transfer_profile as approved

WORK=dict(buffer1=1,buffer1000=1000,texture1=1,texture1000=1000)
CONTRACT='vulkan_resource_create_public_release_full_readback_v1'
EXTRA=['benchmarks/llgl_vulkan_upload/Context.hpp','benchmarks/llgl_vulkan_pso/Support.hpp','benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/llgl_vulkan_draw/Layer.cpp','benchmarks/vulkan_allocation/Payload.hpp','benchmarks/phase3_comparison/allocation.json','benchmarks/competitors/sources.lock.json']

def source_files(root=ROOT):
    folder=root/'benchmarks/llgl_vulkan_allocation'
    return sorted(list(folder.glob('*.cpp'))+list(folder.glob('*.hpp'))+[folder/'CMakeLists.txt']+[root/n for n in EXTRA],key=lambda p:p.as_posix())

def fingerprint(root=ROOT):return hashlib.sha256(('profile|llgl-fixed-pin-transfer-src-v1\nworkload|allocation\n'+''.join(p.relative_to(root).as_posix()+'|'+sha(p)+'\n' for p in source_files(root))).encode()).hexdigest()

@functools.lru_cache(maxsize=4096)
def reference(iteration,resource):
    # Compute the largest tight payload once. Buffer reference is its prefix.
    mask=0xffffffff;seed=((iteration*2246822519)&mask)^((resource*2654435761)&mask)
    out=bytearray(9620)
    for i in range(len(out)):
        value=((i*3266489917)&mask)^seed;value^=value>>16;value=(value*2246822519)&mask;value^=value>>13;value=(value*3266489917)&mask;value^=value>>16;out[i]=value&255
    return bytes(out)

@functools.lru_cache(maxsize=4)
def expected_hashes(scenario):
    size=9620 if scenario.startswith('texture') else 4096
    return [[hashlib.sha256(reference(i,j)[:size]).hexdigest() for j in range(WORK[scenario])] for i in range(4)]

def validate(row,scenario,validation,fp,identity=None):
    control=scenario in ('core-control','sync-control');count=0 if control else WORK[scenario];texture=scenario.startswith('texture');total=count*4
    expected=dict(schema_version=1,task='P3-CMP-06',sdk='llgl',api='vulkan',configuration='Release',profile_id='llgl-fixed-pin-transfer-src-v1',contract=CONTRACT,
        category='cpu_api',source_fingerprint=fp,scenario=scenario,validation=validation,control=control,native_handle_verified=True,native_vendor_id=4318,queue_index=0,instance_API_request_observed=True,
        resource_kind='none' if control else 'texture' if texture else 'buffer',width=65 if texture else 0,height=37 if texture else 0,texel_bytes=4 if texture else 0,buffer_bytes=4096 if not control and not texture else 0,
        resources_per_iteration=count,created_resources=total,released_resource_handles=total,used_resources=total,verified_resources=total,unique_native_resources_checked=total,native_resource_handles_verified=not control,
        verified_images=total if texture else 0,verified_buffers=total if not texture else 0,verified_bytes=total*(9620 if texture else 4096),readback_verified=not control,
        allocation_policy='public_device_resource_default_policy',usage_policy='none' if control else 'sampled_rgba8_2d' if texture else 'llgl_vertex_buffer_copy_src_dst',buffer_cpu_access_flags=0,initial_data_supplied=False,
        copy_submission_policy='separate_upload_download_completed_fence_idle',staging_in_create_release_boundary=False,row_stride=0,layer_stride=0,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,native_allocation_count_verified=False,allocator_cache_state_known=False,
        warmup=0 if control else 1,sample_count=0 if control else 3,native_draw_calls=0,submitted_control_work=0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,performance_samples=0,samples_ms=[],gpu_ms=None,performance_accepted=False,native_workload_shortcuts=False)
    require(all(k in row and type(row[k]) is type(v) and row[k]==v for k,v in expected.items()),'Resource lifecycle, scope, geometry or mode differs')
    for key,length in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:require(re.fullmatch('[0-9a-f]{'+str(length)+'}',row[key]) and int(row[key],16),'Native identity absent')
    for key in ('native_device_id','native_driver_version','native_api_version','queue_family','queue_flags','loader_api_version','observed_instance_api_version'):
        require(type(row[key]) is int and row[key]>=0,'Invalid numeric native identity')
    require(row['queue_flags']&1 and row['observed_instance_api_version']==row['loader_api_version']>=4198400,'Observed instance API/queue differs')
    require(bool(row['loaded_validation_layer'])==validation and row['runtime_modules'] and row['observer_path'],'Loaded validation/observer evidence absent')
    current={k:row[k] for k in IDENTITY};require(identity is None or current==identity,'Device/driver/queue changed')
    if control:
        token='VUID-VkPhysicalDeviceProperties2-sType-sType' if scenario=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE'
        require(validation and row['control_diagnostic']==token and type(row['delivered_errors']) is int and row['delivered_errors']>0 and row['payload_sha256']==[],'Validation control absent')
    else:
        require(type(row['delivered_errors']) is int and row['delivered_errors']==0 and row['control_diagnostic']=='' and row['payload_sha256']==expected_hashes(scenario),'Full per-resource payload references or native validation failed')
    return current

def collect(build,base,candidate):
    approved.validate(approved.read(approved.PROFILE));prior_profile.validate(prior_profile.read(prior_profile.PROFILE))
    prior=read(ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/probe/receipt.json')
    require(unchanged(prior['source_inputs']),'Base LLGL/source inputs changed')
    patch=candidate_inputs(candidate,base)
    roots={ROOT/'.deps/LLGL':('llgl',PIN),ROOT/'.deps/Vulkan-Headers':('vulkan_headers',next(c['commit'] for c in read(ROOT/'tools/vulkan-validation.lock.json')['components'] if c['name']=='Vulkan-Headers'))}
    tracked={p:pin(p,commit) for p,(_,commit) in roots.items()}
    logs=[p for p in build.rglob('*.read.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts and p.name in ('CL.read.1.tlog','link.read.1.tlog')]
    commands=[p for p in build.rglob('CL.command.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts]
    require(len(logs)==2 and len(commands)==1,'Resource compiler-linker evidence absent')
    for p in commands:validate_compiler_flags(p.read_bytes().decode('utf-16'))
    own=set(source_files());files=[]
    for raw in sorted(set(x for p in logs for x in parse_tlog(p.read_bytes())),key=str.casefold):
        p=pathlib.Path(raw).resolve()
        if p.suffix.lower() in ('.obj','.pch','.pdb','.tlog'):continue
        kind=None
        for folder,(sdk,commit) in roots.items():
            if p.is_relative_to(folder):
                require(p.relative_to(folder).as_posix().casefold() in tracked[folder],'Untracked dependency read')
                text=p.read_text(encoding='utf-8-sig');require('BSD' in '\n'.join(source_notices(text)) if sdk=='llgl' else 'SPDX-License-Identifier: Apache-2.0' in text,'Unreviewed header license');kind=sdk;break
        if kind:pass
        elif p in own:kind='own_resource_or_payload_fixture'
        elif p in (base/'vulkan-1.lib',base/'build/llgl/build/Release/LLGL.lib'):kind='audited_import_library'
        elif system_input(p):kind='system_prerequisite'
        else:raise ValueError('Unreviewed Resource compiler/linker input: '+str(p))
        files.append(dict(**record(p),component=kind))
    return dict(files=files,read_logs=[record(p) for p in logs],compiler_commands=[record(p) for p in commands]),patch,prior

def run(build,output,gpu,base,candidate):
    require(os.name=='nt','Windows native allocation profile required');build=build.resolve();output=output.resolve();base=base.resolve();candidate=candidate.resolve()
    require(build.is_relative_to(ROOT/'out') and output.is_relative_to(ROOT/'out') and not output.exists(),'Fresh out/ output required')
    inputs,patch,prior=collect(build,base,candidate);output.mkdir(parents=True);save(output/'inputs.json',inputs);save(output/'profile-inputs.json',patch)
    plan=read(ROOT/'benchmarks/phase3_comparison/allocation.json');require(plan['work']==WORK and plan['resources']['buffer_bytes']==4096 and plan['resources']['texture_shape']==[65,37,4] and plan['timing']['includes']==['public_device_resource_create','public_resource_handle_release'],'Baseline resource work drift')
    exe=build/'Release/llgl_vulkan_allocation.exe';observer=exe.parent/'VkLayer_gfx_llgl_draw.dll';manifest=exe.parent/'VkLayer_gfx_llgl_draw.json';fp=fingerprint()

    require(fp.encode() in exe.read_bytes(),'Stale native resource executable')
    draw_build=ROOT/'out/phase3-llgl-draw-20261028/build'
    for p in (observer,manifest):require(sha(p)==sha(draw_build/'Release'/p.name),'Previously audited draw observer changed')
    draw_prior=read(ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/native/receipt.json')
    require(unchanged(draw_prior['source_inputs']),'Frozen draw observer/source inputs changed')
    pso_prior=read(ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/native/receipt.json')
    require(unchanged(pso_prior['source_inputs']),'Frozen PSO source/artifact inputs changed')
    for dll in ('LLGL.dll','LLGL_Vulkan.dll'):require(sha(exe.parent/dll)==sha(candidate/'build/llgl/build/Release'/dll),'Wrong approved DLL copy')
    layer=ROOT/'out/vulkan-validation-runtime/build/Vulkan-ValidationLayers/layers/Release/VkLayer_khronos_validation.dll'
    settings=output/'settings';settings.mkdir();off=output/'sync-off';off.mkdir()
    (settings/'vk_layer_settings.txt').write_text('khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n',encoding='utf-8',newline='\n')
    (off/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = false\nkhronos_validation.syncval_submit_time_validation = false\n',encoding='utf-8',newline='\n')
    base_env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','GFX_','SDL_'))}
    plain=dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(exe.parent),VK_INSTANCE_LAYERS='VK_LAYER_GFX_llgl_draw',VK_LAYER_SETTINGS_PATH=str(off))
    validated={**plain,'VK_LAYER_PATH':str(exe.parent)+os.pathsep+str(layer.parent),'VK_LAYER_SETTINGS_PATH':str(settings),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true'}
    save(output/'environment.json',dict(removed_inherited_keys=sorted(set(os.environ)-set(base_env)),plain=plain,validation=validated))
    save(output/'payload-reference.json',dict(buffer_bytes=4096,texture_shape=[65,37,4],sha256={n:expected_hashes(n) for n in WORK}))
    paths=source_files()+[pathlib.Path(__file__),exe,observer,manifest,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',layer,build/'CMakeCache.txt',approved.PROFILE,settings/'vk_layer_settings.txt',off/'vk_layer_settings.txt']
    paths += [pathlib.Path(r['path']) for r in prior['source_inputs']+draw_prior['source_inputs']+pso_prior['source_inputs']]
    paths += [pathlib.Path(r['path']) for group in ('files','read_logs','compiler_commands') for r in inputs[group]+patch[group]]
    frozen=[record(p) for p in sorted(set(paths))]
    jobs=[(n+'-'+mode,n,mode=='validation',gpu,0,{}) for n in WORK for mode in ('plain','validation')]
    jobs += [('core-control','core-control',True,gpu,0,{}),('sync-control','sync-control',True,gpu,0,{}),
        ('missing-layer','buffer1',True,gpu,1,{'VK_LAYER_PATH':str(exe.parent)}),('wrong-target','buffer1',False,gpu+' invalid target',1,{})]
    jobs += [(n,n,False,gpu,1,{}) for n in ('missing-resource','alias-resource','skip-release','wrong-resource-payload','stale-iteration','skip-copy','corrupt-readback','short-readback')]
    jobs += [('observer-disabled','buffer1',False,gpu,1,{'VK_INSTANCE_LAYERS':''}),('sync-disabled','sync-control',True,gpu,1,{'VK_LAYER_SETTINGS_PATH':str(off),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'false'})]
    tokens={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','missing-resource':b'Public resource creation count differs','alias-resource':b'Duplicate live public resource handle','skip-release':b'Public resource release count differs','wrong-resource-payload':b'Full changing resource readback mismatch','stale-iteration':b'Full changing resource readback mismatch','skip-copy':b'Full changing resource readback mismatch','corrupt-readback':b'Full changing resource readback mismatch','short-readback':b'Incomplete full resource readback','observer-disabled':b'Native draw observer not active','sync-disabled':b'Expected validation control diagnostic absent'}
    commands=[];identity=None;runtime={};rows=[]
    try:
        for name,scenario,validation,target,expected,overrides in jobs:
            require(unchanged(frozen),'Inputs changed during resource validation')
            argv=[str(exe),target,'validation' if validation else 'plain',scenario];start=time.monotonic()
            p=subprocess.run(argv,cwd=exe.parent,env={**base_env,**(validated if validation else plain),**overrides},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90)
            (output/(name+'.stdout')).write_bytes(p.stdout);(output/(name+'.stderr')).write_bytes(p.stderr)
            commands.append(dict(name=name,scenario=scenario,validation=validation,argv=argv,returncode=p.returncode,expected=expected,seconds=time.monotonic()-start,overrides=overrides));save(output/'commands.json',commands)
            require(p.returncode==expected,'Unexpected native resource exit: '+name)
            if expected:require(not p.stdout and tokens[name] in p.stderr,'Unrelated rejection failure: '+name)
            else:
                row=json.loads(p.stdout);identity=validate(row,scenario,validation,fp,identity);rows.append(row);require(row['device_name']==gpu,'Wrong GPU name')
                for key,wanted in [('loader_path',pathlib.Path(os.environ['WINDIR'])/'System32/vulkan-1.dll'),('observer_path',observer),('LLGL_path',exe.parent/'LLGL.dll'),('backend_path',exe.parent/'LLGL_Vulkan.dll')]:require(pathlib.Path(row[key]).resolve()==wanted.resolve(),'Wrong runtime path: '+key)
                require(not validation or pathlib.Path(row['loaded_validation_layer']).resolve()==layer.resolve(),'Wrong validation DLL')
                if scenario in WORK:require(not re.search(rb'Validation Error|SYNC-HAZARD',p.stderr),'Native resource validation error')
                for name_path in row['runtime_modules']:
                    path=pathlib.Path(name_path).resolve();require(path.is_relative_to(pathlib.Path(os.environ['WINDIR'])) or path in (exe,observer,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',layer.resolve()),'Unreviewed runtime module')
                    if str(path) not in runtime:runtime[str(path)]=record(path)
            print(name,'PASS',flush=True)
        require(unchanged(frozen),'Frozen resource inputs changed')
        save(output/'runtime-inputs.json',dict(files=list(runtime.values()),redistributed=False))
        normal=[r for r in rows if not r['control']]
        save(output/'receipt.json',dict(schema_version=1,task='P3-CMP-06',status='allocation_correctness_passed',profile_id='llgl-fixed-pin-transfer-src-v1',identity=identity,
            source_fingerprint=fp,commands=commands,source_inputs=frozen,inputs_unchanged=True,verified_images=sum(r['verified_images'] for r in normal),verified_bytes=sum(r['verified_bytes'] for r in normal),
            **{k:sum(r[k] for r in normal) for k in ('created_resources','released_resource_handles','used_resources','verified_resources','unique_native_resources_checked','verified_buffers')},
            observed_instance_api_versions=sorted(set(r['observed_instance_api_version'] for r in rows)),core_and_sync_controls_passed=True,GPU_memory_reclamation_verified=False,native_allocation_count_verified=False,allocator_cache_state_known=False,performance_samples=0,performance_accepted=False,
            observation_scope='Reused pass-through observer checks instance API and native instance/device teardown only; resource identity queried separately; no native memory allocation counts or performance samples',next_task='P3-CMP-07'))
    except Exception:
        save(output/'failure.json',dict(task='P3-CMP-06',completed_commands=len(commands),correctness_accepted=False,performance_accepted=False));raise

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-root',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--gpu-name',required=True)
    p.add_argument('--base-build',type=pathlib.Path,default=ROOT/'out/phase3-llgl-vulkan-20261028-2');p.add_argument('--profile-build',type=pathlib.Path,default=ROOT/'out/phase3-llgl-transfer-src-candidate-final');a=p.parse_args();run(a.build_root,a.output,a.gpu_name,a.base_build,a.profile_build)

if __name__=='__main__':main()
