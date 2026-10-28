"""Build Linux adapters for four frozen workloads; qualified software diagnostics only.

Smoke timing values are retained as validation output and never summarized as
performance. Physical BEN-08 acceptance requires physical BEN-07 qualification.
"""
import argparse
import hashlib
import json
import math
import os
import pathlib
import platform
import re
import shutil
import subprocess
import time
import linux_comparison as q
import port_linux_benchmarks as port
from linux_comparison import ROOT,read,save,sha,record,require

SCENARIOS=dict(upload='odd',draw='draw1',pso='pso1',allocation='buffer1')
ATTRIBUTION='unique_exact_name_to_native_vulkan_not_SDL_native_handle'

def schedule():
    jobs=[dict(profile=p,sdk=sdk,kind='probe') for p in port.PROFILES for sdk in q.SDKS]
    jobs += [dict(profile=p,sdk=sdk,kind='validation-smoke') for p in port.PROFILES for sdk in q.SDKS]
    jobs += [dict(profile=p,sdk=sdk,kind=kind) for p in port.PROFILES for sdk in q.SDKS for kind in ('wrong-uuid','missing-layer','wrong-kind')]
    return [dict(j,name=f"{i:02}-{j['kind']}-{j['profile']}-{j['sdk']}") for i,j in enumerate(jobs)]

def arguments(job,r):
    exe=r['binaries'][job['profile']]['path'];gpu=r['qualification']['selected']['name']
    if job['kind']=='probe':return [exe,'--probe',job['sdk'],'high',gpu]
    validation=job['kind'] in ('validation-smoke','missing-layer')
    return [exe,job['sdk'],SCENARIOS[job['profile']],'high','1','3',str(int(validation)),gpu]

def environment(job,r):
    a=r['qualification'];validation=job['kind'] in ('validation-smoke','missing-layer')
    result=q.overrides(pathlib.Path(r['output_directory']),a['icd']['path'],a['validation_library'],validation)
    result.update(GFX_COMPARISON_UUID='0'*32 if job['kind']=='wrong-uuid' else a['device_uuid'],GFX_COMPARISON_MODE=('physical' if a['mode']=='software-diagnostic' else 'software-diagnostic') if job['kind']=='wrong-kind' else a['mode'])
    if job['kind']=='missing-layer':result['VK_LAYER_PATH']=str(pathlib.Path(r['output_directory'])/'empty')
    return result

