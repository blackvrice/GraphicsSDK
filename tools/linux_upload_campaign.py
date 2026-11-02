"""Fresh Linux three-SDK upload campaign; software smoke never exports statistics."""
import argparse
import hashlib
import itertools
import json
import math
import os
import pathlib
import platform
import random
import re
import shlex
import shutil
import subprocess
import time
import zipfile
import linux_build_policy as build_policy

import linux_llgl_probe as p
import linux_llgl_upload as lu
import linux_llgl_draw as draw
import linux_workloads as w
import linux_workload_matrix as matrix
import linux_readiness_report as binding
import port_linux_upload_campaign as port
from linux_comparison import ROOT,require,read,save,sha,record
from run_vulkan_comparison import scalar_stats

PLAN='benchmarks/phase3_comparison/linux-upload-campaign.json'
WINDOWS_PLAN='benchmarks/phase3_comparison/competitors/llgl-upload-comparison.json'
SDKS=('graphicssdk','sdl','llgl')
SHAPES=lu.SHAPES
SUCCESS={'probe','observe','validation','core-control','sync-control','smoke','measurement'}


def validate_plan(value):
    expected=dict(schema_version=1,task='P3-LBEN-01',scope='linux-x86_64-qualified-three-sdk-upload',sdks=list(SDKS),scenarios=list(SHAPES),
        validation=dict(warmup=1,samples=3,required_core_and_sync=True),
        performance=dict(warmup=5,samples=30,repetitions=5,seed=20261027,order='seeded_scenario_blocks_five_distinct_three_SDK_permutations'),
        smoke=dict(warmup=1,samples=3,repetitions=1,statistics_exported=False),serialized_processes=True,
        observer_free_measurements=True,loader_trace_in_measurements=False,validation_timings_excluded=True,
        historical_timings_reused=False,overall_SDK_ranking=False,GPU_timestamp_measurement=False,
        SDL_native_handle_verified=False,physical_qualification_required_for_measurements=True)
    require(p.same(value,expected),'Linux campaign contract changed')
    prior=read(ROOT/WINDOWS_PLAN)
    require(p.same(value['performance'],prior['performance']) and p.same(value['validation'],prior['validation']),'Frozen CPU/repetition contract changed')
    return value


def schedule(plan,mode):
    validate_plan(plan);p.identity_prefix(mode)
    rows=[dict(kind='probe',sdk=s,scenario='probe',repetition=0) for s in SDKS]
    rows += [dict(kind='observe',sdk='llgl',scenario='probe',repetition=0)]
    rows += [dict(kind='validation',sdk=s,scenario=n,repetition=0) for n in SHAPES for s in SDKS]
    rows += [dict(kind=k,sdk=s,scenario='odd',repetition=0) for s in SDKS for k in ('missing-layer','wrong-uuid','wrong-kind')]
    rows += [dict(kind=k,sdk='llgl',scenario=n,repetition=0) for k,n in (
        ('core-control','core-control'),('sync-control','sync-control'),('sync-disabled','sync-control'),
        ('corrupt-readback','corrupt-readback'),('observer-disabled','probe'))]
    rng=random.Random(plan['performance']['seed']);orders={}
    for n in SHAPES:orders[n]=list(itertools.permutations(SDKS));rng.shuffle(orders[n])
    for rep in range(5 if mode=='physical' else 1):
        names=list(SHAPES);rng.shuffle(names)
        for n in names:rows += [dict(kind='measurement' if mode=='physical' else 'smoke',sdk=s,scenario=n,repetition=rep) for s in orders[n][rep]]
    return [dict(j,name=f"{i:03}-{j['kind']}-{j['sdk']}-{j['scenario']}-{j['repetition']}") for i,j in enumerate(rows)]


def options(job):
    kind=job['kind'];validation=kind in ('validation','missing-layer','core-control','sync-control','sync-disabled')
    observed=kind in ('observe','observer-disabled');special=kind in ('probe','observe','observer-disabled','core-control','sync-control','sync-disabled')
    warmup,samples=(0,0) if special else (5,30) if kind=='measurement' else (1,3)
    return validation,observed,warmup,samples


def qualification(r):return r['readiness']['qualification']


