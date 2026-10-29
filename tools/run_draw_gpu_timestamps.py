"""P3-BEN-09 dedicated native GPU counters; CPU-only draw campaigns stay immutable."""
import argparse
import hashlib
import itertools
import json
import math
import os
import pathlib
import random
import re
import subprocess
import time

import prepare_vulkan_comparison as inputs
import run_vulkan_comparison as protocol
import run_vulkan_draw as draw
from compare_llgl_draw import exact,validate_common
from audit_licenses import parse_tlog,source_notices
from audit_vulkan import system_input
from prepare_vulkan_comparison import ROOT,read,save,sha,record,require,unchanged

PLAN=ROOT/'benchmarks/phase3_comparison/draw-gpu.json'
SDKS=('graphicssdk','sdl')
MODES=('baseline','audit','timestamp')
SUCCESS=('probe','validation','validation-audit','missing-timestamp-layer','missing-end','wrong-boundary','measurement')
COMMON_NATIVE={'device_name':'device_name','device_uuid':'device_uuid','driver_uuid':'driver_uuid','device_luid':'device_luid','vendor_id':'native_vendor_id','device_id':'native_device_id','driver_version':'native_driver_version','api_version':'native_api_version'}

def source_files(root=ROOT):return sorted([root/'benchmarks/vulkan_draw_timestamp/CMakeLists.txt',root/'benchmarks/vulkan_draw_timestamp/Layer.cpp'],key=lambda p:p.as_posix())

def fingerprint(root=ROOT):return hashlib.sha256(('profile|native_render_pass_gpu_timestamp_v1\n'+''.join(p.relative_to(root).as_posix()+'|'+sha(p)+'\n' for p in source_files(root))).encode()).hexdigest()

def validate_plan(p):
    old=read(ROOT/'benchmarks/phase3_comparison/draw.json')
    expected=dict(schema_version=1,task='P3-BEN-09',profile='draw_gpu',sdks=list(SDKS),scenarios=list(draw.WORK),work=old['work'],geometry=old['geometry'],validation=old['validation'],
        performance=dict(warmup=5,samples=30,repetitions=5,seed=20261029,modes=list(MODES),order='seeded_scenario_blocks_five_distinct_six_SDK_mode_orders'),
        interval=dict(start='TOP_OF_PIPE immediately before native vkCmdBeginRenderPass',end='BOTTOM_OF_PIPE immediately after native vkCmdEndRenderPass',includes=['render_pass_clear','pipeline_scissor_draws','render_pass_end'],excludes=['resource_setup','transitions_before_pass','readback','query_result_read','CPU_record_submit_wait']),
        query=dict(type='timestamp',slots=2,reset='GPU vkCmdResetQueryPool outside render pass',results='64_bit_with_availability_without_WAIT_or_PARTIAL',completion='actual_submission_fence_observed_signaled',counter_encoding='unsigned_decimal_strings',mask='(end-start)&((1<<valid_bits)-1)',milliseconds='masked_delta*timestampPeriod/1e6'),
        statistics=old['statistics'],CPU_draw_campaigns_unchanged=True,serialized_processes=True,historical_timings_reused=False,overall_SDK_ranking=False,uninstrumented_GPU_time_claimed=False,Linux_hardware_accepted=False)
    require(json.dumps(p,sort_keys=True,allow_nan=False)==json.dumps(expected,sort_keys=True,allow_nan=False),'GPU timestamp contract drift');return p