def validate_result(value,job,context):
    profile=job['profile'];probe=job['kind']=='probe';a=context['qualification'];native=a['selected']
    spec=read(ROOT/'benchmarks/phase3_comparison/contract.json')['profiles'][profile]
    expected=dict(schema_version=1,sdk=job['sdk'],api='vulkan',category=spec['category'],contract=spec['contract'],probe=probe,scenario='probe' if probe else SCENARIOS[profile],preference='high',configuration='Release',validation=not probe,
        source_fingerprint=context['profile_fingerprints'][profile],SDK_source_fingerprint=a['SDK_source_fingerprint'],SDL_commit=a['SDL_commit'],device_name=native['name'],device_uuid=native['device_uuid'],driver_uuid=native['driver_uuid'],device_luid=None,
        native_vendor_id=native['vendor_id'],native_device_id=native['device_id'],native_driver_version=native['driver_version_raw'],native_api_version=native['api_version_raw'],identity_attribution=ATTRIBUTION,
        warmup=0 if probe else 1,sample_count=0 if probe else 3,readback_verified=not probe,gpu_ms=None,loaded_validation_layer='' if probe else a['validation_library'])
    require(all(k in value and value[k]==v and type(value[k]) is type(v) for k,v in expected.items()),'Linux workload identity/source/mode contract changed')
    env=value['environment'];require(all(env[k]==v and type(env[k]) is type(v) for k,v in a['environment'].items()),'Native environment changed')
    require(env['gpu']==native['name'] and env['software_renderer']==(native['device_type']==4) and type(env['software_renderer']) is bool
            and env['device_uuid']==native['device_uuid'] and env['driver_uuid']==native['driver_uuid'] and env['device_luid'] is None,'Native environment target changed')
    rows=value['samples_ms'];require(isinstance(rows,list) and len(rows)==(0 if probe else 3),'Probe/smoke sample count differs')
    if profile in ('pso','allocation'):
        require(value['columns']==['create_ms','public_release_ms'],'CPU API columns changed')
        require(all(isinstance(row,list) and len(row)==2 and all(type(x) in (int,float) and math.isfinite(x) and x>=0 for x in row) and row[0]>0 for row in rows),'Invalid CPU API intervals')
    else:require(all(type(x) in (int,float) and math.isfinite(x) and x>0 for x in rows),'Invalid completed CPU intervals')
    if profile=='draw':
        intervals=value['CPU_intervals_ms'];require(len(intervals)==len(rows) and value['columns']==['record_ms','submit_ms','wait_ms','total_ms'],'Draw CPU intervals changed')
        require(all(len(row)==4 and all(type(x) in (int,float) and math.isfinite(x) and x>=0 for x in row) and row[3]==total and abs(sum(row[:3])-total)<=max(1e-6,total*1e-10) for row,total in zip(intervals,rows)),'Draw total does not partition')
    total=0 if probe else 4;images=total if profile!='allocation' else 0
    require(type(value['verified_images']) is int and value['verified_images']==images and type(value['verified_bytes']) is int and value['verified_bytes']==total*(4096 if profile=='allocation' else 65*37*4),'Full logical readback differs')
    shape=[0,0,0] if profile=='allocation' or (profile=='upload' and probe) else [65,37,4]
    require([value[k] for k in ('width','height','texel_bytes')]==shape and all(type(value[k]) is int for k in ('width','height','texel_bytes')),'Image shape changed')
    for k,v in value.items():
        if k.startswith(('verified_','created_','released_','used_','submitted_')):require(type(v) is int,'Work counters must be integers')
    if profile in ('draw','pso'):require(value['shader_header_sha256']==context['shader_header_sha256'],'Shader fixture changed')
    if profile!='upload':require(type(value['requested_instance_api_version']) is int and value['requested_instance_api_version']==4198400,'Explicit Vulkan 1.1 request changed')
    if profile=='draw':
        count=0 if probe else 1
        require(value['draw_calls_per_frame']==value['instances_per_call']==count and value['submitted_draw_calls']==total and value['scissor']==[5,3,55,31] and value['vertices_per_instance']==3 and value['vertices_per_frame']==3*count and value['first_vertex']==value['first_instance']==0,'Draw/scissor/instance work changed')
        require(all(type(value[k]) is int for k in ('draw_calls_per_frame','instances_per_call','vertices_per_instance','vertices_per_frame','first_vertex','first_instance')) and all(type(x) is int for x in value['scissor']),'Draw work must use integer counters')
    if profile=='pso':
        require(value['pipelines_per_iteration']==(0 if probe else 1) and value['created_pipelines']==value['released_pipeline_handles']==value['used_pipeline_regions']==total
                and value['includes_shader_object_create_release'] is True and value['GPU_use_in_timing'] is value['GPU_memory_reclamation_verified'] is False
                and value['cache_state']=='uncontrolled_driver_cache_repeated_public_creation','Pipeline use/release/cache boundary changed')
        require(type(value['pipelines_per_iteration']) is int,'Pipeline count must be an integer')
    if profile=='allocation':
        require(value['resources_per_iteration']==(0 if probe else 1) and value['resource_kind']=='buffer' and value['buffer_bytes']==4096
                and value['verified_buffers']==value['verified_resources']==value['created_resources']==value['released_resource_handles']==value['used_resources']==total
                and value['allocation_policy']=='public_device_resource_default_policy' and value['usage_policy']==('sdl_vertex_buffer' if job['sdk']=='sdl' else 'sdk_general_device_buffer')
                and value['GPU_use_in_timing'] is value['GPU_memory_reclamation_verified'] is value['native_allocation_count_verified'] is False,'Resource use/release/policy changed')
        require(type(value['resources_per_iteration']) is type(value['buffer_bytes']) is int,'Resource counts/sizes must be integers')
    return value

def validate_scope(r):
    a=r['qualification'];require(r['P3_BEN_08_completed']==a['physical_probe_qualified'] and type(r['P3_BEN_08_completed']) is bool
        and r['physical_GPU_accepted']==a['physical_probe_qualified'] and type(r['physical_GPU_accepted']) is bool
        and r['performance_accepted'] is r['all_workload_scenarios_accepted'] is False and type(r['performance_samples']) is int and r['performance_samples']==0
        and r['validation_timings_excluded'] is True,'Software/smoke promoted to physical/performance/full-workload acceptance')

