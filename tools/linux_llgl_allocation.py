"""Verify approved LLGL buffer/texture creation/use/public-release on qualified Linux devices; no timing campaign."""
import argparse
import hashlib
import json
import math
import os
import pathlib
import platform
import re
import shlex
import shutil
import subprocess
import time
import zipfile

import linux_llgl_probe as p
import linux_llgl_workload_mode as policy
import port_linux_llgl_allocation as port
from linux_comparison import ROOT, read, save, sha, record, require

REFERENCE='benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/native/payload-reference.json'
SHAPES=dict(buffer1=1,buffer1000=1000,texture1=1,texture1000=1000)


def references():
    # Reuse independent CPU hash constants, never historical GPU/timing samples.
    value=read(ROOT/REFERENCE)
    p.exact(value,dict(buffer_bytes=4096,texture_shape=[65,37,4]))
    require(set(value['sha256'])==set(SHAPES) and all(len(value['sha256'][name])==4 and
        all(len(row)==count and all(type(h) is str and re.fullmatch('[0-9a-f]{64}',h) for h in row)
            for row in value['sha256'][name]) for name,count in SHAPES.items()),'Incomplete CPU reference matrix')
    return value
TOKENS={'core-control':'VUID-VkPhysicalDeviceProperties2-sType-sType','sync-control':'SYNC-HAZARD-WRITE-AFTER-WRITE'}
OFF='khronos_validation.validate_sync = false\nkhronos_validation.syncval_submit_time_validation = false\n'


def jobs():
    result=[dict(kind='normal',scenario=s,validation=v) for v in (False,True) for s in SHAPES]
    result += [dict(kind=s,scenario=s,validation=True) for s in TOKENS]
    result += [dict(kind=k,scenario=s,validation=v) for k,s,v in (
        ('missing-layer','buffer1',True),('wrong-uuid','buffer1',False),('wrong-kind','buffer1',False),
        ('missing-resource','missing-resource',False),('alias-resource','alias-resource',False),
        ('skip-release','skip-release',False),('wrong-resource-payload','wrong-resource-payload',False),
        ('stale-iteration','stale-iteration',False),('skip-copy','skip-copy',False),
        ('corrupt-readback','corrupt-readback',False),('short-readback','short-readback',False),
        ('missing-observer','buffer1',False),('sync-disabled','sync-control',True))]
    return [dict(j,name=f"{i:02}-{j['kind']}-{j['scenario']}-{int(j['validation'])}") for i,j in enumerate(result)]


def arguments(job,r):
    return [r['binary']['path'],'0'*32 if job['kind']=='wrong-uuid' else r['probe']['qualification']['device_uuid'],
            'validation' if job['validation'] else 'plain',job['scenario']]


def environment(job,r):
    a=r['probe']['qualification'];out=pathlib.Path(r['output_directory'])
    env=p.q.overrides(out,a['icd']['path'],a['validation_library'],job['validation'])
    env.update(LD_LIBRARY_PATH=str(pathlib.PurePosixPath(r['probe']['binaries']['core']['path']).parent),
               GFX_LLGL_ALLOCATION_KIND=policy.native_kind(job,r))
    observer=str(pathlib.PurePosixPath(r['observer']['path']).parent)
    if job['kind']!='missing-observer':
        env['VK_LAYER_PATH']=observer+(':'+str(pathlib.PurePosixPath(a['validation_library']).parent) if job['validation'] else '')
        env['VK_INSTANCE_LAYERS']='VK_LAYER_GFX_llgl_draw'+(':VK_LAYER_KHRONOS_validation' if job['validation'] else '')
    if job['kind']=='missing-layer':env['VK_LAYER_PATH']=str(out/'empty')
    if job['kind']=='sync-disabled':env.update(VK_LAYER_SETTINGS_PATH=str(out/'sync-off'),VK_KHRONOS_VALIDATION_VALIDATE_SYNC='false')
    return env


