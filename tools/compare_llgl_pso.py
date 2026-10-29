"""P3-CMP-09: fresh serialized three-SDK Vulkan PSO creation/public-release measurements."""
import argparse
import hashlib
import itertools
import json
import math
import os
import pathlib
import random
import re
import struct
import subprocess
import time

import prepare_vulkan_comparison as inputs
import run_vulkan_comparison as protocol
import run_vulkan_pso as sdk_pso
import llgl_vulkan_upload as upload
import check_llgl_draw_comparison as approved
from prepare_vulkan_comparison import ROOT,read,save,sha,record,require,unchanged
from audit_licenses import parse_tlog,source_notices
from audit_vulkan import system_input

PLAN=ROOT/'benchmarks/phase3_comparison/competitors/llgl-pso-comparison.json'
REFERENCE=ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/native/frame-reference.json'
SDKS=('graphicssdk','sdl','llgl')
SHAPES=sdk_pso.SHAPES
COMMON=('device_name','device_uuid','driver_uuid','device_luid','native_vendor_id','native_device_id','native_driver_version','native_api_version','environment')
EXTRA=['benchmarks/llgl_vulkan_upload/Context.hpp', 'benchmarks/llgl_vulkan_pso/Support.hpp', 'benchmarks/llgl_vulkan_draw/Observer.hpp', 'benchmarks/llgl_vulkan_draw/Layer.cpp', 'benchmarks/llgl_vulkan_pso/main.cpp', 'benchmarks/vulkan_pso/Frame.hpp', 'benchmarks/vulkan_pso/main.cpp', 'benchmarks/common/Adapter.hpp', 'benchmarks/common/Environment.cpp', 'benchmarks/competitors/sources.lock.json', 'benchmarks/phase3_comparison/pso.json', 'benchmarks/phase3_comparison/competitors/llgl-pso-comparison.json', 'tests/fixtures/SpirvFixtures.hpp', 'tests/fixtures/spirv/fullscreen.spvasm', 'tests/fixtures/spirv/red.spvasm', 'tests/fixtures/spirv/green.spvasm']
COLUMNS=['create_ms','public_release_ms']
SUCCESS=('probe','observe','audit','validation','core-control','sync-control','measurement')

def source_files(root=ROOT):
    p=root/'benchmarks/llgl_vulkan_pso_compare'
    return sorted([*p.glob('*.cpp'),*p.glob('*.hpp'),p/'CMakeLists.txt',*[root/n for n in EXTRA]],key=lambda p:p.as_posix())

def fingerprint(root=ROOT):return hashlib.sha256(('profile|llgl-fixed-pin-transfer-src-v1\nworkload|pso_compare\n'+''.join(p.relative_to(root).as_posix()+'|'+sha(p)+'\n' for p in source_files(root))).encode()).hexdigest()

def exact(row,expected,message):
    require(all(k in row and type(row[k]) is type(v) and json.dumps(row[k],sort_keys=True,allow_nan=False)==json.dumps(v,sort_keys=True,allow_nan=False) for k,v in expected.items()),message)

def validate_plan(p):
    old=read(ROOT/'benchmarks/phase3_comparison/pso.json')
    exact(p,dict(schema_version=1,task='P3-CMP-09',profile='pso',profile_id='llgl-fixed-pin-transfer-src-v1',sdks=list(SDKS),scenarios=list(SHAPES),validation=old['validation'],work=old['work'],geometry=old['geometry'],timing=old['timing'],
        performance=dict(warmup=5,samples=30,repetitions=5,seed=20261027,order='seeded_scenario_blocks_five_distinct_three_SDK_permutations'),statistics=old['statistics'],serialized_processes=True,observer_free_performance_required=True,
        historical_timings_reused=False,overall_SDK_ranking=False,SDL_native_handle_verified=False,native_allocation_count_verified=False,GPU_memory_reclamation_verified=False,next_task='P3-CMP-10',cache_state=old['cache_state'],native_pipeline_count_verified=False,driver_cache_state_known=False),'PSO plan coverage/repetition/scope drift')
    return p