def schedule(p):
    jobs=[dict(kind='probe',sdk=s,scenario='probe',mode='audit',repetition=0) for s in SDKS]
    jobs += [dict(kind='validation',sdk=s,scenario=n,mode='timestamp',repetition=0) for n in draw.WORK for s in SDKS]
    jobs += [dict(kind='validation-audit',sdk=s,scenario='draw1',mode='audit',repetition=0) for s in SDKS]
    jobs += [dict(kind=k,sdk=s,scenario='draw1',mode=k,repetition=0) for s in SDKS for k in ('missing-end','wrong-boundary')]
    jobs += [dict(kind=k,sdk=s,scenario='draw1',mode='timestamp' if k!='wrong-target' else 'audit',repetition=0) for s in SDKS for k in ('missing-layer','missing-timestamp-layer','wrong-target')]
    jobs += [dict(kind='validation-control',sdk='graphicssdk',scenario='draw1',mode='validation-control',repetition=0)]
    rng=random.Random(p['performance']['seed']);orders={n:rng.sample(list(itertools.permutations(itertools.product(SDKS,MODES))),5) for n in draw.WORK}
    for rep in range(5):
        names=list(draw.WORK);rng.shuffle(names)
        for n in names:jobs += [dict(kind='measurement',sdk=s,scenario=n,mode=m,repetition=rep) for s,m in orders[n][rep]]
    for i,j in enumerate(jobs):j['name']=f"{i:03}-{j['kind']}-{j['sdk']}-{j['scenario']}-{j['mode']}-{j['repetition']}"
    return jobs

def options(j):
    k=j['kind'];validation=k in ('validation','validation-audit','missing-layer','missing-timestamp-layer','missing-end','wrong-boundary','validation-control');w,n=(0,0) if k=='probe' else (5,30) if k=='measurement' else (1,3);return validation,w,n

def arguments(j,r):
    validation,w,n=options(j);gpu='GraphicsSDK BEN09 absent GPU' if j['kind']=='wrong-target' else r['expected_gpu'];exe=r['binary']['path']
    return [exe,'--probe',j['sdk'],'high',gpu] if j['kind']=='probe' else [exe,j['sdk'],j['scenario'],'high',str(w),str(n),str(int(validation)),gpu]

def overrides(j,r):
    validation,_,_=options(j);out=r['output_directory'];env=dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=out+'/empty',VK_LAYER_SETTINGS_PATH=out+'/empty')
    layered=j['mode']!='baseline' and j['kind'] not in ('missing-layer','missing-timestamp-layer')
    if layered:env.update(VK_LAYER_PATH=r['layer_directory'],VK_INSTANCE_LAYERS='VK_LAYER_GFX_draw_timestamp',GFX_GPU_TIMESTAMP_MODE=j['mode'],GFX_GPU_TIMESTAMP_OUTPUT=out+'/commands/'+j['name']+'.layer.json')
    if validation:
        env.update(VK_LAYER_PATH=(r['layer_directory']+';' if layered else '')+r['validation_library'].rsplit('/',1)[0],VK_INSTANCE_LAYERS=('VK_LAYER_GFX_draw_timestamp;' if layered else '')+'VK_LAYER_KHRONOS_validation',VK_LAYER_SETTINGS_PATH=out+'/settings',VK_KHRONOS_VALIDATION_VALIDATE_SYNC='true',VK_LOADER_DEBUG='layer',GFX_EXPECT_VULKAN_LAYER=r['validation_library'])
    if j['kind']=='missing-layer':env['VK_LAYER_PATH']=out+'/missing'
    return env

def caller(j,row,r):
    validation,w,n=options(j);probe=j['kind']=='probe';name=j['scenario'];identity=validate_common(row,r.get('identity'))
    exact(row,dict(schema_version=1,sdk=j['sdk'],api='vulkan',configuration='Release',category='end_to_end',contract=draw.CONTRACT,scenario=name,preference='high',validation=validation,probe=probe,warmup=w,sample_count=n,source_fingerprint=r['draw_fingerprint'],SDK_source_fingerprint=r['SDK_source_fingerprint'],SDL_commit=r['SDL_commit'],shader_header_sha256=r['shader_header_sha256'],requested_instance_api_version=4198400,gpu_ms=None),'Caller/source/request metadata differs')
    count,instances=(0,0) if probe else draw.WORK[name];images=0 if probe else w+n
    exact(row,dict(width=65,height=37,texel_bytes=4,scissor=[5,3,55,31],vertices_per_instance=3,first_vertex=0,first_instance=0,draw_calls_per_frame=count,instances_per_call=instances,vertices_per_frame=3*count*instances,submitted_draw_calls=images*count,verified_images=images,verified_bytes=9620*images,readback_verified=not probe,columns=['record_ms','submit_ms','wait_ms','total_ms']),'Caller full reference/work differs')
    require(bool(row['loaded_validation_layer'])==validation,'Wrong caller validation mode')
    if validation:require(protocol.normalized_path(row['loaded_validation_layer'])==protocol.normalized_path(r['validation_library']),'Wrong pinned validation runtime')
    values=row['samples_ms'];intervals=row['CPU_intervals_ms'];require(type(values) is list and len(values)==n and type(intervals) is list and len(intervals)==n,'Caller CPU sample count differs')
    for value,t in zip(values,intervals):require(type(t) is list and len(t)==4 and all(type(v) in (int,float) and math.isfinite(v) and v>=0 for v in t) and t[-1]==value and value>0 and abs(sum(t[:3])-value)<=max(1e-6,value*1e-10),'Caller CPU intervals invalid')
    return identity