def validate_result(value,job,r):
    a=r['probe']['qualification'];d=a['selected'];control=job['kind'] in TOKENS
    count=0 if control else SHAPES[job['scenario']];texture=job['scenario'].startswith('texture');total=count*4
    native=next(x['result'] for x in r['probe']['runs'] if x['job']['kind']=='normal')
    p.exact(value,dict(schema_version=1,task='P3-LBEN-04-LLGL-readiness',sdk='llgl',api='vulkan',configuration='Release',
        contract='vulkan_resource_create_public_release_full_readback_v1',category='cpu_api',source_fingerprint=r['adaptation']['source_fingerprint'],
        LLGL_commit=r['probe']['LLGL_commit'],profile_id=p.PROFILE,dependency_profile=p.PROFILE,unmodified_upstream=False,
        selected=d,environment=a['environment'],mode=policy.mode(r),device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
        native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
        native_handle_verified=True,queue_family=native['queue_family'],queue_index=0,queue_flags=native['queue_flags'],loader_api_version=native['loader_api_version'],
        instance_API_request_observed=True,observed_instance_api_version=native['loader_api_version'],loader_path=a['loader_path'],LLGL_path=r['probe']['binaries']['core']['path'],backend_path=r['probe']['binaries']['backend']['path'],
        loaded_validation_layer=a['validation_library'] if job['validation'] else '',validation=job['validation'],scenario=job['scenario'],control=control,
        control_diagnostic=TOKENS[job['kind']] if control else '',submitted_control_work=0,
        resource_kind='none' if control else 'texture' if texture else 'buffer',width=65 if texture else 0,height=37 if texture else 0,
        texel_bytes=4 if texture else 0,buffer_bytes=4096 if not control and not texture else 0,
        resources_per_iteration=count,created_resources=total,released_resource_handles=total,used_resources=total,verified_resources=total,
        unique_native_resources_checked=total,native_resource_handles_verified=not control,
        verified_images=total if texture else 0,verified_buffers=total if not texture else 0,
        verified_bytes=total*(9620 if texture else 4096),readback_verified=not control,
        allocation_policy='public_device_resource_default_policy',usage_policy='none' if control else 'sampled_rgba8_2d' if texture else 'llgl_vertex_buffer_copy_src_dst',
        buffer_cpu_access_flags=0,initial_data_supplied=False,copy_submission_policy='separate_upload_download_completed_fence_idle',
        staging_in_create_release_boundary=False,row_stride=0,layer_stride=0,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,
        native_allocation_count_verified=False,allocator_cache_state_known=False,warmup=0 if control else 1,sample_count=0 if control else 3,
        payload_sha256=[] if control else r['reference']['sha256'][job['scenario']],native_draw_calls=0,observer_path=r['observer']['path'],
        observer_live_instances_after_close=0,observer_live_devices_after_close=0,performance_samples=0,samples_ms=[],gpu_ms=None,
        physical_GPU_accepted=False,P3_LBEN_04_completed=False,performance_accepted=False,native_workload_shortcuts=False))
    require(type(value['delivered_errors']) is int and (value['delivered_errors']>=1 if control else value['delivered_errors']==0), 'Native errors/control counter differs')
    require(type(value['observed_instance_api_version']) is int and value['observed_instance_api_version']>=4194304,'Observed LLGL instance API absent')
    modules=value['runtime_modules'];required={x['path'] for x in a['runtime_libraries']}|{r['probe']['binaries'][n]['path'] for n in ('core','backend')}|{r['crypto']['path'],r['observer']['path']}
    require(type(modules) is list and modules==sorted(set(modules)) and required.issubset(modules)
            and (a['validation_library'] in modules)==job['validation'], 'Actual runtime library binding differs')
    for path in modules:
        require(type(path) is str and pathlib.PurePosixPath(path).is_absolute() and
                (path in required or path==a['validation_library'] or any(pathlib.PurePosixPath(path).is_relative_to(n) for n in ('/usr/lib','/lib'))), 'Unreviewed runtime module')
        require('VkLayer_khronos_validation' not in path or path==a['validation_library'], 'Wrong validation library')
    return value


def rejected(job,code,stdout,stderr,mode='software-diagnostic'):
    require(type(code) is int and code==1 and not stdout,'Rejection succeeded or produced output')
    token={'missing-layer':b'Required comparison validation layer absent','wrong-uuid':b'Exact UUID absent or ambiguous',
           'native-software':b'Requested probe kind differs from native device type',
           'wrong-kind':policy.wrong_kind_message(mode),'missing-resource':b'Public resource creation count differs',
           'alias-resource':b'Duplicate live public resource handle','skip-release':b'Public resource release count differs',
           'wrong-resource-payload':b'Full changing resource readback mismatch','stale-iteration':b'Full changing resource readback mismatch',
           'skip-copy':b'Full changing resource readback mismatch','corrupt-readback':b'Full changing resource readback mismatch',
           'short-readback':b'Incomplete full resource readback','missing-observer':b'Required runtime module absent or ambiguous',
           'sync-disabled':b'Expected validation control diagnostic absent'}[job['kind']]
    require(token in stderr,'Unrelated control failure')