def schedule(p):
    jobs=[dict(kind='probe',sdk=s,scenario='probe',repetition=0) for s in SDKS]
    jobs += [dict(kind='observe',sdk='llgl',scenario='probe',repetition=0)]
    jobs += [dict(kind='audit',sdk='llgl',scenario=n,repetition=0) for n in SHAPES]
    jobs += [dict(kind='validation',sdk=s,scenario=n,repetition=0) for n in SHAPES for s in SDKS]
    jobs += [dict(kind=k,sdk=s,scenario='pso1',repetition=0) for s in SDKS for k in ('missing-layer','wrong-target')]
    jobs += [dict(kind=k,sdk='llgl',scenario=n,repetition=0) for k,n in [('core-control','core-control'),('sync-control','sync-control'),('sync-disabled','sync-control'),('missing-pipeline','missing-pipeline'),('skip-release','skip-release'),('wrong-pipeline','wrong-pipeline'),('skip-region','skip-region'),('wrong-scissor','wrong-scissor'),('corrupt-readback','corrupt-readback'),('observer-disabled','probe')]]
    rng=random.Random(p['performance']['seed']);orders={}
    for n in SHAPES:
        orders[n]=list(itertools.permutations(SDKS));rng.shuffle(orders[n])
    for rep in range(5):
        names=list(SHAPES);rng.shuffle(names)
        for n in names:jobs += [dict(kind='measurement',sdk=s,scenario=n,repetition=rep) for s in orders[n][rep]]
    for i,j in enumerate(jobs):j['name']=f"{i:03}-{j['kind']}-{j['sdk']}-{j['scenario']}-{j['repetition']}"
    return jobs

def options(job):
    k=job['kind'];validation=k in ('validation','missing-layer','core-control','sync-control','sync-disabled')
    mode='observe' if k in ('observe','audit','missing-pipeline','skip-release','wrong-pipeline','skip-region','wrong-scissor','corrupt-readback','observer-disabled') else 'validation' if validation else 'plain'
    w,n=(0,0) if k in ('probe','observe','observer-disabled','core-control','sync-control','sync-disabled') else (5,30) if k=='measurement' else (1,3)
    return validation,mode,w,n

def arguments(job,r):
    validation,mode,w,n=options(job);gpu='GraphicsSDK CMP09 absent GPU' if job['kind']=='wrong-target' else r['expected_gpu'];exe=r['binaries']['llgl' if job['sdk']=='llgl' else 'sdk_sdl']['path']
    if job['sdk']=='llgl':return [exe,gpu,mode,job['scenario'],str(w),str(n)]
    if job['kind']=='probe':return [exe,'--probe',job['sdk'],'high',gpu]
    return [exe,job['sdk'],job['scenario'],'high',str(w),str(n),str(int(validation)),gpu]

def overrides(job,r):
    validation,mode,_,_=options(job);env=dict(r['validation_overrides'] if validation else r['performance_overrides']);out=r['output_directory'];layer=r['validation_library']
    if validation and job['sdk']=='llgl':env.update(VK_LAYER_PATH=r['observer_directory']+';'+layer.rsplit('/',1)[0],VK_INSTANCE_LAYERS='VK_LAYER_GFX_llgl_draw')
    if mode=='observe':env.update(VK_LAYER_PATH=r['observer_directory'],VK_INSTANCE_LAYERS='VK_LAYER_GFX_llgl_draw')
    if job['kind']=='missing-layer':env['VK_LAYER_PATH']=out+'/missing'
    if job['kind']=='sync-disabled':env.update(VK_LAYER_SETTINGS_PATH=out+'/sync-off',VK_KHRONOS_VALIDATION_VALIDATE_SYNC='false')
    if job['kind']=='observer-disabled':env['VK_INSTANCE_LAYERS']=''
    return env