def native(j,data,r,row):
    exact(data,dict(schema_version=1,task='P3-BEN-09',layer_fingerprint=r['layer_fingerprint'],mode=j['mode'],live_instances=0,live_devices=0),'Native layer mode/lifetime/source differs')
    require(type(data['created_instances']) is int and data['created_instances']>0 and data['created_instances']==data['destroyed_instances'] and data['validation_diagnostics']==[],'Unretired instance or unexpected diagnostic')
    selected=[d for d in data['devices'] if d['frames']];probe=j['kind']=='probe'
    require(len(selected)==(0 if probe else 1) and data['devices'],'Ambiguous native sample device');d=data['devices'][-1] if probe else selected[0]
    for k,caller_key in COMMON_NATIVE.items():require(type(d[k]) is type(row[caller_key]) and d[k]==row[caller_key],'Actual renderer native identity differs')
    exact(d,dict(luid_valid=True,device_type=2,instance_api_version=4198400,retired=True),'Actual renderer/API/retirement differs');require(type(d['timestamp_period_ns']) in (int,float) and math.isfinite(d['timestamp_period_ns']) and d['timestamp_period_ns']>0,'Invalid actual period');require(type(d['native_device']) is str and d['native_device'].isdigit() and int(d['native_device'])>0,'Native device handle absent')
    expected_fault='query_results_unavailable' if j['kind']=='missing-end' else 'boundary_mismatch' if j['kind']=='wrong-boundary' else '';require(d['fault']==expected_fault,'Unexpected native query/boundary failure')
    validation,w,n=options(j);frames=d['frames'];require(len(frames)==(0 if probe else w+n),'Missing/extra native render passes');timed=j['mode']!='audit'
    exact(d,dict(query_pools_created=len(frames) if timed else 0,query_pools_destroyed=len(frames) if timed else 0),'Unretired/missing native query pools')
    gpu=[];family=None;queue=None
    for index,f in enumerate(frames):
        calls,instances=draw.WORK[j['scenario']];exact(f,dict(index=index,render_area=[0,0,65,37],scissor=[5,3,55,31],draw_calls=calls,instances=calls*instances,vertices=3*calls*instances,bad_arguments=0,pipeline_binds=1,ended=True,submitted=True,fence_completed=True,collected=True),'Native interval/work/fence differs')
        require(type(f['scissor_commands']) is int and f['scissor_commands']>=1 and type(f['queue_flags']) is int and f['queue_flags']&1 and type(f['queue_family']) is int and f['queue_family']>=0 and f['queue_index']==0,'Invalid actual graphics queue')
        for key in ('command','queue','fence'):require(type(f[key]) is str and f[key].isdigit() and int(f[key])>0,'Native submission handle absent')
        family=f['queue_family'] if family is None else family;queue=f['queue'] if queue is None else queue;require(f['queue_family']==family and f['queue']==queue,'Queue changed within process')
        valid=f['timestamp_valid_bits'];period=d['timestamp_period_ns'];host=f['submit_to_completed_fence_ms'];require(type(valid) is int and 1<=valid<=64 and type(host) in (int,float) and math.isfinite(host) and host>0 and host*1e6<2**valid*period,'Invalid/ambiguous counter width or completion span')
        if not timed:exact(f,dict(ticks=None,availability=None,query_result=None,gpu_ms=None,start_before_render_pass=False,end_after_render_pass=False),'Audit claims counters');continue
        require(type(f['ticks']) is list and len(f['ticks'])==2 and all(type(v) is str and v.isdigit() and str(int(v))==v and 0<=int(v)<2**64 for v in f['ticks']),'Raw counters must be exact uint64 strings')
        if j['kind']=='missing-end':exact(f,dict(query_result=1,availability=[1,0],gpu_ms=None,start_before_render_pass=True,end_after_render_pass=False),'Missing end did not produce actual unavailable query');continue
        exact(f,dict(query_result=0,availability=[1,1],start_before_render_pass=j['kind']!='wrong-boundary',end_after_render_pass=True),'Native query availability/boundary differs')
        delta=(int(f['ticks'][1])-int(f['ticks'][0]))&((1<<valid)-1);ms=delta*period/1e6;require(type(f['gpu_ms']) in (float,int) and math.isfinite(f['gpu_ms']) and f['gpu_ms']==ms and ms<=host+0.001,'Counter conversion or CPU fallback')
        if j['kind']!='wrong-boundary':require(delta>0,'Empty GPU interval')
        if index>=w:gpu.append(ms)
    for other in data['devices']:require(other['retired'] is True and other['query_pools_created']==other['query_pools_destroyed'],'Native device/query lifetime incomplete')
    modules=data['runtime_modules'];require(modules and any(protocol.normalized_path(p)==protocol.normalized_path(r['layer_library']) for p in modules),'Actual timestamp DLL not recorded');require(any('vklayer_khronos_validation.dll' in p.casefold() for p in modules)==validation,'Wrong VVL isolation')
    if validation:require(any(protocol.normalized_path(p)==protocol.normalized_path(r['validation_library']) for p in modules),'Wrong native validation module')
    return gpu