def validate_audit(value):
    require(len(value['compile_commands'])==len(value['depfiles'])==2 and value['actual_verbose_commands_verified'] is True,'Consumer compiler command count/log differs')
    dependencies=set()
    for command,dep in zip(value['compile_commands'],value['depfiles']):
        argv=shlex.split(command['executed_command']);p.compiler_flags(argv)
        filtered=[];i=0
        while i<len(argv):
            if argv[i] in ('-MF','-MT','-MQ'):i+=2;continue
            if argv[i]!='-MD':filtered.append(argv[i])
            i+=1
        require(filtered==shlex.split(command['command']),'CMake/Ninja consumer command differs')
        raw=dep['raw'].encode()
        require(hashlib.sha256(raw).hexdigest()==dep['sha256'] and len(raw)==dep['bytes'],'Raw GNU dependencies changed')
        dependencies.update(dep['dependencies'])
    require(dependencies=={x['path'] for x in value['files']} and len(value['files'])==len({x['path'] for x in value['files']}),'Consumer input coverage differs')
    require({x['component'] for x in value['files']}=={'own','generated','llgl','vulkan_headers','system'},'Consumer input categories differ')
    for item in value['files']:
        require(pathlib.PurePosixPath(item['path']).is_absolute() and type(item['bytes']) is int and item['bytes']>0 and re.fullmatch('[0-9a-f]{64}',item['sha256']),'Input path/size/hash invalid')
        if item['component']=='system':require(item['redistributed'] is False,'System input redistribution')
        else:require(item['selected_license'] in ({'BSD-3-Clause'} if item['component']=='llgl' else {'MIT','Apache-2.0'} if item['component']=='vulkan_headers' else {'MIT'}),'Consumer license drift')


def audit(build,call,build_stdout):
    commands=read(build/'compile_commands.json');require(len(commands)==2,'Consumer and observer units required')
    row,stdout,stderr=call('ninja-commands',['ninja','-C',build,'-t','commands','linux_llgl_allocation']);require(row['returncode']==0,'Ninja consumer command missing')
    lines=[s for s in stdout.decode().splitlines() if '-c' in shlex.split(s) and '-o' in shlex.split(s)]
    require(len(lines)==2 and all(('] '+line).encode() in build_stdout for line in lines),'Actual verbose compiler command absent')
    paths=set();deps=[]
    for command in commands:
        matching=[line for line in lines if shlex.split(line)[shlex.split(line).index('-c')+1]==command['file']]
        require(len(matching)==1,'Ambiguous actual compiler command');command['executed_command']=matching[0]
        argv=shlex.split(matching[0]);p.compiler_flags(argv)
        dep=pathlib.Path(command['directory'])/argv[argv.index('-MF')+1];raw=dep.read_bytes().decode();items=p.gnu.dependencies(raw,pathlib.Path(command['directory']))
        deps.append(dict(**record(dep),raw=raw,dependencies=[str(n) for n in items]));paths.update(items)
    paths=sorted(paths)
    own={ROOT/n for n in ('benchmarks/common/LinuxEnvironment.hpp','benchmarks/vulkan_allocation/Payload.hpp','benchmarks/llgl_vulkan_draw/Observer.hpp')}
    generated={build/n for n in ('main.cpp','Platform.hpp','Layer.cpp','SupportLinux.hpp')}
    require(generated.issubset(paths) and own.issubset(paths) and not {ROOT/'benchmarks/llgl_vulkan_upload/Context.hpp',ROOT/'benchmarks/llgl_vulkan_pso/Support.hpp'}&set(paths),'Generated consumer leaked original platform or omitted workload inputs')
    llgl=ROOT/'.deps/LLGL';headers=ROOT/'.deps/Vulkan-Headers';tracked={n:set(p.q.git(n,'ls-files').splitlines()) for n in (llgl,headers)}
    rows=[];notices=set()
    for path in paths:
        row=record(path)
        if path in own:row.update(component='own',selected_license='MIT')
        elif path in generated:row.update(component='generated',selected_license='MIT')
        elif path.is_relative_to(llgl):
            require(path.relative_to(llgl).as_posix() in tracked[llgl],'Untracked LLGL header')
            blocks=p.source_notices(path.read_text(encoding='utf-8-sig'));require('BSD' in '\n'.join(blocks),'Unreviewed LLGL header license');notices.update(blocks)
            row.update(component='llgl',selected_license='BSD-3-Clause',dependency_commit=read(ROOT/p.APPROVAL)['upstream_commit'])
        elif path.is_relative_to(headers):
            require(path.relative_to(headers).as_posix() in tracked[headers],'Untracked Vulkan header')
            text=path.read_text(encoding='utf-8-sig');licenses=re.findall(r'SPDX-License-Identifier:\s*([^\r\n*]+)',text)
            require(licenses and all(s.strip() in ('MIT','Apache-2.0','MIT OR Apache-2.0','Apache-2.0 OR MIT') for s in licenses),'Unreviewed Vulkan header license')
            row.update(component='vulkan_headers',selected_license='MIT' if any('MIT' in s for s in licenses) else 'Apache-2.0',dependency_commit=p.q.HEADERS);notices.update(p.source_notices(text))
        else:
            require(any(path.is_relative_to(pathlib.Path(n)) for n in ('/usr/include','/usr/lib','/lib')),'Unreviewed consumer input')
            row.update(component='system',redistributed=False)
        rows.append(row)
    result=dict(files=rows,compile_commands=commands,depfiles=deps,
        actual_verbose_commands_verified=True,notice_blocks=sorted(notices),scope='Actual consumer GNU dependencies; qualified shared LLGL libraries reused; system OpenSSL not bundled')
    validate_audit(result);return result


