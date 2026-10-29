"""Independent stdlib replay of raw GPU counters, actual native queues, fences and impact modes."""
import hashlib,itertools,json,math,pathlib,random,re,statistics,struct,zipfile
HERE=pathlib.Path(__file__).resolve().parent
SDKS=('graphicssdk','sdl')
MODES=('baseline','audit','timestamp')
WORK={'draw1':(1,1),'draw1000':(1000,1),'instanced1000':(1,1000)}
SUCCESS=('probe','validation','validation-audit','missing-timestamp-layer','missing-end','wrong-boundary','measurement')
SETTINGS='khronos_validation.validate_sync = true\nkhronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n'
def require(ok,message):
    if not ok:raise ValueError(message)

def exact(row,expected,message='Comparison scope/identity/coverage differs'):
    require(all(k in row and type(row[k]) is type(v) and json.dumps(row[k],sort_keys=True,allow_nan=False)==json.dumps(v,sort_keys=True,allow_nan=False) for k,v in expected.items()),message)

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
    result=[dict(kind='probe',sdk=s,scenario='probe',mode='audit',repetition=0) for s in SDKS]
    result += [dict(kind='validation',sdk=s,scenario=n,mode='timestamp',repetition=0) for n in WORK for s in SDKS]
    result += [dict(kind='validation-audit',sdk=s,scenario='draw1',mode='audit',repetition=0) for s in SDKS]
    result += [dict(kind=k,sdk=s,scenario='draw1',mode=k,repetition=0) for s in SDKS for k in ('missing-end','wrong-boundary')]
    result += [dict(kind=k,sdk=s,scenario='draw1',mode='audit' if k=='wrong-target' else 'timestamp',repetition=0) for s in SDKS for k in ('missing-layer','missing-timestamp-layer','wrong-target')]
    result += [dict(kind='validation-control',sdk='graphicssdk',scenario='draw1',mode='validation-control',repetition=0)]
    rng=random.Random(20261029);orders={n:rng.sample(list(itertools.permutations(itertools.product(SDKS,MODES))),5) for n in WORK}
    for rep in range(5):
        names=list(WORK);rng.shuffle(names)
        for n in names:result += [dict(kind='measurement',sdk=s,scenario=n,mode=m,repetition=rep) for s,m in orders[n][rep]]
    for i,j in enumerate(result):j['name']=f"{i:03}-{j['kind']}-{j['sdk']}-{j['scenario']}-{j['mode']}-{j['repetition']}"
    return result

def options(j):
    validation=j['kind'] in ('validation','validation-audit','missing-layer','missing-timestamp-layer','missing-end','wrong-boundary','validation-control')
    return validation,*((0,0) if j['kind']=='probe' else (5,30) if j['kind']=='measurement' else (1,3))

def overrides(j,r):
    validation,_,_=options(j);out=r['output_directory'];env=dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=out+'/empty',VK_LAYER_SETTINGS_PATH=out+'/empty');layered=j['mode']!='baseline' and j['kind'] not in ('missing-layer','missing-timestamp-layer')
    if layered:env.update(VK_LAYER_PATH=r['layer_directory'],VK_INSTANCE_LAYERS='VK_LAYER_GFX_draw_timestamp',GFX_GPU_TIMESTAMP_MODE=j['mode'],GFX_GPU_TIMESTAMP_OUTPUT=out+'/commands/'+j['name']+'.layer.json')
    if validation:env.update(VK_LAYER_PATH=(r['layer_directory']+';' if layered else '')+r['validation_library'].rsplit('/',1)[0],VK_INSTANCE_LAYERS=('VK_LAYER_GFX_draw_timestamp;' if layered else '')+'VK_LAYER_KHRONOS_validation',VK_LAYER_SETTINGS_PATH=out+'/settings',VK_KHRONOS_VALIDATION_VALIDATE_SYNC='true',VK_LOADER_DEBUG='layer',GFX_EXPECT_VULKAN_LAYER=r['validation_library'])
    if j['kind']=='missing-layer':env['VK_LAYER_PATH']=out+'/missing'
    return env

def layer_trace(err):
    stacks=re.findall(rb'vkCreate(?:Instance|Device) layer callstack setup to:(.*?)<Drivers>',err,re.S);selected=[s for s in stacks if b'VK_LAYER_GFX_draw_timestamp' in s]
    require(selected and all(b'VK_LAYER_KHRONOS_validation' in s and s.index(b'VK_LAYER_GFX_draw_timestamp')<s.index(b'VK_LAYER_KHRONOS_validation') for s in selected),'Wrong real validation callstack order')