def summarize(rows):
    groups=[]
    for name in draw.WORK:
        for sdk in SDKS:
            for mode in MODES:
                selected=[x for x in rows if x['job']['kind']=='measurement' and (x['job']['scenario'],x['job']['sdk'],x['job']['mode'])==(name,sdk,mode)];require([x['job']['repetition'] for x in selected]==list(range(5)),'Missing/duplicate independent process')
                values=dict(cpu_total_ms=protocol.scalar_stats([x['result']['samples_ms'] for x in selected]))
                if mode=='timestamp':values['gpu_ms']=protocol.scalar_stats([x['gpu_ms'] for x in selected])
                groups.append(dict(scenario=name,sdk=sdk,mode=mode,statistics=values))
    return groups

def collect(build):
    logs=[p for p in build.rglob('*.read.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts and p.name in ('CL.read.1.tlog','link.read.1.tlog')];commands=[p for p in build.rglob('CL.command.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts];require(len(logs)==2 and len(commands)==1,'Native compiler/linker evidence absent')
    for p in commands:inputs.validate_compiler_flags(p.read_bytes().decode('utf-16'))
    folder=ROOT/'.deps/Vulkan-Headers';tracked=inputs.pin(folder,read(ROOT/'licenses/vulkan-presentation-policy.json')['dependency']['commit']);files=[]
    for name in sorted(set(x for p in logs for x in parse_tlog(p.read_bytes())),key=str.casefold):
        p=pathlib.Path(name).resolve()
        if p.suffix.lower() in ('.obj','.pch','.pdb','.tlog'):continue
        if p in source_files():kind='own_timestamp_layer'
        elif p.is_relative_to(folder):require(p.relative_to(folder).as_posix().casefold() in tracked and 'SPDX-License-Identifier: Apache-2.0' in p.read_text(encoding='utf-8-sig'),'Unreviewed Vulkan header');kind='vulkan_headers'
        elif system_input(p):kind='system_prerequisite'
        else:raise ValueError('Unreviewed timestamp layer input '+str(p))
        files.append(dict(**record(p),component=kind))
    return dict(files=files,read_logs=[record(p) for p in logs],compiler_commands=[record(p) for p in commands])

def rejection(j,code,out,err):
    require(code!=0 and not out,'Native rejection produced successful samples')
    token=b'VUID-VkPhysicalDeviceProperties2-sType-sType' if j['kind']=='validation-control' else b'VK_LAYER_KHRONOS_validation is not installed' if j['kind']=='missing-layer' else b'FAIL: Unique exact SDK GPU required' if j['sdk']=='graphicssdk' else b'FAIL: Selected GPU differs from expected GPU'
    require(token in err,'Unrelated failure accepted as control')

def layer_trace(err):
    stacks=re.findall(rb'vkCreate(?:Instance|Device) layer callstack setup to:(.*?)<Drivers>',err,re.S)
    matching=[s for s in stacks if b'VK_LAYER_GFX_draw_timestamp' in s]
    require(matching and all(b'VK_LAYER_KHRONOS_validation' in s and s.index(b'VK_LAYER_GFX_draw_timestamp')<s.index(b'VK_LAYER_KHRONOS_validation') for s in matching),'Timestamp layer must precede VVL in the actual loader callstack')

def validation_control(data,r):
    exact(data,dict(schema_version=1,task='P3-BEN-09',mode='validation-control',layer_fingerprint=r['layer_fingerprint'],live_instances=0,live_devices=0),'Wrong diagnostic layer/context')
    require('VUID-VkPhysicalDeviceProperties2-sType-sType' in data['validation_diagnostics'] and data['devices'],'Actual validation callback absent')
    for d in data['devices']:exact(d,dict(fault='validation_control_only',retired=True,frames=[],query_pools_created=0,query_pools_destroyed=0,vendor_id=4318,device_uuid=r['identity']['device_uuid']),'Control submitted GPU work or used wrong device')

def check(folder,live=False):
    r=read(folder/'receipt.json');p=validate_plan(read(folder/'plan.json'));exact(r,dict(task='P3-BEN-09',status='completed',inputs_unchanged=True,GPU_timestamp_comparison_accepted=True,CPU_draw_campaigns_unchanged=True,historical_timings_reused=False,overall_SDK_ranking=False,measurement_GPU_samples=900,measurement_CPU_samples=2700,verified_images=3206,verified_bytes=30841720),'Incomplete GPU comparison')
    require(r['plan_sha256']==sha(folder/'plan.json') and r['environment_sha256']==sha(folder/'environment.json'),'Plan/environment changed');require((folder/'settings/vk_layer_settings.txt').read_text()==protocol.SETTINGS,'Required sync settings changed')
    jobs=schedule(p);require(len(jobs)==len(r['runs'])==111,'Missing/extra process');end=0;images=0
    for row,j in zip(r['runs'],jobs):
        require(row['job']==j and row['argv']==arguments(j,r) and row['overrides']==overrides(j,r),'Schedule/arguments/environment drift');require(type(row['pid']) is int and row['pid']>0 and end<=row['started']<row['finished'],'Invalid/overlapping process');end=row['finished']
        out=(folder/row['stdout']).read_bytes();err=(folder/row['stderr']).read_bytes();require(sha(folder/row['stdout'])==row['stdout_sha256'] and sha(folder/row['stderr'])==row['stderr_sha256'],'Raw caller output changed');path=folder/row['native_path']
        if path.exists():require(sha(path)==row['native_sha256'],'Native raw output changed')
        else:require(row['native_sha256'] is None,'Missing native raw output')
        if j['kind'] not in SUCCESS:
            rejection(j,row['returncode'],out,err)
            if j['kind']=='validation-control':validation_control(read(path),r);layer_trace(err)
            continue
        require(row['returncode']==0,'Native caller failed');data=json.loads(out);require(data==row['result'],'Raw/parsed caller differs');caller(j,data,r);require(not re.search(rb'Validation Error|SYNC-HAZARD',err),'Native workload validation error');images+=data['verified_images']
        if options(j)[0]:require(b'VK_LAYER_GFX_draw_timestamp' in err or j['kind']=='missing-timestamp-layer','Layer trace absent');require(b'VkLayer_khronos_validation.dll' in err,'Required VVL trace absent')
        if options(j)[0] and j['kind']!='missing-timestamp-layer':layer_trace(err)
        if j['mode']=='baseline' or j['kind']=='missing-timestamp-layer':require(not path.exists() and row['native_sha256'] is None and row['gpu_ms'] is None,'Unobserved process claims GPU data');continue
        require(path.is_file() and sha(path)==row['native_sha256'],'Native raw counter evidence changed');values=native(j,read(path),r,data);require(row['gpu_ms']==(values if j['mode']=='timestamp' else None),'Retained GPU sample slice differs')
    require(images==3206 and r['groups']==summarize(r['runs']),'Full references or separated statistics differ')
    if live:require(unchanged(r['source_inputs']),'GPU campaign inputs changed')
    return dict(task='P3-BEN-09',processes=111,GPU_samples=900,CPU_impact_samples=2700,verified_images=3206,verified_bytes=30841720,accepted=True)

def run(build,prepared,out,gpu):
    require(os.name=='nt','Native Windows GPU required');p=validate_plan(read(PLAN));out=out.resolve();build=build.resolve();require(out.is_relative_to(ROOT/'out') and not out.exists(),'Fresh output under out/ required');inputs.check(prepared,live=True);pr=read(prepared/'receipt.json');audit=collect(build);runtime,vvl=protocol.runtime_inputs(ROOT/'out/vulkan-validation-runtime/receipt.json');dll=build/'Release/VkLayer_gfx_draw_timestamp.dll';manifest=dll.with_suffix('.json');fp=fingerprint();require(fp.encode() in dll.read_bytes(),'Stale timestamp layer');require(protocol.normalized_path(read(manifest)['layer']['library_path'])==protocol.normalized_path(dll),'Absolute layer manifest differs')
    out.mkdir(parents=True)
    for name in ('empty','missing','settings','commands'):(out/name).mkdir()
    (out/'settings/vk_layer_settings.txt').write_bytes(protocol.SETTINGS.encode());save(out/'plan.json',p);save(out/'inputs.json',audit);save(out/'prepared-receipt.json',pr);save(out/'prepared-inputs.json',read(prepared/'inputs.json'));save(out/'runtime-receipt.json',runtime)
    clean={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_'))};environment=dict(removed_inherited_keys=sorted(set(os.environ)-set(clean)),base_environment_values_not_archived=True);save(out/'environment.json',environment)
    paths=source_files()+inputs.source_paths()+[PLAN,pathlib.Path(__file__),ROOT/'tools/compare_llgl_draw.py',ROOT/'tools/run_vulkan_comparison.py',dll,manifest,vvl,build/'CMakeCache.txt',out/'settings/vk_layer_settings.txt']+[pathlib.Path(x['path']) for x in read(prepared/'inputs.json')['files']]
    paths += [pathlib.Path(x['path']) for key in ('files','read_logs','compiler_commands') for x in audit[key]];frozen=[record(x) for x in sorted(set(paths))]
    r=dict(task='P3-BEN-09',status='running',source_commit=inputs.git(ROOT,'rev-parse','HEAD'),expected_gpu=gpu,output_directory=out.as_posix(),binary=pr['binaries']['draw'],layer_directory=dll.parent.as_posix(),layer_library=dll.as_posix(),layer_fingerprint=fp,layer_library_sha256=sha(dll),validation_library=vvl.as_posix(),validation_library_sha256=sha(vvl),SDK_source_fingerprint=pr['SDK_source_fingerprint'],draw_fingerprint=pr['profile_fingerprints']['draw'],shader_header_sha256=sha(ROOT/'tests/fixtures/SpirvFixtures.hpp'),SDL_commit=pr['SDL_commit'],plan_sha256=sha(out/'plan.json'),environment_sha256=sha(out/'environment.json'),source_inputs=frozen,CPU_draw_campaigns_unchanged=True,historical_timings_reused=False,overall_SDK_ranking=False,GPU_timestamp_comparison_accepted=False,runs=[],groups=[])
    runtime_files={}
    try:
        for j in schedule(p):
            require(unchanged(frozen),'Frozen GPU inputs changed');args=arguments(j,r);env=overrides(j,r);path=out/'commands'/(j['name']+'.layer.json');require(not path.exists(),'Stale GPU result');started=time.monotonic();child=subprocess.Popen(args,cwd=ROOT,env={**clean,**env},stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:stdout,stderr=child.communicate(timeout=120)
            except subprocess.TimeoutExpired:child.kill();stdout,stderr=child.communicate();(out/'commands'/(j['name']+'.timeout.stdout')).write_bytes(stdout);(out/'commands'/(j['name']+'.timeout.stderr')).write_bytes(stderr);raise
            row=dict(job=j,argv=args,overrides=env,pid=child.pid,started=started,finished=time.monotonic(),returncode=child.returncode,native_path=path.relative_to(out).as_posix(),native_sha256=sha(path) if path.exists() else None,gpu_ms=None)
            for key,data in [('stdout',stdout),('stderr',stderr)]:
                q=out/'commands'/(j['name']+'.'+key);q.write_bytes(data);row[key]=q.relative_to(out).as_posix();row[key+'_sha256']=sha(q)
            r['runs'].append(row);save(out/'receipt.json',r)
            if j['kind'] not in SUCCESS:
                rejection(j,child.returncode,stdout,stderr)
                if j['kind']=='validation-control':validation_control(read(path),r);layer_trace(stderr)
            else:
                require(child.returncode==0,'GPU caller failed '+j['name']);data=json.loads(stdout);identity=caller(j,data,r);row['result']=data
                if 'identity' not in r:r['identity']=identity
                require(not re.search(rb'Validation Error|SYNC-HAZARD',stderr),'Native workload validation error')
                if j['mode']=='baseline' or j['kind']=='missing-timestamp-layer':require(not path.exists(),'Unexpected GPU report without layer')
                else:
                    require(path.exists(),'Native GPU query report absent');layer_data=read(path);values=native(j,layer_data,r,data)
                    if j['mode']=='timestamp':row['gpu_ms']=values
                    for name in layer_data['runtime_modules']:
                        q=pathlib.Path(name).resolve();require(q.is_relative_to(pathlib.Path(os.environ['WINDIR'])) or q in (pathlib.Path(r['binary']['path']).resolve(),dll.resolve(),vvl.resolve()),'Unreviewed native runtime module');runtime_files[q.as_posix()]=record(q)
            save(out/'receipt.json',r);print(j['name'],'PASS',flush=True)
        require(unchanged(frozen),'Final GPU inputs changed');save(out/'runtime-inputs.json',dict(files=list(runtime_files.values()),redistributed=False));r.update(status='completed',inputs_unchanged=True,GPU_timestamp_comparison_accepted=True,measurement_GPU_samples=900,measurement_CPU_samples=2700,verified_images=3206,verified_bytes=30841720,groups=summarize(r['runs']));save(out/'receipt.json',r);print(json.dumps(check(out,live=True)),flush=True)
    except Exception as e:r.update(status='failed',GPU_timestamp_comparison_accepted=False,error=str(e));save(out/'receipt.json',r);raise

def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('stage',choices=('run','check'));a.add_argument('--build-root',type=pathlib.Path);a.add_argument('--prepared',type=pathlib.Path,default=ROOT/'out/phase3-current-upload-inputs-20261027/evidence');a.add_argument('--output',type=pathlib.Path,required=True);a.add_argument('--gpu-name',default='NVIDIA GeForce GTX 1660 SUPER');a.add_argument('--live',action='store_true');p=a.parse_args()
    if p.stage=='check':print(json.dumps(check(p.output,p.live)));return
    require(p.build_root is not None,'Native layer build required');run(p.build_root,p.prepared,p.output,p.gpu_name)

if __name__=='__main__':main()