def validate_inputs(ready,llgl,mode):
    p.identity_prefix(mode);a=ready['qualification']
    require(p.same(a,llgl['probe']['qualification']) and ready['source_commit']==llgl['source_commit'],'Mixed qualification/revision')
    p.validate_qualification(a,mode)
    lu.policy.validate_receipt(llgl,'upload')
    require(lu.policy.mode(llgl)==mode and not lu.policy.guard(llgl),'Wrong or guard-only LLGL correctness receipt')
    require(ready['status']==llgl['status']=='completed' and ready['inputs_unchanged'] is llgl['inputs_unchanged'] is True,'Incomplete prerequisites')
    if mode=='physical':
        build_policy.scope(a.get('controlled_build'),('sdk','sdl','probe'))
        build_policy.scope(ready.get('controlled_build'),('consumer',))
        p.exact(a['environment'],dict(platform='Linux',machine='x86_64',pointer_bits=64))


def arguments(job,r):
    validation,observed,warmup,samples=options(job);a=qualification(r)
    if job['sdk']=='llgl':
        return [r['binary']['path'],'0'*32 if job['kind']=='wrong-uuid' else a['device_uuid'],
            'observe' if observed else 'validation' if validation else 'plain',job['scenario'],str(warmup),str(samples)]
    binary=r['readiness']['binaries']['upload']['path']
    if job['kind']=='probe':return [binary,'--probe',job['sdk'],'high',a['selected']['name']]
    return [binary,job['sdk'],job['scenario'],'high',str(warmup),str(samples),str(int(validation)),a['selected']['name']]


def environment(job,r):
    validation,observed,_,_=options(job);a=qualification(r);out=pathlib.Path(r['output_directory'])
    env=p.q.overrides(out,a['icd']['path'],a['validation_library'],validation)
    env['LD_LIBRARY_PATH']=str(pathlib.PurePosixPath(r['llgl_correctness']['probe']['binaries']['core']['path']).parent)
    kind=('software-diagnostic' if r['mode']=='physical' else 'physical') if job['kind']=='wrong-kind' else r['mode']
    if job['sdk']=='llgl':env['GFX_LLGL_UPLOAD_COMPARE_KIND']=kind
    else:env.update(GFX_COMPARISON_UUID='0'*32 if job['kind']=='wrong-uuid' else a['device_uuid'],GFX_COMPARISON_MODE=kind)
    if observed:
        env.update(VK_LAYER_PATH=str(pathlib.PurePosixPath(r['observer']['path']).parent),VK_INSTANCE_LAYERS='VK_LAYER_GFX_llgl_draw')
    if job['kind']=='observer-disabled':env['VK_INSTANCE_LAYERS']=''
    if job['kind']=='missing-layer':env['VK_LAYER_PATH']=str(out/'empty')
    if job['kind']=='sync-disabled':env.update(VK_LAYER_SETTINGS_PATH=str(out/'sync-off'),VK_KHRONOS_VALIDATION_VALIDATE_SYNC='false')
    if job['kind'] in ('measurement','smoke'):env.pop('LD_DEBUG',None)
    return env


def positive_number(value):return type(value) in (int,float) and math.isfinite(value) and value>0