def check(folder,live=False):
    r=read(folder/'receipt.json');require(r['task']=='P3-BEN-08' and r['status']=='completed' and r['inputs_unchanged'] is True,'Incomplete Linux workload build')
    require(sha(folder/'qualification/receipt.json')==r['qualification_sha256'] and read(folder/'qualification/receipt.json')==r['qualification'],'Qualification changed')
    q.check(folder/'qualification');validate_scope(r)
    require((folder/'settings/vk_layer_settings.txt').read_bytes()==(folder/'qualification/settings/vk_layer_settings.txt').read_bytes(),'Required synchronization settings changed')
    prefix=next(x['path'][:-len('tools/linux_workloads.py')] for x in r['source_inputs'] if x['path'].endswith('/tools/linux_workloads.py'))
    inputs={x['path'][len(prefix):]:x['sha256'] for x in r['source_inputs']}
    for profile in port.PROFILES:
        generated=folder/'generated'/(profile+'.cpp');manifest=read(folder/'generated'/(profile+'.json'))
        require(manifest['profile']==profile and manifest['generated_sha256']==sha(generated) and manifest['source_fingerprint']==r['profile_fingerprints'][profile]
                and manifest['workload_bodies_changed'] is False and len(manifest['adaptations'])==5,'Generated adaptation changed')
        require(all(x['path'] in inputs and inputs[x['path']]==x['sha256'] for x in manifest['inputs'])
                and hashlib.sha256(''.join(x['path']+'|'+x['sha256']+'\n' for x in manifest['inputs']).encode()).hexdigest()==r['profile_fingerprints'][profile],'Generated source provenance changed')
    expected=schedule();require(len(r['runs'])==len(expected),'Missing/extra native process');last=0;images=buffers=verified=0
    for row,job in zip(r['runs'],expected):
        require(row['job']==job and row['argv']==arguments(job,r) and type(row['pid']) is int and row['pid']>0 and last<=row['started']<=row['finished'],'Native command/order/process changed');last=row['finished']
        env=environment(job,r);require(row['overrides'].keys()==env.keys() and all(row['overrides'][k].replace('\\','/')==v.replace('\\','/') for k,v in env.items()),'Native environment changed')
        stdout=(folder/row['stdout']).read_bytes();stderr=(folder/row['stderr']).read_bytes();require(hashlib.sha256(stdout).hexdigest()==row['stdout_sha256'] and hashlib.sha256(stderr).hexdigest()==row['stderr_sha256'],'Raw native output changed')
        if job['kind'] in ('probe','validation-smoke'):
            require(row['returncode']==0 and not re.search(rb'Validation Error|SYNC-HAZARD|VUID-.*(?:ERROR|Error)',stdout+b'\n'+stderr),'Native workload failed')
            result=validate_result(json.loads(stdout),job,r);require(result==row['result'],'Raw/parsed result changed');images+=result['verified_images'];buffers+=result.get('verified_buffers',0);verified+=result['verified_bytes']
        else:q.control(job,row['returncode'],stdout,stderr)
    require((r['verified_images'],r['verified_buffers'],r['verified_bytes'])==(images,buffers,verified)==(24,8,263648),'Readback totals changed')
    if live:
        q.check(pathlib.Path(r['qualification_evidence']),live=True);require(q.unchanged(r['frozen_inputs']),'Source/build/runtime changed')
        require(all(port.fingerprint(p)==r['profile_fingerprints'][p] for p in port.PROFILES),'Profile fingerprint changed')
    return dict(task='P3-BEN-08',probe_processes=8,validation_smoke_processes=8,control_processes=24,verified_images=24,verified_buffers=8,verified_bytes=263648,performance_samples=0,P3_BEN_08_completed=r['P3_BEN_08_completed'])