def counter_result(j,native,row,r):
    exact(native,dict(schema_version=1,task='P3-BEN-09',layer_fingerprint=r['layer_fingerprint'],mode=j['mode'],live_instances=0,live_devices=0,validation_diagnostics=[]));require(type(native['created_instances']) is int and native['created_instances']>0 and native['created_instances']==native['destroyed_instances'],'Incomplete instance retirement')
    found=[d for d in native['devices'] if d['frames']];probe=j['kind']=='probe';require(len(found)==(0 if probe else 1) and native['devices'],'Ambiguous renderer native device');d=native['devices'][-1] if probe else found[0]
    mapping={'device_name':'device_name','device_uuid':'device_uuid','driver_uuid':'driver_uuid','device_luid':'device_luid','vendor_id':'native_vendor_id','device_id':'native_device_id','driver_version':'native_driver_version','api_version':'native_api_version'}
    exact(d,{k:row[v] for k,v in mapping.items()});exact(d,dict(luid_valid=True,device_type=2,instance_api_version=4198400,retired=True));require(type(d['native_device']) is str and d['native_device'].isdigit() and int(d['native_device'])>0,'Actual VkDevice absent');period=d['timestamp_period_ns'];require(type(period) in (int,float) and math.isfinite(period) and period>0,'Invalid native period')
    fault='query_results_unavailable' if j['kind']=='missing-end' else 'boundary_mismatch' if j['kind']=='wrong-boundary' else '';require(d['fault']==fault,'Wrong query diagnostic');validation,w,n=options(j);frames=d['frames'];require(len(frames)==(0 if probe else w+n),'Native render pass count differs');timed=j['mode']!='audit';exact(d,dict(query_pools_created=len(frames) if timed else 0,query_pools_destroyed=len(frames) if timed else 0));values=[];queues=set()
    for index,f in enumerate(frames):
        calls,instances=WORK[j['scenario']];exact(f,dict(index=index,render_area=[0,0,65,37],scissor=[5,3,55,31],draw_calls=calls,instances=calls*instances,vertices=3*calls*instances,bad_arguments=0,pipeline_binds=1,ended=True,submitted=True,fence_completed=True,collected=True));require(type(f['scissor_commands']) is int and f['scissor_commands']>=1,'Scissor observation absent');require(type(f['queue_flags']) is int and f['queue_flags']&1 and type(f['queue_family']) is int and f['queue_family']>=0 and f['queue_index']==0,'Wrong actual graphics queue');queues.add((f['queue_family'],f['queue']))
        for key in ('command','queue','fence'):require(type(f[key]) is str and f[key].isdigit() and int(f[key])>0,'Actual submission handle absent')
        width=f['timestamp_valid_bits'];span=f['submit_to_completed_fence_ms'];require(type(width) is int and 1<=width<=64 and type(span) in (float,int) and math.isfinite(span) and span>0 and span*1e6<2**width*period,'Ambiguous counter wrap or invalid host completion span')
        if not timed:exact(f,dict(query_result=None,ticks=None,availability=None,gpu_ms=None,start_before_render_pass=False,end_after_render_pass=False));continue
        require(type(f['ticks']) is list and len(f['ticks'])==2 and all(type(v) is str and v.isdigit() and str(int(v))==v and 0<=int(v)<2**64 for v in f['ticks']),'Lossy or invalid uint64 counters')
        if j['kind']=='missing-end':exact(f,dict(query_result=1,availability=[1,0],gpu_ms=None,start_before_render_pass=True,end_after_render_pass=False));continue
        exact(f,dict(query_result=0,availability=[1,1],start_before_render_pass=j['kind']!='wrong-boundary',end_after_render_pass=True));delta=(int(f['ticks'][1])-int(f['ticks'][0]))&((1<<width)-1);elapsed=delta*period/1e6;require(type(f['gpu_ms']) in (float,int) and math.isfinite(f['gpu_ms']) and f['gpu_ms']==elapsed and elapsed<=span+.001,'GPU counter conversion/CPU fallback differs')
        if j['kind']!='wrong-boundary':require(delta>0,'Empty measured GPU interval')
        if index>=w:values.append(elapsed)
    require(len(queues)<=1,'Actual graphics queue changed');require(all(d['retired'] is True and d['query_pools_created']==d['query_pools_destroyed'] for d in native['devices']),'Device/query lifetime incomplete');mods=native['runtime_modules'];normalize=lambda p:p.replace('\\','/').casefold();require(any(normalize(p)==normalize(r['layer_library']) for p in mods),'Wrong timestamp module');require(any('vklayer_khronos_validation.dll' in p.casefold() for p in mods)==validation,'VVL mode contamination')
    if validation:require(any(normalize(p)==normalize(r['validation_library']) for p in mods),'Wrong pinned native validation module')
    return values

