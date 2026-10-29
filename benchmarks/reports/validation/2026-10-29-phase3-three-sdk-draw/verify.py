"""Independent stdlib replay of CMP-08 raw processes, source bytes and statistics; no ZIP code execution."""
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
SHAPES={'draw1': (65, 37, 4), 'draw1000': (65, 37, 4), 'instanced1000': (65, 37, 4)}
WORK={'draw1': [1, 1], 'draw1000': [1000, 1], 'instanced1000': [1, 1000]}
COLUMNS=['record_ms', 'submit_ms', 'wait_ms', 'total_ms']
COMMON=('device_name','device_uuid','driver_uuid','device_luid','native_vendor_id','native_device_id','native_driver_version','native_api_version','environment')
EXTRA=['benchmarks/llgl_vulkan_upload/Context.hpp', 'benchmarks/llgl_vulkan_pso/Support.hpp', 'benchmarks/llgl_vulkan_draw/Observer.hpp', 'benchmarks/llgl_vulkan_draw/Layer.cpp', 'benchmarks/llgl_vulkan_draw/main.cpp', 'benchmarks/vulkan_draw/Frame.hpp', 'benchmarks/vulkan_draw/main.cpp', 'benchmarks/common/Adapter.hpp', 'benchmarks/common/Environment.cpp', 'benchmarks/competitors/sources.lock.json', 'benchmarks/phase3_comparison/draw.json', 'benchmarks/phase3_comparison/competitors/llgl-draw-comparison.json', 'tests/fixtures/SpirvFixtures.hpp', 'tests/fixtures/spirv/fullscreen.spvasm', 'tests/fixtures/spirv/red.spvasm', 'tests/fixtures/spirv/green.spvasm']
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
    values=[v for p in processes for v in p];require(all(type(v) in (float,int) and math.isfinite(v) and v>=0 for v in values),'Non-finite/non-positive elapsed time');ordered=sorted(values)
    def quantile(q):
        position=(len(ordered)-1)*q;lower=int(position);upper=min(lower+1,len(ordered)-1);return ordered[lower]+(ordered[upper]-ordered[lower])*(position-lower)
    median=quantile(.5)
    return dict(processes=5,samples=150,p50_ms=median,p95_ms=quantile(.95),p99_ms=quantile(.99),min_ms=min(values),max_ms=max(values),MAD_ms=statistics.median(abs(v-median) for v in values),process_medians_ms=[statistics.median(p) for p in processes],samples_removed=0)

def jobs():
    result=[dict(kind='probe',sdk=s,scenario='probe',repetition=0) for s in SDKS]+[dict(kind='observe',sdk='llgl',scenario='probe',repetition=0)]
    result += [dict(kind='audit',sdk='llgl',scenario=n,repetition=0) for n in SHAPES]
    result += [dict(kind='validation',sdk=s,scenario=n,repetition=0) for n in SHAPES for s in SDKS]
    result += [dict(kind=k,sdk=s,scenario='draw1',repetition=0) for s in SDKS for k in ('missing-layer','wrong-target')]
    result += [dict(kind=k,sdk='llgl',scenario=n,repetition=0) for k,n in [('core-control','core-control'),('sync-control','sync-control'),('sync-disabled','sync-control'),('wrong-call-count','wrong-call-count'),('wrong-instance-count','wrong-instance-count'),('wrong-scissor','wrong-scissor'),('corrupt-readback','corrupt-readback'),('observer-disabled','probe')]]
    rng=random.Random(20261027);orders={}
    for n in SHAPES:orders[n]=list(itertools.permutations(SDKS));rng.shuffle(orders[n])
    for rep in range(5):
        scenarios=list(SHAPES);rng.shuffle(scenarios)
        for scenario in scenarios:result += [dict(kind='measurement',sdk=s,scenario=scenario,repetition=rep) for s in orders[scenario][rep]]
    for i,j in enumerate(result):j['name']=f"{i:03}-{j['kind']}-{j['sdk']}-{j['scenario']}-{j['repetition']}"
    return result