def check(folder,live=False):
    r=read(folder/'receipt.json')
    policy.validate_receipt(r,'allocation')
    p.exact(r,dict(schema_version=1,task='P3-LBEN-04-LLGL-readiness',status='completed',inputs_unchanged=True,
        software_allocation_correctness_verified=policy.mode(r)=='software-diagnostic',physical_GPU_accepted=False,P3_LBEN_04_completed=False,performance_accepted=False,performance_samples=0))
    p.check(folder/'probe');require(sha(folder/'probe/receipt.json')==r['probe_sha256'] and p.same(read(folder/'probe/receipt.json'),r['probe']),'Qualified probe changed')
    require(r['source_commit']==r['probe']['source_commit'],'Probe/consumer revision differs')
    layer=read(folder/'observer-layer.json')
    p.exact(layer,dict(file_format_version='1.2.0',layer=dict(name='VK_LAYER_GFX_llgl_draw',type='GLOBAL',
        library_path='./libVkLayer_gfx_llgl_draw.so',api_version='1.3.290',implementation_version='1',
        description='Own Linux pass-through draw observer',functions=dict(vkNegotiateLoaderLayerInterfaceVersion='gfxDrawNegotiate'))))
    require(sha(folder/'observer-layer.json')==r['observer_manifest_sha256'],'Observer manifest drift')
    require(sha(folder/'inputs.json')==r['inputs_sha256'],'Consumer input audit changed');validate_audit(read(folder/'inputs.json'))
    require(read(folder/'adaptation.json')==r['adaptation'] and r['adaptation']['workload_bodies_changed'] is False,'Adaptation manifest drift')
    for name,digest in r['adaptation']['generated'].items():require(sha(folder/'generated'/name)==digest,'Generated consumer changed')
    require(sha(folder/'source.zip')==r['source_zip_sha256'],'Executed source archive changed')
    with zipfile.ZipFile(folder/'source.zip') as z:
        require(len(z.namelist())==len(set(z.namelist()))==len(r['source_inputs']) and set(z.namelist())=={x['path'] for x in r['source_inputs']},'Source inventory changed')
        for item in r['source_inputs']:require(hashlib.sha256(z.read(item['path'])).hexdigest()==item['sha256'],'Source byte drift')
        source=lambda n:z.read(n).decode().replace('\r\n','\n')
        main,support,platform,layer,changes=port.translate(source('benchmarks/llgl_vulkan_allocation/main.cpp'),source('benchmarks/llgl_vulkan_pso/main.cpp'),source('benchmarks/llgl_vulkan_pso/Support.hpp'),source('benchmarks/llgl_vulkan_upload/Context.hpp'),source('benchmarks/llgl_vulkan_draw/Layer.cpp'),source('benchmarks/llgl_vulkan_upload/main.cpp'),source('benchmarks/llgl_vulkan_draw/main.cpp'),mode=policy.mode(r))
        require(all(text.encode()==(folder/'generated'/name).read_bytes() for name,text in [('main.cpp',main),('SupportLinux.hpp',support),('Platform.hpp',platform),('Layer.cpp',layer)]) and changes==r['adaptation']['adaptations'],'Platform adaptation/body drift')
        inputs={x['path']:x['sha256'] for x in r['source_inputs']}
        require(all(inputs[x['path']]==x['sha256'] for x in r['adaptation']['inputs']),'Adaptation input binding changed')
        identity=policy.prefix('allocation',policy.mode(r))+'|'+r['probe']['LLGL_commit']+'|'+p.PROFILE+'\n'+''.join(x['path']+'|'+x['sha256']+'\n' for x in r['adaptation']['inputs'])
        require(hashlib.sha256(identity.encode()).hexdigest()==r['adaptation']['source_fingerprint'],'Consumer fingerprint changed')
        require(json.loads(z.read(REFERENCE))==r['reference'],'Archived independent CPU reference changed')
    require(r['reference']==references()==read(folder/'payload-reference.json'),'Independent CPU reference changed')
    require((folder/'settings/vk_layer_settings.txt').read_bytes()==(folder/'probe/settings/vk_layer_settings.txt').read_bytes()
            and (folder/'sync-off/vk_layer_settings.txt').read_text()==OFF,'Validation controls/settings drift')
    schedule=policy.jobs(r,jobs());require(len(r['runs'])==len(schedule),'Missing/extra resource process');last=0;images=buffers=total=0
    for row,job in zip(r['runs'],schedule):
        env=environment(job,r)
        require(p.same(row['job'],job) and row['argv']==arguments(job,r) and row['overrides'].keys()==env.keys()
                and all(row['overrides'][k].replace('\\','/')==v.replace('\\','/') for k,v in env.items()),'Native order/argv/environment differs')
        require(type(row['pid']) is int and row['pid']>0 and all(type(row[k]) in (int,float) and math.isfinite(row[k]) for k in ('started','finished')) and last<=row['started']<=row['finished'],'Native process/serialization differs');last=row['finished']
        stdout,stderr=(p.safe(folder,row[k]).read_bytes() for k in ('stdout','stderr'))
        require(hashlib.sha256(stdout).hexdigest()==row['stdout_sha256'] and hashlib.sha256(stderr).hexdigest()==row['stderr_sha256'],'Raw output drift')
        if job['kind']=='normal' or job['kind'] in TOKENS:
            require(type(row['returncode']) is int and row['returncode']==0,'Native resource failed')
            value=validate_result(json.loads(stdout),job,r);require(p.same(value,row['result']),'Raw/parsed result differs')
            if job['kind']=='normal':require(not re.search(rb'Validation Error|SYNC-HAZARD',stderr),'Normal validation error')
            else:require(TOKENS[job['kind']].encode() in stderr,'Actual control diagnostic missing')
            require({x['path'] for x in row['runtime_files']}==set(value['runtime_modules']) and all(x in r['runtime_inputs'] for x in row['runtime_files']),'Runtime file binding differs')
            images+=value['verified_images'];buffers+=value['verified_buffers'];total+=value['verified_bytes']
        else:rejected(job,row['returncode'],stdout,stderr,policy.mode(r))
    require((r['verified_images'],r['verified_buffers'],r['verified_bytes'])==(images,buffers,total)==policy.totals(r,(8008,8008,109837728)),'Complete changing-image totals differ')
    if live:
        p.check(pathlib.Path(r['probe_evidence']),live=True)
        require(p.q.unchanged(r['frozen_inputs']+r['runtime_inputs']),'Source/build/runtime changed')
    return policy.summary(r,dict(processes=23,normal_processes=8,core_sync_controls=2,rejection_controls=13,verified_images=images,verified_buffers=buffers,verified_bytes=total,resource_create_release_pairs=images+buffers,
                performance_samples=0,physical_GPU_accepted=False,P3_LBEN_04_completed=False),'allocation')