def validate_output(value,job,r):
    validation,observed,warmup,samples=options(job);a=qualification(r);d=a['selected'];kind=job['kind']
    probe=kind in ('probe','observe');control=kind in lu.TOKENS;special=probe or control
    shape=[0,0,0] if special else SHAPES[job['scenario']];images=0 if special else warmup+samples
    p.exact(value,dict(schema_version=1,sdk=job['sdk'],api='vulkan',configuration='Release',category='end_to_end',
        contract='vulkan_upload_cpu_payload_to_texture_fence_complete_each_sample_readback_v1',scenario=job['scenario'],
        validation=validation,probe=probe,warmup=warmup,sample_count=samples,gpu_ms=None,
        device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
        native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
        width=shape[0],height=shape[1],texel_bytes=shape[2],verified_images=images,verified_bytes=math.prod(shape)*images,readback_verified=not special,
        loaded_validation_layer=a['validation_library'] if validation else ''))
    p.exact(value['environment'],a['environment'])
    values=value['samples_ms'];require(type(values) is list and len(values)==samples and all(positive_number(x) for x in values),'Invalid CPU upload interval rows')
    if job['sdk']!='llgl':
        p.exact(value,dict(preference='high',source_fingerprint=r['readiness']['profile_fingerprints']['upload'],
            SDK_source_fingerprint=a['SDK_source_fingerprint'],SDL_commit=a['SDL_commit'],identity_attribution=w.ATTRIBUTION))
        p.exact(value['environment'],dict(gpu=d['name'],software_renderer=r['mode']=='software-diagnostic',device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None))
    else:
        prior=r['llgl_correctness']['probe'];native=next(x['result'] for x in prior['runs'] if x['job']['kind']=='normal')
        p.exact(value,dict(task='P3-LBEN-01-upload-campaign',mode=r['mode'],profile_id=p.PROFILE,LLGL_commit=prior['LLGL_commit'],
            source_fingerprint=r['adaptation']['source_fingerprint'],identity_attribution='actual_LLGL_native_handles_and_native_UUID',
            native_handle_verified=True,queue_family=native['queue_family'],queue_index=0,queue_flags=native['queue_flags'],loader_api_version=native['loader_api_version'],
            loader_path=a['loader_path'],LLGL_path=prior['binaries']['core']['path'],backend_path=prior['binaries']['backend']['path'],
            control=control,control_diagnostic=lu.TOKENS[kind] if control else '',submitted_control_work=0,
            row_stride=0,layer_stride=0,native_workload_shortcuts=False,observer_enabled=observed,instance_API_request_observed=observed,
            observed_instance_api_version=native['loader_api_version'] if observed else 0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,
            payload_sha256=r['reference']['hashes'][job['scenario']] if validation and not special else []))
        require(type(value['delivered_errors']) is int and (value['delivered_errors']>=1 if control else value['delivered_errors']==0),'Native validation/control counter differs')
        modules=value['runtime_modules'];required={x['path'] for x in a['runtime_libraries']}|{prior['binaries'][n]['path'] for n in ('core','backend')}|{r['crypto']['path']}
        if validation:required.add(a['validation_library'])
        if observed:required.add(r['observer']['path'])
        require(type(modules) is list and modules==sorted(set(modules)) and required.issubset(modules),'Actual LLGL runtime snapshot incomplete')
        require((a['validation_library'] in modules)==validation and (r['observer']['path'] in modules)==observed,'Timing layer contamination')
        for path in modules:
            require(type(path) is str and pathlib.PurePosixPath(path).is_absolute() and (path in required or any(pathlib.PurePosixPath(path).is_relative_to(base) for base in ('/usr/lib','/lib'))),'Unreviewed runtime module')
            require('VkLayer_khronos_validation' not in path or path==a['validation_library'],'Unpinned validation runtime')
            require('VkLayer_gfx_llgl_draw' not in path or path==r['observer']['path'],'Unpinned observer')
    return value


def rejected(job,code,stdout,stderr,r):
    require(type(code) is int and code==1 and not stdout,'Rejection succeeded or emitted samples')
    if job['sdk']!='llgl':p.q.control(job,code,stdout,stderr);return
    tokens={'wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':lu.policy.wrong_kind_message(r['mode']),
        'missing-layer':b'Required comparison validation layer absent','sync-disabled':b'Expected validation control diagnostic absent',
        'corrupt-readback':b'Full changing upload readback mismatch','observer-disabled':b'Required runtime module absent or ambiguous'}
    require(tokens[job['kind']] in stderr,'Unrelated LLGL rejection')


def summarize(rows,mode):
    if mode=='software-diagnostic':
        require(not any(x['job']['kind']=='measurement' for x in rows),'Software timings promoted to measurement');return []
    groups=[]
    for scenario in SHAPES:
        for sdk in SDKS:
            selected=[x for x in rows if x['job']['kind']=='measurement' and x['job']['scenario']==scenario and x['job']['sdk']==sdk]
            require([x['job']['repetition'] for x in selected]==list(range(5)),'Missing/duplicate fresh process repetitions')
            values=[x['result']['samples_ms'] for x in selected]
            require(all(len(row)==30 and all(positive_number(v) for v in row) for row in values),'Missing/invalid retained samples')
            groups.append(dict(scenario=scenario,sdk=sdk,column='total_ms',category='end_to_end',statistics=scalar_stats(values)))
    return groups