def run(a):
    require(platform.system()=='Linux','Actual Linux host required');qualification=a.qualification.resolve();q.check(qualification,live=True);prior=read(qualification/'receipt.json')
    require(a.mode==prior['mode'] and (a.mode=='software-diagnostic' or prior['physical_probe_qualified'] is True),'Explicit qualified mode required')
    require(q.git(ROOT,'rev-parse','HEAD')==prior['source_commit'],'Qualification belongs to a different source revision')
    out=a.output.resolve();build=out.with_name(out.name+'-build');require(out.is_relative_to(ROOT/'out') and not out.exists() and not build.exists(),'Fresh Linux workload roots required')
    qualified_build=pathlib.Path(prior['output_directory']).with_name(pathlib.Path(prior['output_directory']).name+'-build');install=qualified_build/'install';sdl=qualified_build/'sdl'
    out.mkdir(parents=True);build.mkdir();shutil.copytree(qualification,out/'qualification')
    for n in ('commands','empty','settings','xdg'):(out/n).mkdir(mode=0o700)
    shutil.copyfile(qualification/'settings/vk_layer_settings.txt',out/'settings/vk_layer_settings.txt')
    sources=sorted(set([pathlib.Path(__file__),ROOT/'benchmarks/phase3_comparison/contract.json',*[p for profile in port.PROFILES for p in port.paths(profile)]]));before=[record(p) for p in sources]
    base={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_','LD_')) and k not in ('DISPLAY','WAYLAND_DISPLAY')}
    commands=[]
    def call(name,argv,env=None):
        overrides=env or {};start=time.monotonic();child=subprocess.Popen(list(map(str,argv)),cwd=ROOT,env={**base,**overrides},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:stdout,stderr=child.communicate(timeout=600)
        except subprocess.TimeoutExpired:child.kill();stdout,stderr=child.communicate();(out/'commands'/(name+'.stdout')).write_bytes(stdout);(out/'commands'/(name+'.stderr')).write_bytes(stderr);raise
        row=dict(argv=list(map(str,argv)),overrides=overrides,pid=child.pid,started=start,finished=time.monotonic(),returncode=child.returncode)
        for key,data in (('stdout',stdout),('stderr',stderr)):
            p=out/'commands'/(name+'.'+key);p.write_bytes(data);row[key]=p.relative_to(out).as_posix();row[key+'_sha256']=sha(p)
        commands.append(dict(name=name,**row));save(out/'commands.json',commands);return row,stdout,stderr
    for name,argv in [('configure',['cmake','-S',ROOT/'benchmarks/linux_workloads','-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DGFX_INSTALLED_SDK='+str(install),'-DSDL_BINARY_DIR='+str(sdl)]),('build',['cmake','--build',build,'--parallel','2'])]:
        row,stdout,stderr=call(name,argv)
        if row['returncode']:print((stdout+stderr).decode(errors='replace')[-16000:],flush=True)
        require(row['returncode']==0,name+' failed; raw logs retained');print(name+' PASS',flush=True)
    q.packages(q.cache(build/'CMakeCache.txt'),install,sdl);binaries={p:record(build/('linux_vulkan_'+p)) for p in port.PROFILES}
    (out/'generated').mkdir()
    for p in port.PROFILES:
        for suffix in ('.cpp','.json'):
            shutil.copyfile(build/(p+suffix),out/'generated'/(p+suffix));require(sha(build/(p+suffix))==sha(out/'generated'/(p+suffix)),'Generated source copy differs')
    frozen=before+[record(out/'settings/vk_layer_settings.txt')]+[record(build/n) for n in ('CMakeCache.txt','compile_commands.json','build.ninja')]+[record(build/(p+suffix)) for p in port.PROFILES for suffix in ('.cpp','.json')]+[record(out/'generated'/(p+suffix)) for p in port.PROFILES for suffix in ('.cpp','.json')]+list(binaries.values())
    r=dict(task='P3-BEN-08',status='running',source_commit=q.git(ROOT,'rev-parse','HEAD'),qualification_evidence=str(qualification),qualification_sha256=sha(out/'qualification/receipt.json'),qualification=prior,output_directory=str(out),
        binaries=binaries,profile_fingerprints={p:port.fingerprint(p) for p in port.PROFILES},shader_header_sha256=sha(ROOT/'tests/fixtures/SpirvFixtures.hpp'),frozen_inputs=frozen,source_inputs=before,
        P3_BEN_08_completed=False,physical_GPU_accepted=False,performance_accepted=False,all_workload_scenarios_accepted=False,performance_samples=0,validation_timings_excluded=True,verified_images=0,verified_buffers=0,verified_bytes=0,runs=[])
    save(out/'receipt.json',r)
    try:
        for job in schedule():
            require(q.unchanged(frozen),'Frozen workload inputs changed');row,stdout,stderr=call(job['name'],arguments(job,r),environment(job,r));row['job']=job;r['runs'].append(row);save(out/'receipt.json',r)
            if job['kind'] in ('probe','validation-smoke'):
                require(row['returncode']==0,'Native workload failed; raw output retained');row['result']=validate_result(json.loads(stdout),job,r)
                for key in ('verified_images','verified_bytes'):r[key]+=row['result'][key]
                r['verified_buffers']+=row['result'].get('verified_buffers',0)
                actual=q.driver_libraries(pathlib.Path(prior['icd']['path']),stderr.decode(errors='replace'),prior['loader_path']);require(actual==prior['runtime_libraries'],'Actual loader/ICD differs from qualification')
            else:q.control(job,row['returncode'],stdout,stderr)
            save(out/'receipt.json',r);print(job['name']+' PASS',flush=True)
        q.check(qualification,live=True);require(q.unchanged(frozen),'Final source/build input changed')
        r.update(status='completed',inputs_unchanged=True,P3_BEN_08_completed=prior['physical_probe_qualified'],physical_GPU_accepted=prior['physical_probe_qualified']);save(out/'receipt.json',r);print(json.dumps(check(out,live=True)),flush=True)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:r.update(status='failed',error=str(e));save(out/'receipt.json',r);raise

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',choices=('run','check'));p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--qualification',type=pathlib.Path);p.add_argument('--mode',choices=('software-diagnostic','physical'),default='physical');p.add_argument('--live',action='store_true');a=p.parse_args()
    if a.stage=='check':print(json.dumps(check(a.output,a.live),indent=2));return
    require(a.qualification is not None,'Explicit BEN07 qualification required');run(a)
if __name__=='__main__':main()
