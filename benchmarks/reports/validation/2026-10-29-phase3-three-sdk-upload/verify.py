"""Independent stdlib replay of CMP-07 raw processes, source bytes and statistics; no ZIP code execution."""
import hashlib
import itertools
import json
import math
import pathlib
import random
import re
import statistics
import struct
import zipfile

HERE=pathlib.Path(__file__).resolve().parent
SDKS=('graphicssdk','sdl','llgl')
SHAPES={'odd':(65,37,4),'upload1080':(1920,1080,4),'upload4k':(3840,2160,4),'upload4k16f':(3840,2160,8)}
COMMON=('device_name','device_uuid','driver_uuid','device_luid','native_vendor_id','native_device_id','native_driver_version','native_api_version','environment')
EXTRA=['benchmarks/llgl_vulkan_upload/Context.hpp','benchmarks/llgl_vulkan_upload/Payload.hpp','benchmarks/llgl_vulkan_upload/main.cpp','benchmarks/llgl_vulkan_pso/Support.hpp','benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/llgl_vulkan_draw/Layer.cpp','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/main.cpp','benchmarks/phase3_comparison/upload.json','benchmarks/phase3_comparison/competitors/llgl-upload-comparison.json','benchmarks/competitors/sources.lock.json']
SETTINGS='khronos_validation.validate_sync = true\nkhronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n'
OFF='khronos_validation.validate_sync = false\nkhronos_validation.syncval_submit_time_validation = false\n'

def require(ok,message):
    if not ok:raise ValueError(message)

def exact(row,expected,message='Comparison scope/identity/coverage differs'):
    require(all(k in row and type(row[k]) is type(v) and row[k]==v for k,v in expected.items()),message)

def unique(pairs):
    out={}
    for k,v in pairs:require(k not in out,'Duplicate JSON key');out[k]=v
    return out

def loads(data):return json.loads(data,object_pairs_hook=unique)

def read(p):return loads(p.read_text(encoding='utf-8-sig'))

def sha(data):return hashlib.sha256(data).hexdigest()

def safe(name):
    p=pathlib.PurePosixPath(name);require(not p.is_absolute() and '..' not in p.parts and ':' not in name and '\\' not in name and '__pycache__' not in p.parts and p.suffix!='.pyc','Unsafe evidence path');return p

def stats(processes):
    require(len(processes)==5 and all(len(p)==30 for p in processes),'Independent process/sample count differs')
    values=[v for p in processes for v in p];require(all(type(v) in (float,int) and math.isfinite(v) and v>0 for v in values),'Non-finite/non-positive elapsed time');ordered=sorted(values)
    def quantile(q):
        position=(len(ordered)-1)*q;lower=int(position);upper=min(lower+1,len(ordered)-1);return ordered[lower]+(ordered[upper]-ordered[lower])*(position-lower)
    median=quantile(.5)
    return dict(processes=5,samples=150,p50_ms=median,p95_ms=quantile(.95),p99_ms=quantile(.99),min_ms=min(values),max_ms=max(values),MAD_ms=statistics.median(abs(v-median) for v in values),process_medians_ms=[statistics.median(p) for p in processes],samples_removed=0)

def jobs():
    result=[dict(kind='probe',sdk=s,scenario='probe',repetition=0) for s in SDKS]+[dict(kind='observe',sdk='llgl',scenario='probe',repetition=0)]
    result += [dict(kind='validation',sdk=s,scenario=n,repetition=0) for n in SHAPES for s in SDKS]
    result += [dict(kind=k,sdk=s,scenario='odd',repetition=0) for s in SDKS for k in ('missing-layer','wrong-target')]
    result += [dict(kind=k,sdk='llgl',scenario=n,repetition=0) for k,n in [('core-control','core-control'),('sync-control','sync-control'),('sync-disabled','sync-control'),('corrupt-readback','corrupt-readback'),('observer-disabled','probe')]]
    rng=random.Random(20261027);orders={}
    for n in SHAPES:orders[n]=list(itertools.permutations(SDKS));rng.shuffle(orders[n])
    for rep in range(5):
        scenarios=list(SHAPES);rng.shuffle(scenarios)
        for scenario in scenarios:result += [dict(kind='measurement',sdk=s,scenario=scenario,repetition=rep) for s in orders[scenario][rep]]
    for i,j in enumerate(result):j['name']=f"{i:03}-{j['kind']}-{j['sdk']}-{j['scenario']}-{j['repetition']}"
    return result