def validate_scope(r):
    validate_inputs(r['readiness'],r['llgl_correctness'],r['mode']);physical=r['mode']=='physical'
    p.exact(r,dict(schema_version=1,task='P3-LBEN-01-upload-campaign',status='completed',inputs_unchanged=True,
        campaign_kind='physical-campaign' if physical else 'software-smoke',performance_accepted=physical,
        measurement_samples=1800 if physical else 0,diagnostic_rows=36 if physical else 72,
        physical_GPU_accepted=False,P3_LBEN_01_completed=False,historical_timings_reused=False,overall_SDK_ranking=False,
        GPU_timestamp_measurement=False,SDL_native_handle_verified=False,SDK_SDL_measurement_module_snapshot_observed=False))


def validate_success(row,stdout,stderr,job,r):
    require(type(row['returncode']) is int and row['returncode']==0,'Native run failed')
    value=validate_output(matrix.strict_json(stdout),job,r)
    if job['kind'] in lu.TOKENS:require(lu.TOKENS[job['kind']].encode() in stderr,'Actual core/sync diagnostic absent')
    else:require(not re.search(rb'Validation Error|SYNC-HAZARD',stderr),'Unexpected native error')
    if job['kind'] in ('measurement','smoke'):
        require(b'VkLayer_' not in stderr,'Layer activation in measured process')
        require(sum(value['samples_ms'])/1000<=row['finished']-row['started'],'Samples exceed enclosing process')
    if job['sdk']!='llgl' and job['kind'] not in ('measurement','smoke'):
        require(matrix.loaded_libraries(stderr,qualification(r))==(job['kind']=='validation'),'Actual SDK/SDL runtime layer drift')
    return value