def verify(root=HERE):
    m=read(root/'manifest.json');exact(m,dict(task='P3-CMP-08',historical_timings_reused=False));require(len(m['files'])==len(set(x['path'] for x in m['files'])),'Duplicate manifest path')
    for item in m['files']:
        data=(root/safe(item['path'])).read_bytes();require(sha(data)==item['sha256'] and len(data)==item['bytes'],'Evidence hash differs: '+item['path'])
    f=root/'native';r=read(f/'receipt.json');p=read(f/'plan.json');expected=dict(task='P3-CMP-08',status='completed',inputs_unchanged=True,performance_accepted=True,historical_timings_reused=False,overall_SDK_ranking=False,SDL_native_handle_verified=False,measurement_samples=1350,verified_images=1623,verified_bytes=15613260);exact(r,expected)
    exact(p,dict(schema_version=1,task='P3-CMP-08',profile='draw',profile_id='llgl-fixed-pin-transfer-src-v1',sdks=list(SDKS),scenarios=list(SHAPES),validation=dict(warmup=1,samples=3,required_core_and_sync=True),performance=dict(warmup=5,samples=30,repetitions=5,seed=20261027,order='seeded_scenario_blocks_five_distinct_three_SDK_permutations'),statistics=dict(quantile='linear_interpolation_at_(N-1)*q',retain_all_raw_samples=True,samples_removed=0,process_medians=True),serialized_processes=True,observer_free_performance_required=True,historical_timings_reused=False,overall_SDK_ranking=False,SDL_native_handle_verified=False,native_allocation_count_verified=False,GPU_memory_reclamation_verified=False,next_task='P3-CMP-09'))
    exact(p['timing'],{'category': 'end_to_end', 'columns': ['record_ms', 'submit_ms', 'wait_ms', 'total_ms'], 'includes': ['record_render_pass_pipeline_scissor_draws', 'submit', 'completed_fence'], 'excludes': ['resource_shader_pipeline_setup', 'full_changing_readback', 'teardown', 'warmup', 'validation'], 'GPU_timestamp_measurement': False})
    exact(p,dict(work=WORK,geometry={'shape': [65, 37, 4], 'scissor': [5, 3, 55, 31], 'vertices_per_instance': 3, 'first_vertex': 0, 'first_instance': 0, 'reference': 'full_changing_red_green_triangle_blue_cyan_clear'},GPU_timestamp_pending_task='P3-BEN-09'))
    require(sha((f/'plan.json').read_bytes())==r['plan_sha256'] and sha((f/'environment.json').read_bytes())==r['environment_sha256'],'Plan/environment hash differs')
    with zipfile.ZipFile(root/'source.zip') as z:
        names=z.namelist();require(len(names)==len(set(names)),'Duplicate ZIP source')
        for name in names:safe(name)
        folder='benchmarks/llgl_vulkan_draw_compare/';selected=sorted([n for n in names if n.startswith(folder) and '/' not in n[len(folder):] and n.endswith(('.cpp','.hpp','CMakeLists.txt'))]+EXTRA)
        require([n for n in selected if n.startswith(folder)]==[folder+n for n in ('CMakeLists.txt','Draw.hpp','Identity.hpp','main.cpp')],'Native source coverage differs')
        fp=sha(('profile|llgl-fixed-pin-transfer-src-v1\nworkload|draw_compare\n'+''.join(n+'|'+sha(z.read(n))+'\n' for n in selected)).encode());require(fp==r['LLGL_draw_fingerprint'],'LLGL source fingerprint differs')
        sdk=sorted(n for n in names if n=='CMakeLists.txt' or n.startswith(('src/','include/')) and n.endswith(('.cpp','.hpp')));require(len(sdk)==28,'SDK source coverage differs');require(sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in sdk).encode())==r['SDK_source_fingerprint'],'SDK source fingerprint differs')
        consumer=sorted([n for n in names if n.startswith('benchmarks/vulkan_draw/') and n.endswith(('.cpp','.hpp','CMakeLists.txt'))]+['benchmarks/vulkan_upload/Identity.hpp','tests/fixtures/SpirvFixtures.hpp','tests/fixtures/spirv/fullscreen.spvasm','tests/fixtures/spirv/red.spvasm','tests/fixtures/spirv/green.spvasm','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/competitors/sources.lock.json']);require(sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in consumer).encode())==r['SDK_draw_fingerprint'],'SDK/SDL consumer source differs')
        suffix=folder+'main.cpp';original=next(x['path'][:-len(suffix)] for x in r['source_inputs'] if x['path'].endswith(suffix));frozen={x['path'][len(original):]:x for x in r['source_inputs'] if x['path'].startswith(original)}
        for name in selected+sdk+consumer+['tools/compare_llgl_draw.py']:
            require(name in frozen and sha(z.read(name))==frozen[name]['sha256'],'ZIP differs from executed source')
        require(loads(z.read('benchmarks/phase3_comparison/competitors/llgl-draw-comparison.json'))==p,'Compiled comparison contract differs')
    reference=read(f/'frame-reference.json');hashes=[]
    for i in range(4):
        data=b''.join(bytes(((0,255,0,255) if i%2 else (255,0,0,255)) if 5<=x<60 and 3<=y<34 else ((0,255,255,255) if i%2 else (0,0,255,255))) for y in range(37) for x in range(65));hashes.append(sha(data))
    require(reference['sha256']==r['reference_hashes']==hashes,'Full validation references differ')
    ref_path='benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/native/frame-reference.json';require(sha((f/'frame-reference.json').read_bytes())==frozen[ref_path]['sha256'],'Independent CPU reference changed')
    require(r['shader_header_sha256']==frozen['tests/fixtures/SpirvFixtures.hpp']['sha256'],'Shader header provenance differs')
    require((f/'settings/vk_layer_settings.txt').read_text()==SETTINGS and (f/'sync-off/vk_layer_settings.txt').read_text()==OFF,'Required sync settings changed')
    clean=dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=r['output_directory']+'/empty',VK_LAYER_SETTINGS_PATH=r['output_directory']+'/empty');layer=r['validation_library'];validated={**clean,'VK_LAYER_PATH':layer.rsplit('/',1)[0],'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','VK_LAYER_SETTINGS_PATH':r['output_directory']+'/settings','GFX_EXPECT_VULKAN_LAYER':layer,'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true','VK_LOADER_DEBUG':'layer'}
    require(r['performance_overrides']==clean and r['validation_overrides']==validated and read(f/'environment.json')['performance_overrides']==clean and read(f/'environment.json')['validation_overrides']==validated,'Layer/observer isolation differs')
    runtime=read(f/'runtime-receipt.json');require(any(x['path'].endswith('VkLayer_khronos_validation.dll') and x['sha256']==r['validation_library_sha256'] for x in runtime['artifacts']),'Validation DLL provenance differs')
    schedule=jobs();require(len(r['runs'])==len(schedule)==75,'Missing/extra native process');end=0;images=0;bytes_verified=0;measurements=[]
    for run,job in zip(r['runs'],schedule):
        require(run['job']==job,'Scenario/SDK/order/repetition differs');require(type(run['pid']) is int and run['pid']>0 and all(type(run[k]) in (int,float) and math.isfinite(run[k]) for k in ('started','finished')) and end<=run['started']<run['finished'],'Overlapping/invalid process interval');end=run['finished']
        kind=job['kind'];sdk=job['sdk'];scenario=job['scenario'];validation=kind in ('validation','missing-layer','core-control','sync-control','sync-disabled');observe=sdk=='llgl' and (validation or kind in ('observe','audit','wrong-call-count','wrong-instance-count','wrong-scissor','corrupt-readback','observer-disabled'));special=kind in ('probe','observe','core-control','sync-control');w,n=(0,0) if kind in ('probe','observe','observer-disabled','core-control','sync-control','sync-disabled') else (5,30) if kind=='measurement' else (1,3)
        env=dict(validated if validation else clean)
        if observe:env.update(VK_LAYER_PATH=r['observer_directory']+(';' + layer.rsplit('/',1)[0] if validation else ''),VK_INSTANCE_LAYERS='VK_LAYER_GFX_llgl_draw')
        if kind=='missing-layer':env['VK_LAYER_PATH']=r['output_directory']+'/missing'
        if kind=='sync-disabled':env.update(VK_LAYER_SETTINGS_PATH=r['output_directory']+'/sync-off',VK_KHRONOS_VALIDATION_VALIDATE_SYNC='false')
        if kind=='observer-disabled':env['VK_INSTANCE_LAYERS']=''
        require(run['overrides']==env,'Per-process environment differs');gpu='GraphicsSDK CMP08 absent GPU' if kind=='wrong-target' else r['expected_gpu'];exe=r['binaries']['llgl' if sdk=='llgl' else 'sdk_sdl']['path'];argv=[exe,gpu,'validation' if validation else 'observe' if observe else 'plain',scenario,str(w),str(n)] if sdk=='llgl' else [exe,'--probe',sdk,'high',gpu] if kind=='probe' else [exe,sdk,scenario,'high',str(w),str(n),str(int(validation)),gpu];require(run['argv']==argv,'Native argv differs')
        stdout=(f/safe(run['stdout'])).read_bytes();stderr=(f/safe(run['stderr'])).read_bytes();require(sha(stdout)==run['stdout_sha256'] and sha(stderr)==run['stderr_sha256'],'Raw output differs')
        if kind not in ('probe','observe','audit','validation','core-control','sync-control','measurement'):
            require(type(run['returncode']) is int and run['returncode']==1 and not stdout,'Failed control produced samples')
            if sdk=='llgl':token={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','sync-disabled':b'Expected validation control diagnostic absent','wrong-call-count':b'Native draw count/arguments differ from contract','wrong-instance-count':b'Native draw count/arguments differ from contract','wrong-scissor':b'Full changing draw reference mismatch','corrupt-readback':b'Full changing draw reference mismatch','observer-disabled':b'Native draw observer not active'}[kind];require(token in stderr,'Unrelated LLGL rejection')
            elif kind=='missing-layer':require(b'VK_LAYER_KHRONOS_validation is not installed' in stderr,'Unrelated missing-layer rejection')
            else:
                expected='FAIL: Selected GPU differs from expected GPU' if sdk=='sdl' else 'FAIL: Unique exact SDK GPU required'
                lines=stderr.decode(errors='replace').strip().splitlines();require(lines and lines[-1]==expected,'Unrelated wrong-target rejection')
            continue
        exact(run,dict(returncode=0));row=loads(stdout);require(row==run['result'],'Raw/parsed result differs');exact(row,dict(schema_version=1,sdk=sdk,api='vulkan',configuration='Release',category='end_to_end',contract='vulkan_scissored_fullscreen_triangle_draw_fence_each_sample_readback_v1',scenario=scenario,validation=validation,probe=kind in ('probe','observe'),warmup=w,sample_count=n,gpu_ms=None));exact(row,r['identity']);require(row['device_name']==r['expected_gpu'] and row['native_vendor_id']==4318,'Wrong actual GPU')
        for key,size in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:require(re.fullmatch('[0-9a-f]{'+str(size)+'}',row[key]) and int(row[key],16),'Invalid hardware UUID/LUID')
        e=row['environment'];high,low=map(int,e['luid'].split(':'));require(struct.pack('<II',low,high&0xffffffff).hex()==row['device_luid'] and e['vendor_id']==row['native_vendor_id'] and e['device_id']==row['native_device_id'] and e['driver_query_available'] is True and e['software_renderer'] is False and e['pointer_bits']==64,'Native/DXGI mismatch')
        if sdk=='llgl':
            exact(row,dict(task='P3-CMP-08',profile_id='llgl-fixed-pin-transfer-src-v1',source_fingerprint=fp,native_handle_verified=True,identity_attribution='actual_LLGL_native_handles_and_unique_DXGI',control=kind in ('core-control','sync-control'),queue_index=0,instance_API_request_observed=observe,observer_enabled=observe,observed_instance_api_version=r['observed_LLGL_instance_api'] if observe else 0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,requested_instance_api_version=None,clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',texture_bind_policy='color_attachment_sampled_copy_src',submitted_control_work=0,native_workload_shortcuts=False));require(row['queue_flags']&1 and row['runtime_modules'],'LLGL native queue/runtime absent');require(any('vklayer_gfx_llgl_draw.dll' in x.casefold() for x in row['runtime_modules'])==observe,'Observer in timing/absence');require(any('vklayer_khronos_validation.dll' in x.casefold() for x in row['runtime_modules'])==validation,'Validation in timing/absence')
            require(row['frame_sha256']==([] if special or not observe else hashes),'LLGL full changing frame differs')
            count=0 if special else w+n;calls,instances=(0,0) if special else WORK[scenario];native=dict(native_draw_calls=count*calls,native_forwarded_draw_calls=count*calls,native_instances=count*calls*instances,native_vertices=3*count*calls*instances,native_bad_arguments=0,native_min_instances=instances if count else 0,native_max_instances=instances if count else 0);exact(row,{k:value if observe else None for k,value in native.items()})
            if kind in ('core-control','sync-control'):
                token='VUID-VkPhysicalDeviceProperties2-sType-sType' if kind=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE';require(row['control_diagnostic']==token and type(row['delivered_errors']) is int and row['delivered_errors']>0,'Validation diagnostic missing')
            else:exact(row,dict(control_diagnostic='',delivered_errors=0))
        else:exact(row,dict(source_fingerprint=r['SDK_draw_fingerprint'],SDK_source_fingerprint=r['SDK_source_fingerprint'],SDL_commit=r['SDL_commit'],identity_attribution='unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle',preference='high',requested_instance_api_version=4198400))
        count=0 if special else w+n;calls,instances=(0,0) if special else WORK[scenario]
        exact(row,dict(width=65,height=37,texel_bytes=4,verified_images=count,verified_bytes=9620*count,readback_verified=not special,scissor=[5,3,55,31],vertices_per_instance=3,first_vertex=0,first_instance=0,draw_calls_per_frame=calls,instances_per_call=instances,vertices_per_frame=3*calls*instances,submitted_draw_calls=count*calls,shader_header_sha256=r['shader_header_sha256'],columns=COLUMNS))
        values=row['samples_ms'];require(type(values) is list and len(values)==(0 if special else n) and all(type(v) in (int,float) and math.isfinite(v) and v>0 for v in values),'Invalid completed elapsed times');intervals=row['CPU_intervals_ms'];require(type(intervals) is list and len(intervals)==len(values),'CPU interval count differs')
        for i,t in enumerate(intervals):require(type(t) is list and len(t)==4 and all(type(v) in (int,float) and math.isfinite(v) and v>=0 for v in t) and t[-1]==values[i] and abs(sum(t[:3])-t[-1])<=max(1e-6,t[-1]*1e-10),'CPU intervals do not partition total')
        require(sum(values)/1000<=run['finished']-run['started'],'Samples exceed process interval');require(bool(row['loaded_validation_layer'])==validation and (not validation or row['loaded_validation_layer'].replace('\\','/').casefold()==layer.casefold()),'Validation contamination/runtime changed')
        if kind in ('audit','validation','measurement'):
            require(not re.search(rb'Validation Error|SYNC-HAZARD',stderr),'Native workload validation error');images+=count;bytes_verified+=row['verified_bytes']
            if validation:require(b'VK_LAYER_KHRONOS_validation' in stderr and b'VkLayer_khronos_validation.dll' in stderr,'Required layer load trace missing')
        if kind=='measurement':measurements.append(run)
    require(images==1623 and bytes_verified==15613260 and len(measurements)==45,'Full readback/process totals differ')
    groups=[]
    for name in SHAPES:
        for sdk in SDKS:
            selected=[x for x in measurements if x['job']['sdk']==sdk and x['job']['scenario']==name];require([x['job']['repetition'] for x in selected]==list(range(5)),'Independent repetitions missing')
            for i,column in enumerate(COLUMNS):groups.append(dict(scenario=name,sdk=sdk,shape=list(SHAPES[name]),category='end_to_end',column=column,statistics=stats([[t[i] for t in x['result']['CPU_intervals_ms']] for x in selected])))
    require(r['groups']==groups,'Measurement-only statistics do not reproduce');require(r['observed_LLGL_instance_api']==4211045,'Separate observed API request differs')
    print('PASS saved CMP-08: 75 processes, 1350 fresh samples, 5400 CPU intervals, 1623 complete images / 15613260 bytes; no overall SDK ranking')
    return r

if __name__=='__main__':verify()