def validate_common(row,reference=None):
    for k,n in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:require(isinstance(row.get(k),str) and re.fullmatch('[0-9a-f]{'+str(n)+'}',row[k]) and int(row[k],16),'Invalid hardware identity')
    for k in ('native_vendor_id','native_device_id','native_driver_version','native_api_version'):require(type(row[k]) is int and row[k]>0,'Invalid numeric native identity')
    e=row['environment'];require(e['gpu']==row['device_name'] and e['vendor_id']==row['native_vendor_id']==4318 and e['device_id']==row['native_device_id'] and e['software_renderer'] is False and e['driver_query_available'] is True and e['pointer_bits']==64,'Native/DXGI environment mismatch')
    high,low=map(int,e['luid'].split(':'));require(struct.pack('<II',low,high&0xffffffff).hex()==row['device_luid'],'Actual native/DXGI LUID differs')
    current={k:row[k] for k in COMMON};require(reference is None or current==reference,'Current device/driver/LUID/CPU/environment differs');return current

def validate_output(job,row,r):
    validation,mode,w,n=options(job);kind=job['kind'];probe=kind in ('probe','observe');diagnostic=kind in ('core-control','sync-control');special=probe or diagnostic;observe=job['sdk']=='llgl' and mode!='plain'
    exact(row,dict(schema_version=1,sdk=job['sdk'],api='vulkan',configuration='Release',category='cpu_api',contract=sdk_pso.CONTRACT,scenario=job['scenario'],validation=validation,probe=probe,warmup=w,sample_count=n,gpu_ms=None),'Workload/build/interval mode differs')
    require(row['device_name']==r['expected_gpu'],'Wrong current GPU');identity=validate_common(row,r.get('identity'));count=0 if special else sdk_pso.WORK[job['scenario']];images=0 if special else w+n
    exact(row,dict(width=65,height=37,texel_bytes=4,pipelines_per_iteration=count,created_pipelines=images*count,released_pipeline_handles=images*count,used_pipeline_regions=images*count,verified_images=images,verified_bytes=9620*images,readback_verified=not special,shader_header_sha256=r['shader_header_sha256'],columns=COLUMNS,includes_shader_object_create_release=True,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,cache_state='uncontrolled_driver_cache_repeated_public_creation'),'Incomplete public PSO/shader/readback/timing policy')
    if job['sdk']!='llgl':
        exact(row,dict(source_fingerprint=r['SDK_pso_fingerprint'],SDK_source_fingerprint=r['SDK_source_fingerprint'],SDL_commit=r['SDL_commit'],preference='high',requested_instance_api_version=4198400,identity_attribution='unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle'),'SDK/SDL source identity differs')
    else:
        exact(row,dict(task='P3-CMP-09',profile_id='llgl-fixed-pin-transfer-src-v1',source_fingerprint=r['LLGL_pso_fingerprint'],identity_attribution='actual_LLGL_native_handles_and_unique_DXGI',control=diagnostic,native_handle_verified=True,queue_index=0,instance_API_request_observed=observe,observer_enabled=observe,observed_instance_api_version=r.get('observed_LLGL_instance_api',row['loader_api_version']) if observe else 0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,native_workload_shortcuts=False,submitted_control_work=0,requested_instance_api_version=None,clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',texture_bind_policy='color_attachment_sampled_copy_src',scissor=[5,3,55,31],vertices_per_pipeline=3,first_vertex=0,first_instance=0,created_shader_handles=images*count*2,released_shader_handles=images*count*2,native_pipeline_count_verified=False,driver_cache_state_known=False),'LLGL profile/observation/public lifecycle differs')
        require(type(row['queue_family']) is int and type(row['queue_flags']) is int and row['queue_flags']&1,'LLGL actual graphics queue absent')
        if diagnostic:
            token='VUID-VkPhysicalDeviceProperties2-sType-sType' if kind=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE';require(row['control_diagnostic']==token and type(row['delivered_errors']) is int and row['delivered_errors']>0,'Required validation diagnostic absent')
        else:exact(row,dict(control_diagnostic='',delivered_errors=0),'LLGL native validation error')
        require(row['frame_sha256']==([] if special or not observe else r['reference_hashes'][job['scenario']]),'LLGL complete changing reference differs')
        native=dict(native_draw_calls=images*count,native_forwarded_draw_calls=images*count,native_instances=images*count,native_vertices=3*images*count,native_bad_arguments=0,native_min_instances=1 if images else 0,native_max_instances=1 if images else 0)
        exact(row,{k:v if observe else None for k,v in native.items()},'Native PSO use observation differs')
        modules=row['runtime_modules'];require(modules,'Runtime module evidence absent');require(any('vklayer_gfx_llgl_draw.dll' in p.casefold() for p in modules)==observe,'Observer timing contamination/absence');require(any('vklayer_khronos_validation.dll' in p.casefold() for p in modules)==validation,'Validation timing contamination/absence')
    values=row['samples_ms'];require(isinstance(values,list) and len(values)==(0 if special else n),'Invalid PSO sample count')
    for t in values:require(isinstance(t,list) and len(t)==2 and all(type(v) in (int,float) and math.isfinite(v) and v>=0 for v in t) and t[0]>0,'Invalid create/public-release intervals')
    require(bool(row['loaded_validation_layer'])==validation,'Validation/performance process contamination')
    if validation:require(protocol.normalized_path(row['loaded_validation_layer'])==protocol.normalized_path(r['validation_library']),'Wrong validation runtime')
    return identity

def validate_rejection(job,code,out,err):
    require(type(code) is int and code==1 and not out,'Control did not reject without samples');kind=job['kind']
    if job['sdk']!='llgl':
        token='VK_LAYER_KHRONOS_validation is not installed' if kind=='missing-layer' else 'FAIL: Selected GPU differs from expected GPU' if job['sdk']=='sdl' else 'FAIL: Unique exact SDK GPU required'
        lines=err.decode(errors='replace').strip().splitlines();require(token.encode() in err if kind=='missing-layer' else bool(lines) and lines[-1]==token,'Unrelated SDK/SDL failure accepted');return
    token={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','sync-disabled':b'Expected validation control diagnostic absent','missing-pipeline':b'Public pipeline creation count differs','skip-release':b'Public pipeline release count differs','wrong-pipeline':b'Full changing PSO stripe reference mismatch','skip-region':b'Native PSO draw count/arguments differ','wrong-scissor':b'Full changing PSO stripe reference mismatch','corrupt-readback':b'Full changing PSO stripe reference mismatch','observer-disabled':b'Native draw observer not active'}[kind]
    require(token in err,'Unrelated failure accepted as negative control')

def summarize(rows):
    groups=[]
    for n in SHAPES:
        for s in SDKS:
            selected=[x for x in rows if x['job']['kind']=='measurement' and x['job']['scenario']==n and x['job']['sdk']==s]
            require(len(selected)==5 and [x['job']['repetition'] for x in selected]==list(range(5)),'Missing/duplicate independent process')
            for i,column in enumerate(COLUMNS):groups.append(dict(scenario=n,sdk=s,shape=list(SHAPES[n]),category='cpu_api',column=column,statistics=protocol.scalar_stats([[t[i] for t in x['result']['samples_ms']] for x in selected])))
    return groups

def collect(build,base,candidate):
    approved.validate(approved.read(approved.PROFILE));patch=upload.candidate_inputs(candidate,base)
    roots={ROOT/'.deps/LLGL':('llgl',upload.PIN),ROOT/'.deps/Vulkan-Headers':('vulkan_headers',read(ROOT/'licenses/vulkan-presentation-policy.json')['dependency']['commit'])};tracked={p:inputs.pin(p,pin) for p,(_,pin) in roots.items()}
    logs=[p for p in build.rglob('*.read.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts and p.name in ('CL.read.1.tlog','link.read.1.tlog')];commands=[p for p in build.rglob('CL.command.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts];require(len(logs)==2 and len(commands)==1,'Compiler/linker evidence absent')
    for p in commands:inputs.validate_compiler_flags(p.read_bytes().decode('utf-16'))
    own=set(source_files());files=[]
    for raw in sorted(set(x for p in logs for x in parse_tlog(p.read_bytes())),key=str.casefold):
        p=pathlib.Path(raw).resolve()
        if p.suffix.lower() in ('.obj','.pch','.pdb','.tlog'):continue
        kind=None
        for folder,(sdk,pin) in roots.items():
            if p.is_relative_to(folder):
                require(p.relative_to(folder).as_posix().casefold() in tracked[folder],'Untracked dependency read');text=p.read_text(encoding='utf-8-sig');require('BSD' in '\n'.join(source_notices(text)) if sdk=='llgl' else 'SPDX-License-Identifier: Apache-2.0' in text,'Unreviewed header license');kind=sdk;break
        if kind:pass
        elif p in own:kind='own_pso_context_environment'
        elif p in (base/'vulkan-1.lib',base/'build/llgl/build/Release/LLGL.lib'):kind='audited_import_library'
        elif system_input(p):kind='system_prerequisite'
        else:raise ValueError('Unreviewed comparison compiler/linker input: '+str(p))
        files.append(dict(**record(p),component=kind))
    return dict(files=files,read_logs=[record(p) for p in logs],compiler_commands=[record(p) for p in commands]),patch

def check(folder,live=False):
    r=read(folder/'receipt.json');p=validate_plan(read(folder/'plan.json'));exact(r,dict(task='P3-CMP-09',status='completed',inputs_unchanged=True,performance_accepted=True,historical_timings_reused=False,overall_SDK_ranking=False,SDL_native_handle_verified=False,measurement_samples=900,verified_images=1082,verified_bytes=10408840),'Incomplete or promoted comparison')
    require(r['plan_sha256']==sha(folder/'plan.json') and r['environment_sha256']==sha(folder/'environment.json'),'Plan/environment changed');environment=read(folder/'environment.json');require(environment['performance_overrides']==r['performance_overrides'] and environment['validation_overrides']==r['validation_overrides'],'Environment binding differs')
    exact(r['performance_overrides'],dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=r['output_directory']+'/empty',VK_LAYER_SETTINGS_PATH=r['output_directory']+'/empty'),'Performance layer isolation changed');require(set(r['performance_overrides'])=={'VK_LOADER_LAYERS_DISABLE','VK_LAYER_PATH','VK_LAYER_SETTINGS_PATH'},'Extra performance layer setting')
    require((folder/'settings/vk_layer_settings.txt').read_text()==protocol.SETTINGS,'Required sync settings differ')
    require(r['reference_hashes']==read(folder/'frame-reference.json')['sha256'],'CPU references changed')
    jobs=schedule(p);require(len(r['runs'])==len(jobs)==58,'Missing/extra processes');end=0;images=0;bytecount=0
    for row,job in zip(r['runs'],jobs):
        require(row['job']==job and row['argv']==arguments(job,r) and row['overrides']==overrides(job,r),'Schedule/arguments/environment changed');require(type(row['pid']) is int and row['pid']>0 and type(row['started']) in (int,float) and end<=row['started']<=row['finished'],'Overlapping/nonprocess execution');end=row['finished']
        out=(folder/row['stdout']).read_bytes();err=(folder/row['stderr']).read_bytes();require(sha(folder/row['stdout'])==row['stdout_sha256'] and sha(folder/row['stderr'])==row['stderr_sha256'],'Raw output changed')
        if job['kind'] in SUCCESS:
            require(row['returncode']==0,'Successful native exit required');data=json.loads(out);require(data==row['result'],'Parsed/raw result differs');validate_output(job,data,r)
            if job['kind'] in ('audit','validation','measurement'):
                require(not re.search(rb'Validation Error|SYNC-HAZARD',err),'Native workload error');images+=data['verified_images'];bytecount+=data['verified_bytes']
                require(sum(sum(t) for t in data['samples_ms'])/1000<=row['finished']-row['started'],'Samples exceed enclosing process')
        else:validate_rejection(job,row['returncode'],out,err)
    require(images==1082 and bytecount==10408840 and r['groups']==summarize(r['runs']),'Readback totals/statistics differ')
    if live:require(unchanged(r['source_inputs']),'Comparison inputs changed')
    return dict(task='P3-CMP-09',processes=58,measurement_samples=900,verified_images=1082,verified_bytes=bytecount,performance_accepted=True,overall_SDK_ranking=False)

def run(build,prepared,out,gpu):
    require(os.name=='nt','Windows native comparison required');p=validate_plan(read(PLAN));out=out.resolve();build=build.resolve();prepared=prepared.resolve();require(out.is_relative_to(ROOT/'out') and not out.exists(),'Fresh output under out/ required');inputs.check(prepared,live=True);pr=read(prepared/'receipt.json')
    base=ROOT/'out/phase3-llgl-vulkan-20261028-2';candidate=ROOT/'out/phase3-llgl-transfer-src-candidate-final';audit,patch=collect(build,base,candidate);runtime,layer=protocol.runtime_inputs(ROOT/'out/vulkan-validation-runtime/receipt.json');exe=build/'Release/llgl_vulkan_pso_compare.exe';fp=fingerprint();require(fp.encode() in exe.read_bytes(),'Stale comparison executable')
    for name in ('LLGL.dll','LLGL_Vulkan.dll'):require(sha(exe.parent/name)==sha(candidate/'build/llgl/build/Release'/name),'Wrong approved LLGL DLL')
    for name in ('VkLayer_gfx_llgl_draw.dll','VkLayer_gfx_llgl_draw.json'):require(sha(exe.parent/name)==sha(ROOT/'out/phase3-llgl-draw-20261028/build/Release'/name),'Changed audited observer')
    prior=read(ROOT/'benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/native/receipt.json');require(unchanged(prior['source_inputs']),'Prior LLGL inputs changed')
    out.mkdir(parents=True)
    for n in ('empty','missing','settings','sync-off','commands'):(out/n).mkdir()
    (out/'settings/vk_layer_settings.txt').write_text(protocol.SETTINGS,encoding='utf-8',newline='\n');(out/'sync-off/vk_layer_settings.txt').write_text('khronos_validation.validate_sync = false\nkhronos_validation.syncval_submit_time_validation = false\n',encoding='utf-8',newline='\n')
    save(out/'plan.json',p);save(out/'inputs.json',audit);save(out/'profile-inputs.json',patch);save(out/'prepared-receipt.json',pr);save(out/'prepared-inputs.json',read(prepared/'inputs.json'));save(out/'runtime-receipt.json',runtime);save(out/'frame-reference.json',read(REFERENCE))
    clean={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_'))};performance=dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=out.as_posix()+'/empty',VK_LAYER_SETTINGS_PATH=out.as_posix()+'/empty');validated={**performance,'VK_LAYER_PATH':layer.parent.as_posix(),'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','VK_LAYER_SETTINGS_PATH':out.as_posix()+'/settings','GFX_EXPECT_VULKAN_LAYER':layer.as_posix(),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true','VK_LOADER_DEBUG':'layer'}
    save(out/'environment.json',dict(removed_inherited_keys=sorted(set(os.environ)-set(clean)),performance_overrides=performance,validation_overrides=validated))
    paths=source_files()+inputs.source_paths()+[pathlib.Path(__file__),PLAN,REFERENCE,approved.PROFILE,exe,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',exe.parent/'VkLayer_gfx_llgl_draw.dll',exe.parent/'VkLayer_gfx_llgl_draw.json',layer,build/'CMakeCache.txt',out/'settings/vk_layer_settings.txt',out/'sync-off/vk_layer_settings.txt']
    paths += [pathlib.Path(r['path']) for r in read(prepared/'inputs.json')['files']+prior['source_inputs']]
    paths += [pathlib.Path(r['path']) for group in ('files','read_logs','compiler_commands') for r in audit[group]+patch[group]]
    frozen=[record(x) for x in sorted(set(paths))];r=dict(task='P3-CMP-09',status='running',source_commit=inputs.git(ROOT,'rev-parse','HEAD'),expected_gpu=gpu,output_directory=out.as_posix(),observer_directory=exe.parent.as_posix(),validation_library=layer.as_posix(),validation_library_sha256=sha(layer),plan_sha256=sha(out/'plan.json'),environment_sha256=sha(out/'environment.json'),performance_overrides=performance,validation_overrides=validated,
        binaries=dict(sdk_sdl=pr['binaries']['pso'],llgl=record(exe)),SDK_source_fingerprint=pr['SDK_source_fingerprint'],SDK_pso_fingerprint=pr['profile_fingerprints']['pso'],LLGL_pso_fingerprint=fp,SDL_commit=pr['SDL_commit'],reference_hashes=read(REFERENCE)['sha256'],shader_header_sha256=sha(ROOT/'tests/fixtures/SpirvFixtures.hpp'),source_inputs=frozen,historical_timings_reused=False,overall_SDK_ranking=False,SDL_native_handle_verified=False,runs=[],groups=[],performance_accepted=False)
    runtime_files={}
    try:
        for job in schedule(p):
            require(unchanged(frozen),'Frozen comparison inputs changed');argv=arguments(job,r);env=overrides(job,r);start=time.monotonic();child=subprocess.Popen(argv,cwd=exe.parent if job['sdk']=='llgl' else ROOT,env={**clean,**env},stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:stdout,stderr=child.communicate(timeout=240)
            except subprocess.TimeoutExpired:
                child.kill();stdout,stderr=child.communicate()
                (out/'commands'/(job['name']+'.timeout.stdout')).write_bytes(stdout);(out/'commands'/(job['name']+'.timeout.stderr')).write_bytes(stderr);raise
            row=dict(job=job,argv=argv,pid=child.pid,started=start,finished=time.monotonic(),returncode=child.returncode,overrides=env)
            for key,data in [('stdout',stdout),('stderr',stderr)]:
                path=out/'commands'/(job['name']+'.'+key);path.write_bytes(data);row[key]=path.relative_to(out).as_posix();row[key+'_sha256']=sha(path)
            r['runs'].append(row);save(out/'receipt.json',r)
            if job['kind'] in SUCCESS:
                require(child.returncode==0,'Native PSO run failed: '+job['name']);data=json.loads(stdout);identity=validate_output(job,data,r);row['result']=data
                if 'identity' not in r:r['identity']=identity
                if job['kind']=='observe':r['observed_LLGL_instance_api']=data['observed_instance_api_version']
                if job['kind'] in ('audit','validation','measurement'):
                    require(not re.search(rb'Validation Error|SYNC-HAZARD',stderr),'Native workload validation error')
                    if options(job)[0]:require(b'VkLayer_khronos_validation.dll' in stderr and b'VK_LAYER_KHRONOS_validation' in stderr,'Loaded validation trace absent')
                if job['sdk']=='llgl':
                    for path in data['runtime_modules']:
                        q=pathlib.Path(path).resolve();require(q.is_relative_to(pathlib.Path(os.environ['WINDIR'])) or q in (exe,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',exe.parent/'VkLayer_gfx_llgl_draw.dll',layer.resolve()),'Unreviewed LLGL runtime module');runtime_files[str(q)]=record(q)
            else:validate_rejection(job,child.returncode,stdout,stderr)
            save(out/'receipt.json',r);print(job['name'],'PASS',flush=True)
        require(unchanged(frozen),'Final source/runtime inputs changed');save(out/'runtime-inputs.json',dict(files=list(runtime_files.values()),redistributed=False));r.update(status='completed',inputs_unchanged=True,groups=summarize(r['runs']),measurement_samples=900,verified_images=sum(x['result']['verified_images'] for x in r['runs'] if x['job']['kind'] in ('measurement','validation','audit')),verified_bytes=sum(x['result']['verified_bytes'] for x in r['runs'] if x['job']['kind'] in ('measurement','validation','audit')),performance_accepted=True);save(out/'receipt.json',r);print(json.dumps(check(out,live=True)),flush=True)
    except Exception as e:r.update(status='failed',performance_accepted=False,error=str(e));save(out/'receipt.json',r);raise

def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('stage',choices=('run','check'));a.add_argument('--build-root',type=pathlib.Path);a.add_argument('--prepared',type=pathlib.Path,default=ROOT/'out/phase3-current-upload-inputs-20261027/evidence');a.add_argument('--output',type=pathlib.Path,required=True);a.add_argument('--gpu-name',default='NVIDIA GeForce GTX 1660 SUPER');a.add_argument('--live',action='store_true');p=a.parse_args()
    if p.stage=='check':print(json.dumps(check(p.output,p.live)));return
    require(p.build_root is not None,'Explicit LLGL build required');run(p.build_root,p.prepared,p.output,p.gpu_name)

if __name__=='__main__':main()