def check(folder,live=False):
    r=matrix.strict_json((folder/'receipt.json').read_bytes());validate_scope(r);validate_plan(r['plan'])
    w.check(folder/'readiness');lu.check(folder/'llgl-correctness')
    for name,key in (('readiness','readiness'),('llgl-correctness','llgl_correctness')):
        require(sha(folder/name/'receipt.json')==r[key+'_sha256'] and p.same(read(folder/name/'receipt.json'),r[key]),'Prerequisite snapshot drift')
    require(r['source_commit']==r['readiness']['source_commit'],'Mixed source revision')
    require(sha(folder/'inputs.json')==r['inputs_sha256'],'Compile audit drift');draw.validate_audit(read(folder/'inputs.json'))
    require(p.same(read(folder/'adaptation.json'),r['adaptation']),'Adaptation drift')
    require(sha(folder/'observer-layer.json')==r['observer_manifest']['sha256'],'Observer manifest drift')
    p.exact(read(folder/'observer-layer.json'),dict(file_format_version='1.2.0',layer=dict(name='VK_LAYER_GFX_llgl_draw',type='GLOBAL',
        library_path='./libVkLayer_gfx_llgl_draw.so',api_version='1.3.290',implementation_version='1',description='Own Linux pass-through draw observer',
        functions=dict(vkNegotiateLoaderLayerInterfaceVersion='gfxDrawNegotiate'))))
    p.exact(r['adaptation'],dict(mode=r['mode'],workload_bodies_changed=False,timed_loop_changed=False))
    require(sha(folder/'source.zip')==r['source_zip_sha256'],'Source ZIP drift')
    with zipfile.ZipFile(folder/'source.zip') as z:
        require(len(z.namelist())==len(set(z.namelist()))==len(r['source_inputs']) and set(z.namelist())=={x['path'] for x in r['source_inputs']},'Source inventory drift')
        for item in r['source_inputs']:require(hashlib.sha256(z.read(item['path'])).hexdigest()==item['sha256'],'Source bytes drift')
        inputs={x['path']:x['sha256'] for x in r['source_inputs']}
        require(all(inputs[x['path']]==x['sha256'] for x in r['adaptation']['inputs']),'Adapter source binding drift')
        require(hashlib.sha256(port.identity(r['adaptation']['inputs'],r['mode'],r['llgl_correctness']['probe']['LLGL_commit']).encode()).hexdigest()==r['adaptation']['source_fingerprint'],'Mode/source fingerprint drift')
        outputs,changes=port.translate({n:z.read(n).decode().replace('\r\n','\n') for n in port.OWN},r['mode'])
        require(changes==r['adaptation']['adaptations'],'Adapter changes drift')
        for name,text in outputs.items():require((folder/'generated'/name).read_bytes()==text.encode() and sha(folder/'generated'/name)==r['adaptation']['generated'][name],'Generated code drift')
        require(p.same(matrix.strict_json(z.read(PLAN)),r['plan']),'Archived plan drift')
    require((folder/'settings/vk_layer_settings.txt').read_bytes()==(folder/'readiness/settings/vk_layer_settings.txt').read_bytes()
        and (folder/'sync-off/vk_layer_settings.txt').read_text()==lu.OFF,'Required validation settings drift')
    require(p.same(r['reference'],read(folder/'payload-reference.json')) and p.same(r['reference'],read(ROOT/lu.REFERENCE)),'Independent full-image reference drift')
    expected=schedule(r['plan'],r['mode']);require(len(r['runs'])==len(expected),'Missing/extra native processes');last=0;images=verified=0
    for row,job in zip(r['runs'],expected):
        env=environment(job,r)
        require(p.same(row['job'],job) and row['argv']==arguments(job,r) and row['overrides'].keys()==env.keys() and all(row['overrides'][k].replace('\\','/')==v.replace('\\','/') for k,v in env.items()),'Native schedule/argv/environment drift')
        require(type(row['pid']) is int and row['pid']>0 and all(type(row[k]) in (int,float) and math.isfinite(row[k]) for k in ('started','finished')) and last<=row['started']<=row['finished'],'Process serialization drift');last=row['finished']
        stdout,stderr=(p.safe(folder,row[k]).read_bytes() for k in ('stdout','stderr'))
        require(hashlib.sha256(stdout).hexdigest()==row['stdout_sha256'] and hashlib.sha256(stderr).hexdigest()==row['stderr_sha256'],'Raw output drift')
        if job['kind'] in SUCCESS:
            value=validate_success(row,stdout,stderr,job,r)
            require(p.same(value,row['result']),'Parsed/raw output drift')
            if job['sdk']=='llgl':require({x['path'] for x in row['runtime_files']}==set(value['runtime_modules']) and all(x in r['runtime_inputs'] for x in row['runtime_files']),'Runtime module binding drift')
            images+=value['verified_images'];verified+=value['verified_bytes']
        else:rejected(job,row['returncode'],stdout,stderr,r)
    physical=r['mode']=='physical'
    p.exact(r,dict(verified_images=2148 if physical else 96,verified_bytes=57908372340 if physical else 2588083680))
    require((images,verified)==(r['verified_images'],r['verified_bytes']) and p.same(r['groups'],summarize(r['runs'],r['mode'])),'Readback totals/statistics drift')
    if live:
        w.check(pathlib.Path(r['readiness_evidence']),live=True);lu.check(pathlib.Path(r['llgl_evidence']),live=True)
        require(p.q.unchanged(r['frozen_inputs']+r['runtime_inputs']),'Source/build/runtime drift')
    return dict(processes=len(expected),rejection_controls=12,core_sync_controls=2,measurement_processes=60 if physical else 0,
        measurement_samples=r['measurement_samples'],verified_images=images,verified_bytes=verified,statistics_rows=len(r['groups']),
        performance_accepted=physical,physical_GPU_accepted=False,P3_LBEN_01_completed=False)


