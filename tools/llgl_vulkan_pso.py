"""P3-CMP-05 LLGL PSO lifecycle correctness, native draw observation, and strict input audit."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import time

from prepare_vulkan_comparison import ROOT,read,record,require,save,sha,unchanged,pin,validate_compiler_flags
from audit_licenses import parse_tlog,source_notices
from audit_vulkan import system_input
from llgl_vulkan_upload import candidate_inputs,PIN
from llgl_vulkan_probe import IDENTITY
import check_llgl_transfer_profile as approved

WORK=dict(pso1=1,pso8=8)
CONTRACT='vulkan_pipeline_create_public_release_use_checked_v1'
EXTRA=['benchmarks/llgl_vulkan_upload/Context.hpp','benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/llgl_vulkan_draw/Layer.cpp','benchmarks/vulkan_pso/Frame.hpp','benchmarks/phase3_comparison/pso.json','benchmarks/competitors/sources.lock.json',
       'tests/fixtures/SpirvFixtures.hpp','tests/fixtures/spirv/fullscreen.spvasm','tests/fixtures/spirv/red.spvasm','tests/fixtures/spirv/green.spvasm']

def source_files(root=ROOT):
    folder=root/'benchmarks/llgl_vulkan_pso'
    return sorted(list(folder.glob('*.cpp'))+list(folder.glob('*.hpp'))+[folder/'CMakeLists.txt']+[root/n for n in EXTRA],key=lambda p:p.as_posix())

def fingerprint(root=ROOT):return hashlib.sha256(('profile|llgl-fixed-pin-transfer-src-v1\nworkload|pso\n'+''.join(p.relative_to(root).as_posix()+'|'+sha(p)+'\n' for p in source_files(root))).encode()).hexdigest()

def reference(iteration,count):
    edges=[5+55*i//count for i in range(count+1)]
    def pixel(x,y):
        if not (5<=x<60 and 3<=y<34):return (0,255 if iteration%2 else 0,255,255)
        stripe=next(i for i in range(count) if edges[i]<=x<edges[i+1])
        return (0,255,0,255) if (iteration+stripe)%2 else (255,0,0,255)
    return b''.join(bytes(pixel(x,y)) for y in range(37) for x in range(65))

def validate(row,scenario,validation,fp,shader_sha,identity=None):
    control=scenario in ('core-control','sync-control');count=0 if control else WORK[scenario]
    expected=dict(schema_version=1,task='P3-CMP-05',sdk='llgl',api='vulkan',configuration='Release',profile_id='llgl-fixed-pin-transfer-src-v1',contract=CONTRACT,
        category='cpu_api',source_fingerprint=fp,shader_header_sha256=shader_sha,scenario=scenario,validation=validation,control=control,native_handle_verified=True,native_vendor_id=4318,queue_index=0,
        instance_API_request_observed=True,clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',texture_bind_policy='color_attachment_sampled_copy_src',width=65,height=37,texel_bytes=4,scissor=[5,3,55,31],vertices_per_pipeline=3,first_vertex=0,first_instance=0,
        pipelines_per_iteration=count,created_pipelines=count*4,released_pipeline_handles=count*4,used_pipeline_regions=count*4,created_shader_handles=count*8,released_shader_handles=count*8,
        includes_shader_object_create_release=True,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,native_pipeline_count_verified=False,driver_cache_state_known=False,cache_state='uncontrolled_driver_cache_repeated_public_creation',
        warmup=0 if control else 1,sample_count=0 if control else 3,verified_images=0 if control else 4,verified_bytes=0 if control else 38480,readback_verified=not control,
        native_draw_calls=count*4,native_forwarded_draw_calls=count*4,native_instances=count*4,native_vertices=count*12,native_bad_arguments=0,submitted_control_work=0,
        observer_live_instances_after_close=0,observer_live_devices_after_close=0,performance_samples=0,samples_ms=[],gpu_ms=None,performance_accepted=False,native_workload_shortcuts=False)
    require(all(k in row and type(row[k]) is type(v) and row[k]==v for k,v in expected.items()),'PSO lifecycle, scope, native counts or mode differs')
    for key,length in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:require(re.fullmatch('[0-9a-f]{'+str(length)+'}',row[key]) and int(row[key],16),'Native identity absent')
    for key in ('native_device_id','native_driver_version','native_api_version','queue_family','queue_flags','loader_api_version','observed_instance_api_version'):
        require(type(row[key]) is int and row[key]>=0,'Invalid numeric native identity')
    require(row['queue_flags']&1 and row['observed_instance_api_version']==row['loader_api_version']>=4198400,'Observed instance API/queue differs')
    require(bool(row['loaded_validation_layer'])==validation and row['runtime_modules'] and row['observer_path'],'Loaded validation/observer evidence absent')
    current={k:row[k] for k in IDENTITY};require(identity is None or current==identity,'Device/driver/queue changed')
    if control:
        token='VUID-VkPhysicalDeviceProperties2-sType-sType' if scenario=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE'
        require(validation and row['control_diagnostic']==token and type(row['delivered_errors']) is int and row['delivered_errors']>0 and row['frame_sha256']==[],'Validation control absent')
    else:
        require(type(row['delivered_errors']) is int and row['delivered_errors']==0 and row['control_diagnostic']=='' and row['frame_sha256']==[hashlib.sha256(reference(i,count)).hexdigest() for i in range(4)],'Changed full-frame stripe reference or native validation failed')
    return current

def collect(build,base,candidate):
    approved.validate(approved.read(approved.PROFILE))
    prior=read(ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan/probe/receipt.json')
    require(unchanged(prior['source_inputs']),'Base LLGL/source inputs changed')
    patch=candidate_inputs(candidate,base)
    roots={ROOT/'.deps/LLGL':('llgl',PIN),ROOT/'.deps/Vulkan-Headers':('vulkan_headers',next(c['commit'] for c in read(ROOT/'tools/vulkan-validation.lock.json')['components'] if c['name']=='Vulkan-Headers'))}
    tracked={p:pin(p,commit) for p,(_,commit) in roots.items()}
    logs=[p for p in build.rglob('*.read.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts and p.name in ('CL.read.1.tlog','link.read.1.tlog')]
    commands=[p for p in build.rglob('CL.command.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts]
    require(len(logs)==2 and len(commands)==1,'PSO compiler-linker evidence absent')
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
        elif p in own:kind='own_pso_or_shader_fixture'
        elif p in (base/'vulkan-1.lib',base/'build/llgl/build/Release/LLGL.lib'):kind='audited_import_library'
        elif system_input(p):kind='system_prerequisite'
        else:raise ValueError('Unreviewed PSO compiler/linker input: '+str(p))
        files.append(dict(**record(p),component=kind))
    return dict(files=files,read_logs=[record(p) for p in logs],compiler_commands=[record(p) for p in commands]),patch,prior

def run(build,output,gpu,base,candidate):
    require(os.name=='nt','Windows native draw profile required');build=build.resolve();output=output.resolve();base=base.resolve();candidate=candidate.resolve()
    require(build.is_relative_to(ROOT/'out') and output.is_relative_to(ROOT/'out') and not output.exists(),'Fresh out/ output required')
    inputs,patch,prior=collect(build,base,candidate);output.mkdir(parents=True);save(output/'inputs.json',inputs);save(output/'profile-inputs.json',patch)
    plan=read(ROOT/'benchmarks/phase3_comparison/pso.json');require(plan['work']==WORK and plan['geometry']['scissor']==[5,3,55,31] and plan['timing']['includes']==['public_pipeline_create_including_shader_object_create_release','public_pipeline_handle_release'],'Baseline PSO work drift')
    exe=build/'Release/llgl_vulkan_pso.exe';observer=exe.parent/'VkLayer_gfx_llgl_draw.dll';manifest=exe.parent/'VkLayer_gfx_llgl_draw.json';fp=fingerprint();shader_sha=sha(ROOT/'tests/fixtures/SpirvFixtures.hpp')
    require(fp.encode() in exe.read_bytes(),'Stale native draw executable')
    draw_build=ROOT/'out/phase3-llgl-draw-20261028/build'
    for p in (observer,manifest):require(sha(p)==sha(draw_build/'Release'/p.name),'Previously audited draw observer changed')
    draw_prior=read(ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/native/receipt.json')
    require(unchanged(draw_prior['source_inputs']),'Frozen draw observer/source inputs changed')
    for dll in ('LLGL.dll','LLGL_Vulkan.dll'):require(sha(exe.parent/dll)==sha(candidate/'build/llgl/build/Release'/dll),'Wrong approved DLL copy')
    layer=ROOT/'out/vulkan-validation-runtime/build/Vulkan-ValidationLayers/layers/Release/VkLayer_khronos_validation.dll'
    settings=output/'settings';settings.mkdir();off=output/'sync-off';off.mkdir()
    (settings/'vk_layer_settings.txt').write_text('khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n',encoding='utf-8',newline='\n')
    (off/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = false\nkhronos_validation.syncval_submit_time_validation = false\n',encoding='utf-8',newline='\n')
    base_env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','GFX_','SDL_'))}
    plain=dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(exe.parent),VK_INSTANCE_LAYERS='VK_LAYER_GFX_llgl_draw',VK_LAYER_SETTINGS_PATH=str(off))
    validated={**plain,'VK_LAYER_PATH':str(exe.parent)+os.pathsep+str(layer.parent),'VK_LAYER_SETTINGS_PATH':str(settings),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true'}
    save(output/'environment.json',dict(removed_inherited_keys=sorted(set(os.environ)-set(base_env)),plain=plain,validation=validated))
    save(output/'frame-reference.json',dict(width=65,height=37,scissor=[5,3,55,31],sha256={n:[hashlib.sha256(reference(i,count)).hexdigest() for i in range(4)] for n,count in WORK.items()}))
    paths=source_files()+[pathlib.Path(__file__),exe,observer,manifest,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',layer,build/'CMakeCache.txt',approved.PROFILE,settings/'vk_layer_settings.txt',off/'vk_layer_settings.txt']
    paths += [pathlib.Path(r['path']) for r in prior['source_inputs']+draw_prior['source_inputs']]
    paths += [pathlib.Path(r['path']) for group in ('files','read_logs','compiler_commands') for r in inputs[group]+patch[group]]
    frozen=[record(p) for p in sorted(set(paths))]
    jobs=[(n+'-'+mode,n,mode=='validation',gpu,0,{}) for n in WORK for mode in ('plain','validation')]
    jobs += [('core-control','core-control',True,gpu,0,{}),('sync-control','sync-control',True,gpu,0,{}),
        ('missing-layer','pso1',True,gpu,1,{'VK_LAYER_PATH':str(exe.parent)}),('wrong-target','pso1',False,gpu+' invalid target',1,{}),
        ('missing-pipeline','missing-pipeline',False,gpu,1,{}),('skip-release','skip-release',False,gpu,1,{}),('wrong-pipeline','wrong-pipeline',False,gpu,1,{}),('skip-region','skip-region',False,gpu,1,{}),
        ('wrong-scissor','wrong-scissor',False,gpu,1,{}),('corrupt-readback','corrupt-readback',False,gpu,1,{}),
        ('observer-disabled','pso1',False,gpu,1,{'VK_INSTANCE_LAYERS':''}),('sync-disabled','sync-control',True,gpu,1,{'VK_LAYER_SETTINGS_PATH':str(off),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'false'})]
    tokens={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','missing-pipeline':b'Public pipeline creation count differs','skip-release':b'Public pipeline release count differs','wrong-pipeline':b'Full changing PSO stripe reference mismatch','skip-region':b'Native PSO draw count/arguments differ','wrong-scissor':b'Full changing PSO stripe reference mismatch','corrupt-readback':b'Full changing PSO stripe reference mismatch','observer-disabled':b'Native draw observer not active','sync-disabled':b'Expected validation control diagnostic absent'}
    commands=[];identity=None;runtime={};rows=[]
    try:
        for name,scenario,validation,target,expected,overrides in jobs:
            require(unchanged(frozen),'Inputs changed during PSO validation')
            argv=[str(exe),target,'validation' if validation else 'plain',scenario];start=time.monotonic()
            p=subprocess.run(argv,cwd=exe.parent,env={**base_env,**(validated if validation else plain),**overrides},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90)
            (output/(name+'.stdout')).write_bytes(p.stdout);(output/(name+'.stderr')).write_bytes(p.stderr)
            commands.append(dict(name=name,scenario=scenario,validation=validation,argv=argv,returncode=p.returncode,expected=expected,seconds=time.monotonic()-start,overrides=overrides));save(output/'commands.json',commands)
            require(p.returncode==expected,'Unexpected native PSO exit: '+name)
            if expected:require(not p.stdout and tokens[name] in p.stderr,'Unrelated rejection failure: '+name)
            else:
                row=json.loads(p.stdout);identity=validate(row,scenario,validation,fp,shader_sha,identity);rows.append(row);require(row['device_name']==gpu,'Wrong GPU name')
                for key,wanted in [('loader_path',pathlib.Path(os.environ['WINDIR'])/'System32/vulkan-1.dll'),('observer_path',observer),('LLGL_path',exe.parent/'LLGL.dll'),('backend_path',exe.parent/'LLGL_Vulkan.dll')]:require(pathlib.Path(row[key]).resolve()==wanted.resolve(),'Wrong runtime path: '+key)
                require(not validation or pathlib.Path(row['loaded_validation_layer']).resolve()==layer.resolve(),'Wrong validation DLL')
                if scenario in WORK:require(not re.search(rb'Validation Error|SYNC-HAZARD',p.stderr),'Native PSO validation error')
                for name_path in row['runtime_modules']:
                    path=pathlib.Path(name_path).resolve();require(path.is_relative_to(pathlib.Path(os.environ['WINDIR'])) or path in (exe,observer,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',layer.resolve()),'Unreviewed runtime module')
                    if str(path) not in runtime:runtime[str(path)]=record(path)
            print(name,'PASS',flush=True)
        require(unchanged(frozen),'Frozen PSO inputs changed')
        save(output/'runtime-inputs.json',dict(files=list(runtime.values()),redistributed=False))
        normal=[r for r in rows if not r['control']]
        save(output/'receipt.json',dict(schema_version=1,task='P3-CMP-05',status='pso_correctness_passed',profile_id='llgl-fixed-pin-transfer-src-v1',identity=identity,
            source_fingerprint=fp,shader_header_sha256=shader_sha,commands=commands,source_inputs=frozen,inputs_unchanged=True,verified_images=sum(r['verified_images'] for r in normal),verified_bytes=sum(r['verified_bytes'] for r in normal),
            created_pipelines=sum(r['created_pipelines'] for r in normal),released_pipeline_handles=sum(r['released_pipeline_handles'] for r in normal),used_pipeline_regions=sum(r['used_pipeline_regions'] for r in normal),
            created_shader_handles=sum(r['created_shader_handles'] for r in normal),released_shader_handles=sum(r['released_shader_handles'] for r in normal),
            native_draw_calls=sum(r['native_draw_calls'] for r in normal),native_instances=sum(r['native_instances'] for r in normal),native_vertices=sum(r['native_vertices'] for r in normal),
            observed_instance_api_versions=sorted(set(r['observed_instance_api_version'] for r in rows)),core_and_sync_controls_passed=True,GPU_memory_reclamation_verified=False,native_pipeline_count_verified=False,driver_cache_state_known=False,performance_samples=0,performance_accepted=False,
            observation_scope='Own pass-through layer enabled only for correctness; no timing samples; performance must use separate observer-free processes',next_task='P3-CMP-06'))
    except Exception:
        save(output/'failure.json',dict(task='P3-CMP-05',completed_commands=len(commands),correctness_accepted=False,performance_accepted=False));raise

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-root',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--gpu-name',required=True)
    p.add_argument('--base-build',type=pathlib.Path,default=ROOT/'out/phase3-llgl-vulkan-20261028-2');p.add_argument('--profile-build',type=pathlib.Path,default=ROOT/'out/phase3-llgl-transfer-src-candidate-final');a=p.parse_args();run(a.build_root,a.output,a.gpu_name,a.base_build,a.profile_build)

if __name__=='__main__':main()