def verify(root=HERE):
    manifest=read(root/'manifest.json');exact(manifest,dict(schema_version=1,task='P3-BEN-09',historical_timings_reused=False));require(len(manifest['files'])==len(set(x['path'] for x in manifest['files'])),'Duplicate evidence path')
    for item in manifest['files']:
        data=(root/safe(item['path'])).read_bytes();require(sha(data)==item['sha256'] and len(data)==item['bytes'],'Evidence hash differs: '+item['path'])
    f=root/'native';r=read(f/'receipt.json');p=read(f/'plan.json');exact(r,dict(task='P3-BEN-09',status='completed',inputs_unchanged=True,GPU_timestamp_comparison_accepted=True,CPU_draw_campaigns_unchanged=True,historical_timings_reused=False,overall_SDK_ranking=False,measurement_GPU_samples=900,measurement_CPU_samples=2700,verified_images=3206,verified_bytes=30841720));require(sha((f/'plan.json').read_bytes())==r['plan_sha256'] and sha((f/'environment.json').read_bytes())==r['environment_sha256'],'Plan/environment binding differs')
    exact(p,dict(schema_version=1,task='P3-BEN-09',profile='draw_gpu',sdks=list(SDKS),scenarios=list(WORK),work={k:list(v) for k,v in WORK.items()},geometry=dict(shape=[65,37,4],scissor=[5,3,55,31],vertices_per_instance=3,first_vertex=0,first_instance=0,reference='full_changing_red_green_triangle_blue_cyan_clear'),validation=dict(warmup=1,samples=3,required_core_and_sync=True),performance=dict(warmup=5,samples=30,repetitions=5,seed=20261029,modes=list(MODES),order='seeded_scenario_blocks_five_distinct_six_SDK_mode_orders'),interval=dict(start='TOP_OF_PIPE immediately before native vkCmdBeginRenderPass',end='BOTTOM_OF_PIPE immediately after native vkCmdEndRenderPass',includes=['render_pass_clear','pipeline_scissor_draws','render_pass_end'],excludes=['resource_setup','transitions_before_pass','readback','query_result_read','CPU_record_submit_wait']),query=dict(type='timestamp',slots=2,reset='GPU vkCmdResetQueryPool outside render pass',results='64_bit_with_availability_without_WAIT_or_PARTIAL',completion='actual_submission_fence_observed_signaled',counter_encoding='unsigned_decimal_strings',mask='(end-start)&((1<<valid_bits)-1)',milliseconds='masked_delta*timestampPeriod/1e6'),statistics=dict(quantile='linear_interpolation_at_(N-1)*q',retain_all_raw_samples=True,samples_removed=0,process_medians=True),CPU_draw_campaigns_unchanged=True,serialized_processes=True,historical_timings_reused=False,overall_SDK_ranking=False,uninstrumented_GPU_time_claimed=False,Linux_hardware_accepted=False))
    with zipfile.ZipFile(root/'source.zip') as z:
        names=z.namelist();require(len(names)==len(set(names)),'Duplicate source ZIP entry')
        for name in names:safe(name)
        selected=sorted(n for n in names if n.startswith('benchmarks/vulkan_draw_timestamp/') and n.endswith(('.cpp','.hpp','CMakeLists.txt')));require(selected==['benchmarks/vulkan_draw_timestamp/CMakeLists.txt','benchmarks/vulkan_draw_timestamp/Layer.cpp'],'Timestamp native source coverage differs');fp=sha(('profile|native_render_pass_gpu_timestamp_v1\n'+''.join(n+'|'+sha(z.read(n))+'\n' for n in selected)).encode());require(fp==r['layer_fingerprint'],'Timestamp layer source differs')
        sdk=sorted(n for n in names if n=='CMakeLists.txt' or n.startswith(('src/','include/')) and n.endswith(('.cpp','.hpp')));require(len(sdk)==28 and sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in sdk).encode())==r['SDK_source_fingerprint'],'SDK source differs')
        consumer=sorted([n for n in names if n.startswith('benchmarks/vulkan_draw/') and n.endswith(('.cpp','.hpp','CMakeLists.txt'))]+['benchmarks/vulkan_upload/Identity.hpp','tests/fixtures/SpirvFixtures.hpp','tests/fixtures/spirv/fullscreen.spvasm','tests/fixtures/spirv/red.spvasm','tests/fixtures/spirv/green.spvasm','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/competitors/sources.lock.json']);require(sha(''.join(n+'|'+sha(z.read(n))+'\n' for n in consumer).encode())==r['draw_fingerprint'] and sha(z.read('tests/fixtures/SpirvFixtures.hpp'))==r['shader_header_sha256'],'Immutable caller/shader differs')
        suffix='benchmarks/vulkan_draw_timestamp/Layer.cpp';original=next(x['path'][:-len(suffix)] for x in r['source_inputs'] if x['path'].endswith(suffix));frozen={x['path'][len(original):]:x for x in r['source_inputs'] if x['path'].startswith(original)}
        for name in selected+sdk+consumer+['tools/run_draw_gpu_timestamps.py','tools/compare_llgl_draw.py','tools/run_vulkan_comparison.py','benchmarks/phase3_comparison/draw-gpu.json']:require(name in frozen and sha(z.read(name))==frozen[name]['sha256'],'ZIP source differs from executed inputs')
        require(loads(z.read('benchmarks/phase3_comparison/draw-gpu.json'))==p,'GPU contract differs from executed source')
    require((f/'settings/vk_layer_settings.txt').read_text()==SETTINGS,'Core/sync settings differ');saved=read(root/'cpu-campaign-preservation.json');exact(saved,dict(task='P3-BEN-09',CPU_draw_campaigns_unchanged=True,historical_CPU_timings_reused=False))
    for item in saved['files']:require(sha((root/'cpu-preserved'/safe(item['path'])).read_bytes())==item['sha256']==item['base_blob_sha256'],'Historical CPU contract or evidence changed')
    expected=jobs();require(len(r['runs'])==len(expected)==111,'Missing/extra native process');end=0;images=0;measurements=[];gpu_count=0
    for run,j in zip(r['runs'],expected):
        require(run['job']==j and run['overrides']==overrides(j,r),'Exact scenario/mode/order/environment differs');require(type(run['pid']) is int and run['pid']>0 and all(type(run[k]) in (int,float) and math.isfinite(run[k]) for k in ('started','finished')) and end<=run['started']<run['finished'],'Overlapping process');end=run['finished'];validation,w,n=options(j);gpu='GraphicsSDK BEN09 absent GPU' if j['kind']=='wrong-target' else r['expected_gpu'];args=[r['binary']['path'],'--probe',j['sdk'],'high',gpu] if j['kind']=='probe' else [r['binary']['path'],j['sdk'],j['scenario'],'high',str(w),str(n),str(int(validation)),gpu];require(run['argv']==args,'Native argv differs')
        stdout=(f/safe(run['stdout'])).read_bytes();stderr=(f/safe(run['stderr'])).read_bytes();require(sha(stdout)==run['stdout_sha256'] and sha(stderr)==run['stderr_sha256'],'Caller raw output differs');path=f/safe(run['native_path'])
        require((path.exists() and sha(path.read_bytes())==run['native_sha256']) or (not path.exists() and run['native_sha256'] is None),'Raw counter output differs')
        if j['kind'] not in SUCCESS:
            require(type(run['returncode']) is int and run['returncode']!=0 and not stdout,'Negative control produced caller samples');token=b'VUID-VkPhysicalDeviceProperties2-sType-sType' if j['kind']=='validation-control' else b'VK_LAYER_KHRONOS_validation is not installed' if j['kind']=='missing-layer' else b'FAIL: Unique exact SDK GPU required' if j['sdk']=='graphicssdk' else b'FAIL: Selected GPU differs from expected GPU';require(token in stderr,'Unrelated negative failure')
            if j['kind']=='validation-control':
                data=read(path);exact(data,dict(schema_version=1,task='P3-BEN-09',mode='validation-control',layer_fingerprint=r['layer_fingerprint'],live_instances=0,live_devices=0));require(token.decode() in data['validation_diagnostics'] and data['devices'],'Actual callback absent');layer_trace(stderr)
                for d in data['devices']:exact(d,dict(fault='validation_control_only',retired=True,frames=[],query_pools_created=0,query_pools_destroyed=0,vendor_id=4318,device_uuid=r['identity']['device_uuid']))
            continue
        exact(run,dict(returncode=0));row=loads(stdout);require(row==run['result'],'Parsed caller differs');exact(row,r['identity']);probe=j['kind']=='probe';calls,instances=(0,0) if probe else WORK[j['scenario']];count=0 if probe else w+n
        exact(row,dict(schema_version=1,sdk=j['sdk'],api='vulkan',configuration='Release',category='end_to_end',contract='vulkan_scissored_fullscreen_triangle_draw_fence_each_sample_readback_v1',scenario=j['scenario'],preference='high',validation=validation,probe=probe,warmup=w,sample_count=n,source_fingerprint=r['draw_fingerprint'],SDK_source_fingerprint=r['SDK_source_fingerprint'],SDL_commit=r['SDL_commit'],shader_header_sha256=r['shader_header_sha256'],requested_instance_api_version=4198400,gpu_ms=None,width=65,height=37,texel_bytes=4,scissor=[5,3,55,31],vertices_per_instance=3,first_vertex=0,first_instance=0,draw_calls_per_frame=calls,instances_per_call=instances,vertices_per_frame=3*calls*instances,submitted_draw_calls=count*calls,verified_images=count,verified_bytes=count*9620,readback_verified=not probe,columns=['record_ms','submit_ms','wait_ms','total_ms']))
        require(row['device_name']==r['expected_gpu'] and row['native_vendor_id']==4318,'Wrong GPU')
        for key,size in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:require(re.fullmatch('[0-9a-f]{'+str(size)+'}',row[key]) and int(row[key],16),'Missing native identity')
        e=row['environment'];high,low=map(int,e['luid'].split(':'));require(struct.pack('<II',low,high&0xffffffff).hex()==row['device_luid'] and e['gpu']==row['device_name'] and e['vendor_id']==row['native_vendor_id'] and e['device_id']==row['native_device_id'] and e['driver_query_available'] is True and e['software_renderer'] is False and e['pointer_bits']==64,'Native/DXGI identity differs');values=row['samples_ms'];intervals=row['CPU_intervals_ms'];require(type(values) is list and len(values)==n and type(intervals) is list and len(intervals)==n,'Missing CPU impact data')
        for value,t in zip(values,intervals):require(type(t) is list and len(t)==4 and all(type(v) in (float,int) and math.isfinite(v) and v>=0 for v in t) and t[-1]==value and value>0 and abs(sum(t[:3])-value)<=max(1e-6,value*1e-10),'CPU intervals differ')
        require(not re.search(rb'Validation Error|SYNC-HAZARD',stderr),'Normal workload validation error');require(bool(row['loaded_validation_layer'])==validation and (not validation or row['loaded_validation_layer'].replace('\\','/').casefold()==r['validation_library'].casefold()),'Wrong caller validation module')
        if validation:
            require(b'VkLayer_khronos_validation.dll' in stderr,'Pinned layer trace absent')
            if j['kind']!='missing-timestamp-layer':layer_trace(stderr)
        images+=count
        if j['mode']=='baseline' or j['kind']=='missing-timestamp-layer':require(not path.exists() and run['gpu_ms'] is None,'Unobserved GPU interval claimed')
        else:values=counter_result(j,read(path),row,r);require(run['gpu_ms']==(values if j['mode']=='timestamp' else None),'Retained GPU counter slice differs')
        if j['kind']=='measurement':measurements.append(run);gpu_count+=len(run['gpu_ms'] or [])
    require(images==3206 and len(measurements)==90 and gpu_count==900,'GPU/impact/full-reference totals differ');groups=[]
    for name in WORK:
        for sdk in SDKS:
            for mode in MODES:
                selected=[x for x in measurements if (x['job']['scenario'],x['job']['sdk'],x['job']['mode'])==(name,sdk,mode)];require([x['job']['repetition'] for x in selected]==list(range(5)),'Missing independent repetition');values=dict(cpu_total_ms=stats([x['result']['samples_ms'] for x in selected]))
                if mode=='timestamp':values['gpu_ms']=stats([x['gpu_ms'] for x in selected])
                groups.append(dict(scenario=name,sdk=sdk,mode=mode,statistics=values))
    require(groups==r['groups'],'GPU/CPU impact statistics do not reproduce');print('PASS saved BEN-09: 111 processes, 900 native GPU samples, 2700 CPU impact samples, 3206 full images; actual device/queue/fence/availability verified');return r

if __name__=='__main__':verify()