def audit(build,call,build_stdout):
    commands=read(build/'compile_commands.json');require(len(commands)==2,'Consumer and untimed observer compiler units required')
    row,stdout,_=call('ninja-commands',['ninja','-C',build,'-t','commands','linux_upload_compare']);require(row['returncode']==0,'Ninja target graph absent')
    lines=[s for s in stdout.decode().splitlines() if '-c' in shlex.split(s) and '-o' in shlex.split(s)]
    require(len(lines)==2 and all(('] '+line).encode() in build_stdout for line in lines),'Actual verbose compiler command absent')
    paths=set();deps=[]
    for command in commands:
        matches=[line for line in lines if shlex.split(line)[shlex.split(line).index('-c')+1]==command['file']]
        require(len(matches)==1,'Ambiguous actual compiler command');command['executed_command']=matches[0]
        argv=shlex.split(matches[0]);p.compiler_flags(argv)
        dep=pathlib.Path(command['directory'])/argv[argv.index('-MF')+1];raw=dep.read_bytes().decode();items=p.gnu.dependencies(raw,pathlib.Path(command['directory']))
        deps.append(dict(**record(dep),raw=raw,dependencies=[str(n) for n in items]));paths.update(items)
    own={ROOT/n for n in ('benchmarks/common/LinuxEnvironment.hpp','benchmarks/common/Adapter.hpp','benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/llgl_vulkan_upload/Payload.hpp')}
    generated={build/n for n in port.OUTPUTS}
    require(own.issubset(paths) and generated.issubset(paths) and not {ROOT/'benchmarks/llgl_vulkan_upload/Context.hpp',ROOT/'benchmarks/llgl_vulkan_pso/Support.hpp'}&paths,'Generated source leaked original platform or omitted workload inputs')
    llgl=ROOT/'.deps/LLGL';headers=ROOT/'.deps/Vulkan-Headers';tracked={n:set(p.q.git(n,'ls-files').splitlines()) for n in (llgl,headers)}
    files=[];notices=set()
    for path in sorted(paths):
        row=record(path)
        if path in own:row.update(component='own',selected_license='MIT')
        elif path in generated:row.update(component='generated',selected_license='MIT')
        elif path.is_relative_to(llgl):
            require(path.relative_to(llgl).as_posix() in tracked[llgl],'Untracked LLGL header')
            blocks=p.source_notices(path.read_text(encoding='utf-8-sig'));require('BSD' in '\n'.join(blocks),'Unreviewed LLGL license')
            notices.update(blocks);row.update(component='llgl',selected_license='BSD-3-Clause',dependency_commit=read(ROOT/p.APPROVAL)['upstream_commit'])
        elif path.is_relative_to(headers):
            require(path.relative_to(headers).as_posix() in tracked[headers],'Untracked Vulkan header')
            text=path.read_text(encoding='utf-8-sig');licenses=re.findall(r'SPDX-License-Identifier:\s*([^\r\n*]+)',text)
            require(licenses and all(s.strip() in ('MIT','Apache-2.0','MIT OR Apache-2.0','Apache-2.0 OR MIT') for s in licenses),'Unreviewed Vulkan license')
            row.update(component='vulkan_headers',selected_license='MIT' if any('MIT' in s for s in licenses) else 'Apache-2.0',dependency_commit=p.q.HEADERS);notices.update(p.source_notices(text))
        else:
            require(any(path.is_relative_to(pathlib.Path(n)) for n in ('/usr/include','/usr/lib','/lib')),'Unreviewed compiler input')
            row.update(component='system',redistributed=False)
        files.append(row)
    value=dict(files=files,compile_commands=commands,depfiles=deps,actual_verbose_commands_verified=True,notice_blocks=sorted(notices))
    draw.validate_audit(value);return value