def verify(root=HERE):
    m=read(root/'manifest.json');exact(m,dict(task='P3-CMP-07',historical_timings_reused=False));require(len(m['files'])==len(set(x['path'] for x in m['files'])),'Duplicate manifest path')
    for item in m['files']:
        data=(root/safe(item['path'])).read_bytes();require(sha(data)==item['sha256'] and len(data)==item['bytes'],'Evidence hash differs: '+item['path'])
    f=root/'native';r=read(f/'receipt.json');p=read(f/'plan.json');expected=dict(task='P3-CMP-07',status='completed',inputs_unchanged=True,performance_accepted=True,historical_timings_reused=False,overall_SDK_ranking=False,SDL_native_handle_verified=False,measurement_samples=1800,verified_images=2148,verified_bytes=57908372340);exact(r,expected)
    exact(p,dict(schema_version=1,task='P3-CMP-07',profile='upload',profile_id='llgl-fixed-pin-transfer-src-v1',sdks=list(SDKS),scenarios=list(SHAPES),validation=dict(warmup=1,samples=3,required_core_and_sync=True),performance=dict(warmup=5,samples=30,repetitions=5,seed=20261027,order='seeded_scenario_blocks_five_distinct_three_SDK_permutations'),statistics=dict(quantile='linear_interpolation_at_(N-1)*q',retain_all_raw_samples=True,samples_removed=0,process_medians=True),serialized_processes=True,observer_free_performance_required=True,historical_timings_reused=False,overall_SDK_ranking=False,SDL_native_handle_verified=False,native_allocation_count_verified=False,GPU_memory_reclamation_verified=False,next_task='P3-CMP-08'))
    exact(p['timing'],dict(category='end_to_end',column='total_ms',includes=['staging_map_and_CPU_payload_copy','record','submit','completed_upload_fence'],excludes=['payload_generation','full_changing_readback','resource_setup','teardown','warmup','validation'],GPU_timestamp_measurement=False,layout_difference='SDK padded staging versus SDL and LLGL tight transfer layout'))
    require(sha((f/'plan.json').read_bytes())==r['plan_sha256'] and sha((f/'environment.json').read_bytes())==r['environment_sha256'],'Plan/environment hash differs')
    with zipfile.ZipFile(root/'source.zip') as z:
        names=z.namelist();require(len(names)==len(set(names)),'Duplicate ZIP source')
        for name in names:safe(name)
        folder='benchmarks/llgl_vulkan_upload_compare/';selected=sorted([n for n in names if n.startswith(folder) and '/' not in n[len(folder):] and n.endswith(('.cpp','.hpp','CMakeLists.txt'))]+EXTRA)
        require([n for n in selected if n.startswith(folder)]==[folder+n for n in ('CMakeLists.txt','Upload.hpp','main.cpp')],'Native source coverage differs')
        fp=sha(('profile|llgl-fixed-pin-transfer-src-v1\nworkload|upload_compare\n'+''.join(n+'|'+sha(z.read(n))+'\n' for n in selected)).encode());require(fp==r['LLGL_upload_fingerprint'],'LLGL source fingerprint differs')
        sdk=sorted(n for n in names if n=='CMakeLists.txt' or n.startswith(('src/','include/')) and n.endswith(('.cpp','.hpp')));require(len(sdk)==28,'SDK source coverage differs');require(sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in sdk).encode())==r['SDK_source_fingerprint'],'SDK source fingerprint differs')
        consumer=sorted([n for n in names if n.startswith('benchmarks/vulkan_upload/') and n.endswith(('.cpp','.hpp','CMakeLists.txt'))]+['benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/competitors/sources.lock.json']);require(sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in consumer).encode())==r['SDK_upload_fingerprint'],'SDK/SDL consumer source differs')
        suffix=folder+'main.cpp';original=next(x['path'][:-len(suffix)] for x in r['source_inputs'] if x['path'].endswith(suffix));frozen={x['path'][len(original):]:x for x in r['source_inputs'] if x['path'].startswith(original)}
        for name in selected+sdk+consumer+['tools/compare_llgl_upload.py']:
            require(name in frozen and sha(z.read(name))==frozen[name]['sha256'],'ZIP differs from executed source')
        require(loads(z.read('benchmarks/phase3_comparison/competitors/llgl-upload-comparison.json'))==p,'Compiled comparison contract differs')
    reference=read(f/'payload-reference.json');require(reference['seeds']==[1,2,3,4] and reference['hashes']==r['reference_hashes'],'Full validation references differ')
    ref_path='benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/payload-reference.json';require(sha((f/'payload-reference.json').read_bytes())==frozen[ref_path]['sha256'],'Independent CPU reference changed')
    require((f/'settings/vk_layer_settings.txt').read_text()==SETTINGS and (f/'sync-off/vk_layer_settings.txt').read_text()==OFF,'Required sync settings changed')
    clean=dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=r['output_directory']+'/empty',VK_LAYER_SETTINGS_PATH=r['output_directory']+'/empty');layer=r['validation_library'];validated={**clean,'VK_LAYER_PATH':layer.rsplit('/',1)[0],'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','VK_LAYER_SETTINGS_PATH':r['output_directory']+'/settings','GFX_EXPECT_VULKAN_LAYER':layer,'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true','VK_LOADER_DEBUG':'layer'}
    require(r['performance_overrides']==clean and r['validation_overrides']==validated and read(f/'environment.json')['performance_overrides']==clean and read(f/'environment.json')['validation_overrides']==validated,'Layer/observer isolation differs')
    runtime=read(f/'runtime-receipt.json');require(any(x['path'].endswith('VkLayer_khronos_validation.dll') and x['sha256']==r['validation_library_sha256'] for x in runtime['artifacts']),'Validation DLL provenance differs')
    schedule=jobs();require(len(r['runs'])==len(schedule)==87,'Missing/extra native process');end=0;images=0;bytes_verified=0;measurements=[]
    for run,job in zip(r['runs'],schedule):
        require(run['job']==job,'Scenario/SDK/order/repetition differs');require(type(run['pid']) is int and run['pid']>0 and all(type(run[k]) in (int,float) and math.isfinite(run[k]) for k in ('started','finished')) and end<=run['started']<run['finished'],'Overlapping/invalid process interval');end=run['finished']
        kind=job['kind'];sdk=job['sdk'];scenario=job['scenario'];validation=kind in ('validation','missing-layer','core-control','sync-control','sync-disabled');observe=kind in ('observe','observer-disabled');special=kind in ('probe','observe','core-control','sync-control');w,n=(0,0) if kind in ('probe','observe','observer-disabled','core-control','sync-control','sync-disabled') else (5,30) if kind=='measurement' else (1,3)
        env=dict(validated if validation else clean)
        if observe:env.update(VK_LAYER_PATH=r['observer_directory'],VK_INSTANCE_LAYERS='VK_LAYER_GFX_llgl_draw')
        if kind=='missing-layer':env['VK_LAYER_PATH']=r['output_directory']+'/missing'
        if kind=='sync-disabled':env.update(VK_LAYER_SETTINGS_PATH=r['output_directory']+'/sync-off',VK_KHRONOS_VALIDATION_VALIDATE_SYNC='false')
        if kind=='observer-disabled':env['VK_INSTANCE_LAYERS']=''
        require(run['overrides']==env,'Per-process environment differs');gpu='GraphicsSDK CMP07 absent GPU' if kind=='wrong-target' else r['expected_gpu'];exe=r['binaries']['llgl' if sdk=='llgl' else 'sdk_sdl']['path'];argv=[exe,gpu,'observe' if observe else 'validation' if validation else 'plain',scenario,str(w),str(n)] if sdk=='llgl' else [exe,'--probe',sdk,'high',gpu] if kind=='probe' else [exe,sdk,scenario,'high',str(w),str(n),str(int(validation)),gpu];require(run['argv']==argv,'Native argv differs')
        stdout=(f/safe(run['stdout'])).read_bytes();stderr=(f/safe(run['stderr'])).read_bytes();require(sha(stdout)==run['stdout_sha256'] and sha(stderr)==run['stderr_sha256'],'Raw output differs')
        if kind not in ('probe','observe','validation','core-control','sync-control','measurement'):
            require(type(run['returncode']) is int and run['returncode']==1 and not stdout,'Failed control produced samples')
            if sdk=='llgl':token={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','sync-disabled':b'Expected validation control diagnostic absent','corrupt-readback':b'Full changing upload readback mismatch','observer-disabled':b'Native draw observer not active'}[kind];require(token in stderr,'Unrelated LLGL rejection')
            elif kind=='missing-layer':require(b'VK_LAYER_KHRONOS_validation is not installed' in stderr,'Unrelated missing-layer rejection')
            else:
                expected='FAIL: Selected GPU differs from expected GPU' if sdk=='sdl' else 'FAIL: Exact unambiguous SDK adapter required'
                lines=stderr.decode(errors='replace').strip().splitlines();require(lines and lines[-1]==expected,'Unrelated wrong-target rejection')
            continue
        exact(run,dict(returncode=0));row=loads(stdout);require(row==run['result'],'Raw/parsed result differs');exact(row,dict(schema_version=1,sdk=sdk,api='vulkan',configuration='Release',category='end_to_end',contract='vulkan_upload_cpu_payload_to_texture_fence_complete_each_sample_readback_v1',scenario=scenario,validation=validation,probe=kind in ('probe','observe'),warmup=w,sample_count=n,gpu_ms=None));exact(row,r['identity']);require(row['device_name']==r['expected_gpu'] and row['native_vendor_id']==4318,'Wrong actual GPU')
        for key,size in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:require(re.fullmatch('[0-9a-f]{'+str(size)+'}',row[key]) and int(row[key],16),'Invalid hardware UUID/LUID')
        e=row['environment'];high,low=map(int,e['luid'].split(':'));require(struct.pack('<II',low,high&0xffffffff).hex()==row['device_luid'] and e['vendor_id']==row['native_vendor_id'] and e['device_id']==row['native_device_id'] and e['driver_query_available'] is True and e['software_renderer'] is False and e['pointer_bits']==64,'Native/DXGI mismatch')
        if sdk=='llgl':
            exact(row,dict(task='P3-CMP-07',profile_id='llgl-fixed-pin-transfer-src-v1',source_fingerprint=fp,native_handle_verified=True,identity_attribution='actual_LLGL_native_handles_and_unique_DXGI',control=kind in ('core-control','sync-control'),queue_index=0,instance_API_request_observed=observe,observer_enabled=observe,observed_instance_api_version=r['observed_LLGL_instance_api'] if observe else 0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,row_stride=0,layer_stride=0,submitted_control_work=0,native_workload_shortcuts=False));require(row['queue_flags']&1 and row['runtime_modules'],'LLGL native queue/runtime absent');require(not any('vklayer_gfx_llgl_draw.dll' in x.casefold() for x in row['runtime_modules']) or observe,'Observer in timing');require(not any('vklayer_khronos_validation.dll' in x.casefold() for x in row['runtime_modules']) or validation,'Validation in timing')
            require(row['payload_sha256']==([] if special or not validation else reference['hashes'][scenario]),'LLGL full changing payload differs')
            if kind in ('core-control','sync-control'):
                token='VUID-VkPhysicalDeviceProperties2-sType-sType' if kind=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE';require(row['control_diagnostic']==token and type(row['delivered_errors']) is int and row['delivered_errors']>0,'Validation diagnostic missing')
            else:exact(row,dict(control_diagnostic='',delivered_errors=0))
        else:exact(row,dict(source_fingerprint=r['SDK_upload_fingerprint'],SDK_source_fingerprint=r['SDK_source_fingerprint'],SDL_commit=r['SDL_commit'],identity_attribution='unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle',preference='high'))
        shape=(0,0,0) if special else SHAPES[scenario];count=0 if special else w+n;exact(row,dict(width=shape[0],height=shape[1],texel_bytes=shape[2],verified_images=count,verified_bytes=math.prod(shape)*count,readback_verified=not special));values=row['samples_ms'];require(type(values) is list and len(values)==(0 if special else n) and all(type(v) in (int,float) and math.isfinite(v) and v>0 for v in values),'Invalid completed elapsed times');require(sum(values)/1000<=run['finished']-run['started'],'Samples exceed process interval');require(bool(row['loaded_validation_layer'])==validation and (not validation or row['loaded_validation_layer'].replace('\\','/').casefold()==layer.casefold()),'Validation contamination/runtime changed')
        if kind in ('validation','measurement'):
            require(not re.search(rb'Validation Error|SYNC-HAZARD',stderr),'Native workload validation error');images+=count;bytes_verified+=row['verified_bytes']
            if validation:require(b'VK_LAYER_KHRONOS_validation' in stderr and b'VkLayer_khronos_validation.dll' in stderr,'Required layer load trace missing')
        if kind=='measurement':measurements.append(run)
    require(images==2148 and bytes_verified==57908372340 and len(measurements)==60,'Full readback/process totals differ')
    groups=[]
    for name in SHAPES:
        for sdk in SDKS:
            selected=[x for x in measurements if x['job']['sdk']==sdk and x['job']['scenario']==name];require([x['job']['repetition'] for x in selected]==list(range(5)),'Independent repetitions missing');groups.append(dict(scenario=name,sdk=sdk,shape=list(SHAPES[name]),category='end_to_end',column='total_ms',statistics=stats([x['result']['samples_ms'] for x in selected])))
    require(r['groups']==groups,'Measurement-only statistics do not reproduce');require(r['observed_LLGL_instance_api']==4211045,'Separate observed API request differs')
    print('PASS saved CMP-07: 87 processes, 1800 fresh samples, 2148 complete images / 57908372340 bytes; no overall SDK ranking')
    return r

if __name__=='__main__':verify()