def run(a):
    require(platform.system()=='Linux','Actual Linux host required');probe=a.probe.resolve();p.check(probe,live=True);prior=read(probe/'receipt.json')
    mode=a.mode;guard=a.stage=='guard';policy.validate_probe(prior,mode,guard)
    require(p.q.git(ROOT,'rev-parse','HEAD')==prior['source_commit'] and not p.q.git(ROOT,'status','--porcelain','--untracked-files=no'),'Clean same-revision probe required')
    out=a.output.resolve();build=out.with_name(out.name+'-build')
    require(out.is_relative_to(ROOT/'out') and not out.exists() and not build.exists(),'Fresh consumer roots required')
    out.mkdir(parents=True);build.mkdir();shutil.copytree(probe,out/'probe')
    for n in ('commands','empty','settings','sync-off','xdg','generated'):(out/n).mkdir(mode=0o700)
    shutil.copyfile(probe/'settings/vk_layer_settings.txt',out/'settings/vk_layer_settings.txt');(out/'sync-off/vk_layer_settings.txt').write_text(OFF,encoding='utf-8',newline='\n')
    save(out/'payload-reference.json',references())
    sources=set(ROOT/n for n in (*port.OWN,REFERENCE,'tools/port_linux_llgl_upload.py','tools/linux_llgl_allocation.py','tools/linux_llgl_probe.py',p.APPROVAL,'LICENSE'))
    before=[record(n) for n in sorted(sources)]
    base={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_','LD_','OPENSSL_')) and k not in ('DISPLAY','WAYLAND_DISPLAY')}
    commands=[]
    def call(name,argv,env=None):
        start=time.monotonic();child=subprocess.Popen(list(map(str,argv)),cwd=ROOT,env={**base,**(env or {})},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:stdout,stderr=child.communicate(timeout=600)
        except subprocess.TimeoutExpired:child.kill();stdout,stderr=child.communicate();(out/'commands'/(name+'.stdout')).write_bytes(stdout);(out/'commands'/(name+'.stderr')).write_bytes(stderr);raise
        row=dict(argv=list(map(str,argv)),overrides=env or {},pid=child.pid,started=start,finished=time.monotonic(),returncode=child.returncode)
        for key,data in (('stdout',stdout),('stderr',stderr)):
            path=out/'commands'/(name+'.'+key);path.write_bytes(data);row[key]=path.relative_to(out).as_posix();row[key+'_sha256']=sha(path)
        commands.append(dict(name=name,**row));save(out/'commands.json',commands);return row,stdout,stderr
    qualified=pathlib.Path(prior['output_directory']).with_name(pathlib.Path(prior['output_directory']).name+'-build')
    for name,argv in [('configure',['cmake','-S',ROOT/'benchmarks/linux_llgl_allocation','-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DGFX_LLGL_WORKLOAD_MODE='+mode,'-DLLGL_PROBE_BUILD='+str(qualified),'-DVulkan_LIBRARY='+prior['qualification']['loader_path']]),('build',['cmake','--build',build,'--target','linux_llgl_allocation','--parallel','2','--verbose','--','-d','keepdepfile'])]:
        row,stdout,stderr=call(name,argv)
        if row['returncode']:print((stdout+stderr).decode(errors='replace')[-12000:],flush=True)
        require(row['returncode']==0,name+' failed; raw logs retained');print(name+' PASS',flush=True)
    inputs=audit(build,call,stdout);save(out/'inputs.json',inputs)
    shutil.copyfile(build/'VkLayer_gfx_llgl_draw.json',out/'observer-layer.json')
    crypto=record(pathlib.Path(p.q.cache(build/'CMakeCache.txt')['OPENSSL_CRYPTO_LIBRARY']));require('.so' in crypto['path'],'Shared system OpenSSL required')
    adaptation=read(build/'adaptation.json');shutil.copyfile(build/'adaptation.json',out/'adaptation.json')
    for n in ('main.cpp','Platform.hpp','Layer.cpp','SupportLinux.hpp'):shutil.copyfile(build/n,out/'generated'/n)
    require(p.q.unchanged(before),'Source changed during consumer build')
    source_inputs=[dict(path=n.relative_to(ROOT).as_posix(),sha256=sha(n)) for n in sorted(sources)]
    with zipfile.ZipFile(out/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for n in sorted(sources):z.write(n,n.relative_to(ROOT).as_posix())
    r=dict(schema_version=1,task='P3-LBEN-04-LLGL-readiness',status='running',source_commit=prior['source_commit'],probe=prior,probe_evidence=str(probe),probe_sha256=sha(out/'probe/receipt.json'),
        output_directory=str(out),adaptation=adaptation,source_inputs=source_inputs,source_zip_sha256=sha(out/'source.zip'),binary=record(build/'linux_llgl_allocation'),crypto=crypto,observer=record(build/'libVkLayer_gfx_llgl_draw.so'),observer_manifest_sha256=sha(out/'observer-layer.json'),
        reference=references(),inputs_sha256=sha(out/'inputs.json'),software_allocation_correctness_verified=False,physical_GPU_accepted=False,P3_LBEN_04_completed=False,
        performance_accepted=False,performance_samples=0,verified_images=0,verified_buffers=0,verified_bytes=0,runtime_inputs=[],runs=[])
    policy.initialize(r,mode,guard,'allocation')
    r['frozen_inputs']=prior['frozen_inputs']+prior['runtime_inputs']+before+[r['binary'],r['observer'],crypto,record(build/'VkLayer_gfx_llgl_draw.json')]+[{k:x[k] for k in ('path','bytes','sha256')} for x in inputs['files']+inputs['depfiles']]
    r['frozen_inputs'] += [record(build/n) for n in ('main.cpp','Platform.hpp','Layer.cpp','SupportLinux.hpp','adaptation.json','CMakeCache.txt','build.ninja','compile_commands.json')]
    r['frozen_inputs'] += [record(out/n) for n in ('source.zip','inputs.json','payload-reference.json','observer-layer.json','settings/vk_layer_settings.txt','sync-off/vk_layer_settings.txt')]
    require(adaptation['source_fingerprint'].encode() in (build/'linux_llgl_allocation').read_bytes(),'Compiled consumer fingerprint differs')
    save(out/'receipt.json',r)
    try:
        for job in policy.jobs(r,jobs()):
            require(p.q.unchanged(r['frozen_inputs']+r['runtime_inputs']),'Frozen consumer input changed')
            row,stdout,stderr=call(job['name'],arguments(job,r),environment(job,r));row['job']=job;r['runs'].append(row);save(out/'receipt.json',r)
            if job['kind']=='normal' or job['kind'] in TOKENS:
                require(row['returncode']==0,'Native resource failed; raw logs retained');value=validate_result(json.loads(stdout),job,r);row['result']=value
                row['runtime_files']=[record(pathlib.Path(n)) for n in value['runtime_modules']]
                for item in row['runtime_files']:
                    matches=[x for x in r['runtime_inputs'] if x['path']==item['path']];require(not matches or matches==[item],'Runtime file changed')
                    if not matches:r['runtime_inputs'].append(item)
                r['verified_images']+=value['verified_images'];r['verified_buffers']+=value['verified_buffers'];r['verified_bytes']+=value['verified_bytes']
            else:rejected(job,row['returncode'],stdout,stderr,policy.mode(r))
            save(out/'receipt.json',r);print(job['name']+' PASS',flush=True)
        require(p.q.unchanged(r['frozen_inputs']+r['runtime_inputs']),'Final consumer inputs changed')
        r.update(status='completed',inputs_unchanged=True);policy.finish(r,'allocation');save(out/'receipt.json',r)
        print(json.dumps(check(out,live=True)),flush=True)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as exc:
        r.update(status='failed',error=str(exc));save(out/'receipt.json',r);raise


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',choices=('run','check','guard'));parser.add_argument('--mode',choices=('software-diagnostic','physical'),default='software-diagnostic');parser.add_argument('--probe',type=pathlib.Path);parser.add_argument('--output',type=pathlib.Path,required=True);parser.add_argument('--live',action='store_true');a=parser.parse_args()
    if a.stage=='check':print(json.dumps(check(a.output,a.live),indent=2))
    else:require(a.probe is not None,'Qualified same-revision Linux LLGL probe required');run(a)