def run(a):
    require(platform.system()=='Linux','Actual Linux host required')
    mode='physical' if a.stage=='run' else 'software-diagnostic'
    ready=a.readiness.resolve();llgl=a.llgl_correctness.resolve();w.check(ready,live=True);lu.check(llgl,live=True)
    prior=read(ready/'receipt.json');approved=read(llgl/'receipt.json');validate_inputs(prior,approved,mode)
    require(p.q.git(ROOT,'rev-parse','HEAD')==prior['source_commit'] and not p.q.git(ROOT,'status','--porcelain','--untracked-files=no'),'Clean same-revision prerequisites required')
    out=a.output.resolve();build=out.with_name(out.name+'-build')
    require(out.is_relative_to(ROOT/'out') and not out.exists() and not build.exists(),'Fresh campaign/build roots required')
    out.mkdir(parents=True);build.mkdir();shutil.copytree(ready,out/'readiness');shutil.copytree(llgl,out/'llgl-correctness')
    for name in ('commands','generated','empty','settings','sync-off','xdg'):(out/name).mkdir(mode=0o700)
    shutil.copyfile(ready/'settings/vk_layer_settings.txt',out/'settings/vk_layer_settings.txt')
    (out/'sync-off/vk_layer_settings.txt').write_text(lu.OFF,encoding='utf-8',newline='\n');shutil.copyfile(ROOT/lu.REFERENCE,out/'payload-reference.json')
    plan=validate_plan(read(ROOT/PLAN))
    sources={ROOT/n for n in (*port.OWN,PLAN,WINDOWS_PLAN,lu.REFERENCE,'tools/linux_upload_campaign.py',
        'tools/linux_llgl_upload.py','tools/linux_llgl_draw.py','tools/linux_llgl_probe.py','tools/linux_llgl_workload_mode.py',
        'tools/linux_workload_matrix.py','tools/linux_workloads.py','tools/linux_comparison.py','tools/run_vulkan_comparison.py','LICENSE')}
    sources.update(pathlib.Path(n['path']) for n in prior['source_inputs']+prior['qualification']['source_inputs'])
    require(all(n.is_relative_to(ROOT) for n in sources),'Qualified source escaped repository')
    before=[record(n) for n in sorted(sources)]
    base={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_','LD_','OPENSSL_')) and k not in ('DISPLAY','WAYLAND_DISPLAY')}
    commands=[]
    def call(name,argv,env=None):
        start=time.monotonic();child=subprocess.Popen(list(map(str,argv)),cwd=ROOT,env={**base,**(env or {})},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:stdout,stderr=child.communicate(timeout=600)
        except subprocess.TimeoutExpired:
            child.kill();stdout,stderr=child.communicate();(out/'commands'/(name+'.stdout')).write_bytes(stdout);(out/'commands'/(name+'.stderr')).write_bytes(stderr);raise
        row=dict(argv=list(map(str,argv)),overrides=env or {},pid=child.pid,started=start,finished=time.monotonic(),returncode=child.returncode)
        for key,data in (('stdout',stdout),('stderr',stderr)):
            path=out/'commands'/(name+'.'+key);path.write_bytes(data);row[key]=path.relative_to(out).as_posix();row[key+'_sha256']=sha(path)
        commands.append(dict(name=name,**row));save(out/'commands.json',commands);return row,stdout,stderr
    qualified=pathlib.Path(approved['probe']['output_directory']);qualified=qualified.with_name(qualified.name+'-build')
    for name,argv in [('configure',['cmake','-S',ROOT/'benchmarks/linux_upload_campaign','-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DGFX_CAMPAIGN_MODE='+mode,'-DLLGL_PROBE_BUILD='+str(qualified),'-DVulkan_LIBRARY='+prior['qualification']['loader_path']]),
        ('build',['cmake','--build',build,'--target','linux_upload_compare','--parallel','2','--verbose','--','-d','keepdepfile'])]:
        row,stdout,stderr=call(name,argv)
        if row['returncode']:print((stdout+stderr).decode(errors='replace')[-12000:],flush=True)
        require(row['returncode']==0,name+' failed; raw logs retained');print(name+' PASS',flush=True)
    inputs=audit(build,call,stdout);save(out/'inputs.json',inputs)
    adaptation=read(build/'adaptation.json');shutil.copyfile(build/'adaptation.json',out/'adaptation.json')
    for name in port.OUTPUTS:shutil.copyfile(build/name,out/'generated'/name)
    shutil.copyfile(build/'VkLayer_gfx_llgl_draw.json',out/'observer-layer.json')
    require(p.q.unchanged(before),'Source changed during build')
    source_inputs=[dict(path=n.relative_to(ROOT).as_posix(),sha256=sha(n)) for n in sorted(sources)]
    with zipfile.ZipFile(out/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for n in sorted(sources):z.write(n,n.relative_to(ROOT).as_posix())
    physical=mode=='physical'
    r=dict(schema_version=1,task='P3-LBEN-01-upload-campaign',status='running',source_commit=prior['source_commit'],mode=mode,
        campaign_kind='physical-campaign' if physical else 'software-smoke',readiness=prior,llgl_correctness=approved,
        readiness_evidence=str(ready),llgl_evidence=str(llgl),readiness_sha256=sha(out/'readiness/receipt.json'),llgl_correctness_sha256=sha(out/'llgl-correctness/receipt.json'),
        plan=plan,output_directory=str(out),adaptation=adaptation,source_inputs=source_inputs,source_zip_sha256=sha(out/'source.zip'),
        binary=record(build/'linux_upload_compare'),observer=record(build/'libVkLayer_gfx_llgl_draw.so'),observer_manifest=record(build/'VkLayer_gfx_llgl_draw.json'),
        crypto=record(pathlib.Path(p.q.cache(build/'CMakeCache.txt')['OPENSSL_CRYPTO_LIBRARY'])),inputs_sha256=sha(out/'inputs.json'),reference=read(ROOT/lu.REFERENCE),
        physical_GPU_accepted=False,P3_LBEN_01_completed=False,historical_timings_reused=False,overall_SDK_ranking=False,GPU_timestamp_measurement=False,
        SDL_native_handle_verified=False,SDK_SDL_measurement_module_snapshot_observed=False,performance_accepted=False,measurement_samples=0,
        diagnostic_rows=0,verified_images=0,verified_bytes=0,runtime_inputs=[],runs=[],groups=[])
    require('.so' in r['crypto']['path'] and adaptation['source_fingerprint'].encode() in (build/'linux_upload_compare').read_bytes(),'Shared crypto/compiled fingerprint differs')
    r['frozen_inputs']=binding.merge_records([prior['frozen_inputs'],prior['qualification']['frozen_inputs'],approved['frozen_inputs']+approved['runtime_inputs'],before,
        [{k:x[k] for k in ('path','bytes','sha256')} for x in inputs['files']+inputs['depfiles']],
        [r[k] for k in ('binary','observer','observer_manifest','crypto')],
        [record(build/n) for n in (*port.OUTPUTS,'adaptation.json','CMakeCache.txt','build.ninja','compile_commands.json')],
        [record(out/n) for n in ('source.zip','inputs.json','payload-reference.json','settings/vk_layer_settings.txt','sync-off/vk_layer_settings.txt','observer-layer.json')]])
    save(out/'receipt.json',r)
    try:
        for job in schedule(plan,mode):
            require(p.q.unchanged(r['frozen_inputs']+r['runtime_inputs']),'Frozen inputs changed')
            row,stdout,stderr=call(job['name'],arguments(job,r),environment(job,r));row['job']=job;r['runs'].append(row);save(out/'receipt.json',r)
            if job['kind'] in SUCCESS:
                value=validate_success(row,stdout,stderr,job,r);row['result']=value
                r['verified_images']+=value['verified_images'];r['verified_bytes']+=value['verified_bytes']
                if job['sdk']=='llgl':
                    row['runtime_files']=[record(pathlib.Path(n)) for n in value['runtime_modules']]
                    r['runtime_inputs']=binding.merge_records([r['runtime_inputs'],row['runtime_files']])
            else:rejected(job,row['returncode'],stdout,stderr,r)
            save(out/'receipt.json',r);print(job['name']+' PASS',flush=True)
        require(p.q.unchanged(r['frozen_inputs']+r['runtime_inputs']),'Final source/runtime changed')
        r.update(status='completed',inputs_unchanged=True,performance_accepted=physical,measurement_samples=1800 if physical else 0,
            diagnostic_rows=36 if physical else 72,groups=summarize(r['runs'],mode));save(out/'receipt.json',r)
        print(json.dumps(check(out,live=True)),flush=True)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as exc:
        r.update(status='failed',performance_accepted=False,error=str(exc));save(out/'receipt.json',r);raise


if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('stage',choices=('run','smoke','check'))
    a.add_argument('--readiness',type=pathlib.Path);a.add_argument('--llgl-correctness',type=pathlib.Path)
    a.add_argument('--output',type=pathlib.Path,required=True);a.add_argument('--live',action='store_true');v=a.parse_args()
    if v.stage=='check':print(json.dumps(check(v.output,v.live),indent=2))
    else:
        require(v.readiness is not None and v.llgl_correctness is not None,'Explicit same-revision prerequisite evidence required');run(v)
