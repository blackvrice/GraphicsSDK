"""Verify all approved LLGL upload cases on qualified Linux software; no timing campaign."""
import argparse
import hashlib
import json
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
import port_linux_llgl_upload as port
from linux_comparison import ROOT, read, save, sha, record, require

REFERENCE='benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/candidate/payload-reference.json'
SHAPES=dict(odd=[65,37,4],upload1080=[1920,1080,4],upload4k=[3840,2160,4],upload4k16f=[3840,2160,8])
TOKENS={'core-control':'VUID-VkPhysicalDeviceProperties2-sType-sType','sync-control':'SYNC-HAZARD-WRITE-AFTER-WRITE'}
OFF='khronos_validation.validate_sync = false\nkhronos_validation.syncval_submit_time_validation = false\n'


def jobs():
    result=[dict(kind='normal',scenario=s,validation=v) for v in (False,True) for s in SHAPES]
    result += [dict(kind=s,scenario=s,validation=True) for s in TOKENS]
    result += [dict(kind=k,scenario=s,validation=v) for k,s,v in (
        ('missing-layer','odd',True),('wrong-uuid','odd',False),('wrong-kind','odd',False),
        ('corrupt-readback','corrupt-readback',False),('sync-disabled','sync-control',True))]
    return [dict(j,name=f"{i:02}-{j['kind']}-{j['scenario']}-{int(j['validation'])}") for i,j in enumerate(result)]


def arguments(job,r):
    return [r['binary']['path'],'0'*32 if job['kind']=='wrong-uuid' else r['probe']['qualification']['device_uuid'],
            'validation' if job['validation'] else 'plain',job['scenario']]


def environment(job,r):
    a=r['probe']['qualification'];out=pathlib.Path(r['output_directory'])
    env=p.q.overrides(out,a['icd']['path'],a['validation_library'],job['validation'])
    env.update(LD_LIBRARY_PATH=str(pathlib.PurePosixPath(r['probe']['binaries']['core']['path']).parent),
               GFX_LLGL_UPLOAD_KIND='physical' if job['kind']=='wrong-kind' else 'software-diagnostic')
    if job['kind']=='missing-layer':env['VK_LAYER_PATH']=str(out/'empty')
    if job['kind']=='sync-disabled':env.update(VK_LAYER_SETTINGS_PATH=str(out/'sync-off'),VK_KHRONOS_VALIDATION_VALIDATE_SYNC='false')
    return env


def validate_result(value,job,r):
    a=r['probe']['qualification'];d=a['selected'];control=job['kind'] in TOKENS
    shape=[0,0,0] if control else SHAPES[job['scenario']]
    native=next(x['result'] for x in r['probe']['runs'] if x['job']['kind']=='normal')
    p.exact(value,dict(schema_version=1,task='P3-LBEN-01-LLGL-readiness',sdk='llgl',api='vulkan',configuration='Release',
        contract='vulkan_upload_cpu_payload_to_texture_fence_complete_each_sample_readback_v1',source_fingerprint=r['adaptation']['source_fingerprint'],
        LLGL_commit=r['probe']['LLGL_commit'],dependency_profile=p.PROFILE,unmodified_upstream=False,
        selected=d,environment=a['environment'],mode='software-diagnostic',device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
        native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
        native_handle_verified=True,queue_family=native['queue_family'],queue_index=0,queue_flags=native['queue_flags'],loader_api_version=native['loader_api_version'],
        instance_API_request_observed=False,loader_path=a['loader_path'],LLGL_path=r['probe']['binaries']['core']['path'],backend_path=r['probe']['binaries']['backend']['path'],
        loaded_validation_layer=a['validation_library'] if job['validation'] else '',validation=job['validation'],scenario=job['scenario'],control=control,
        control_diagnostic=TOKENS[job['kind']] if control else '',submitted_control_work=0,width=shape[0],height=shape[1],texel_bytes=shape[2],
        warmup=0 if control else 1,sample_count=0 if control else 3,verified_images=0 if control else 4,
        verified_bytes=0 if control else 4*shape[0]*shape[1]*shape[2],readback_verified=not control,row_stride=0,layer_stride=0,
        payload_sha256=[] if control else r['reference']['hashes'][job['scenario']],performance_samples=0,samples_ms=[],gpu_ms=None,
        physical_GPU_accepted=False,P3_LBEN_01_completed=False,performance_accepted=False,native_workload_shortcuts=False))
    require(type(value['delivered_errors']) is int and (value['delivered_errors']>=1 if control else value['delivered_errors']==0), 'Native errors/control counter differs')
    modules=value['runtime_modules'];required={x['path'] for x in a['runtime_libraries']}|{r['probe']['binaries'][n]['path'] for n in ('core','backend')}|{r['crypto']['path']}
    require(type(modules) is list and modules==sorted(set(modules)) and required.issubset(modules)
            and (a['validation_library'] in modules)==job['validation'], 'Actual runtime library binding differs')
    for path in modules:
        require(type(path) is str and pathlib.PurePosixPath(path).is_absolute() and
                (path in required or path==a['validation_library'] or any(pathlib.PurePosixPath(path).is_relative_to(n) for n in ('/usr/lib','/lib'))), 'Unreviewed runtime module')
        require('VkLayer_khronos_validation' not in path or path==a['validation_library'], 'Wrong validation library')
    return value


def rejected(job,code,stdout,stderr):
    require(type(code) is int and code==1 and not stdout,'Rejection succeeded or produced output')
    token={'missing-layer':b'Required comparison validation layer absent','wrong-uuid':b'Exact UUID absent or ambiguous',
           'wrong-kind':b'Software diagnostics only','corrupt-readback':b'Full changing upload readback mismatch',
           'sync-disabled':b'Expected validation control diagnostic absent'}[job['kind']]
    require(token in stderr,'Unrelated control failure')


def validate_audit(value):
    require(len(value['compile_commands'])==len(value['depfiles'])==1 and value['actual_verbose_commands_verified'] is True,'Consumer compiler command count/log differs')
    command=value['compile_commands'][0];argv=shlex.split(command['executed_command']);p.compiler_flags(argv)
    filtered=[];i=0
    while i<len(argv):
        if argv[i] in ('-MF','-MT','-MQ'):i+=2;continue
        if argv[i]!='-MD':filtered.append(argv[i])
        i+=1
    require(filtered==shlex.split(command['command']),'CMake/Ninja consumer command differs')
    dep=value['depfiles'][0];raw=dep['raw'].encode()
    require(hashlib.sha256(raw).hexdigest()==dep['sha256'] and len(raw)==dep['bytes'],'Raw GNU dependencies changed')
    require(set(dep['dependencies'])=={x['path'] for x in value['files']} and len(value['files'])==len({x['path'] for x in value['files']}),'Consumer input coverage differs')
    require({x['component'] for x in value['files']}=={'own','generated','llgl','vulkan_headers','system'},'Consumer input categories differ')
    for item in value['files']:
        require(pathlib.PurePosixPath(item['path']).is_absolute() and type(item['bytes']) is int and item['bytes']>0 and re.fullmatch('[0-9a-f]{64}',item['sha256']),'Input path/size/hash invalid')
        if item['component']=='system':require(item['redistributed'] is False,'System input redistribution')
        else:require(item['selected_license'] in ({'BSD-3-Clause'} if item['component']=='llgl' else {'MIT','Apache-2.0'} if item['component']=='vulkan_headers' else {'MIT'}),'Consumer license drift')


def audit(build,call,build_stdout):
    commands=read(build/'compile_commands.json');require(len(commands)==1,'Exactly one consumer translation unit required')
    row,stdout,stderr=call('ninja-commands',['ninja','-C',build,'-t','commands','linux_llgl_upload']);require(row['returncode']==0,'Ninja consumer command missing')
    lines=[s for s in stdout.decode().splitlines() if '-c' in shlex.split(s) and '-o' in shlex.split(s)]
    require(len(lines)==1 and ('] '+lines[0]).encode() in build_stdout,'Actual verbose compiler command absent')
    command=commands[0];command['executed_command']=lines[0];argv=shlex.split(lines[0]);p.compiler_flags(argv)
    dep=pathlib.Path(command['directory'])/argv[argv.index('-MF')+1];raw=dep.read_bytes().decode();paths=p.gnu.dependencies(raw,pathlib.Path(command['directory']))
    own={ROOT/'benchmarks/common/LinuxEnvironment.hpp',ROOT/'benchmarks/llgl_vulkan_upload/Payload.hpp'}
    generated={build/'main.cpp',build/'Platform.hpp'}
    require(generated.issubset(paths) and own.issubset(paths) and ROOT/'benchmarks/llgl_vulkan_upload/Context.hpp' not in paths,'Generated consumer leaked original platform or omitted workload inputs')
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
    result=dict(files=rows,compile_commands=commands,depfiles=[dict(**record(dep),raw=raw,dependencies=[str(n) for n in paths])],
        actual_verbose_commands_verified=True,notice_blocks=sorted(notices),scope='Actual consumer GNU dependencies; qualified shared LLGL libraries reused; system OpenSSL not bundled')
    validate_audit(result);return result


def check(folder,live=False):
    r=read(folder/'receipt.json')
    p.exact(r,dict(schema_version=1,task='P3-LBEN-01-LLGL-readiness',status='completed',inputs_unchanged=True,
        software_upload_correctness_verified=True,physical_GPU_accepted=False,P3_LBEN_01_completed=False,performance_accepted=False,performance_samples=0))
    p.check(folder/'probe');require(sha(folder/'probe/receipt.json')==r['probe_sha256'] and p.same(read(folder/'probe/receipt.json'),r['probe']),'Qualified probe changed')
    require(r['source_commit']==r['probe']['source_commit'],'Probe/consumer revision differs')
    require(sha(folder/'inputs.json')==r['inputs_sha256'],'Consumer input audit changed');validate_audit(read(folder/'inputs.json'))
    require(read(folder/'adaptation.json')==r['adaptation'] and r['adaptation']['workload_bodies_changed'] is False,'Adaptation manifest drift')
    for name,digest in r['adaptation']['generated'].items():require(sha(folder/'generated'/name)==digest,'Generated consumer changed')
    require(sha(folder/'source.zip')==r['source_zip_sha256'],'Executed source archive changed')
    with zipfile.ZipFile(folder/'source.zip') as z:
        require(len(z.namelist())==len(set(z.namelist()))==len(r['source_inputs']) and set(z.namelist())=={x['path'] for x in r['source_inputs']},'Source inventory changed')
        for item in r['source_inputs']:require(hashlib.sha256(z.read(item['path'])).hexdigest()==item['sha256'],'Source byte drift')
        original=z.read('benchmarks/llgl_vulkan_upload/main.cpp').decode().replace('\r\n','\n');context=z.read('benchmarks/llgl_vulkan_upload/Context.hpp').decode().replace('\r\n','\n')
        main,platform,changes=port.translate(original,context)
        require(main.encode()==(folder/'generated/main.cpp').read_bytes() and platform.encode()==(folder/'generated/Platform.hpp').read_bytes() and changes==r['adaptation']['adaptations'],'Platform adaptation/body drift')
        inputs={x['path']:x['sha256'] for x in r['source_inputs']}
        require(all(inputs[x['path']]==x['sha256'] for x in r['adaptation']['inputs']),'Adaptation input binding changed')
        identity='linux-llgl-upload|'+r['probe']['LLGL_commit']+'|'+p.PROFILE+'\n'+''.join(x['path']+'|'+x['sha256']+'\n' for x in r['adaptation']['inputs'])
        require(hashlib.sha256(identity.encode()).hexdigest()==r['adaptation']['source_fingerprint'],'Consumer fingerprint changed')
        require(json.loads(z.read(REFERENCE))==r['reference']==read(folder/'payload-reference.json'),'CPU reference differs')
    require(r['reference']==read(ROOT/REFERENCE),'Frozen independent CPU reference changed')
    require((folder/'settings/vk_layer_settings.txt').read_bytes()==(folder/'probe/settings/vk_layer_settings.txt').read_bytes()
            and (folder/'sync-off/vk_layer_settings.txt').read_text()==OFF,'Validation controls/settings drift')
    schedule=jobs();require(len(r['runs'])==len(schedule),'Missing/extra upload process');last=0;images=total=0
    for row,job in zip(r['runs'],schedule):
        env=environment(job,r)
        require(p.same(row['job'],job) and row['argv']==arguments(job,r) and row['overrides'].keys()==env.keys()
                and all(row['overrides'][k].replace('\\','/')==v.replace('\\','/') for k,v in env.items()),'Native order/argv/environment differs')
        require(type(row['pid']) is int and row['pid']>0 and last<=row['started']<=row['finished'],'Native process/serialization differs');last=row['finished']
        stdout,stderr=(p.safe(folder,row[k]).read_bytes() for k in ('stdout','stderr'))
        require(hashlib.sha256(stdout).hexdigest()==row['stdout_sha256'] and hashlib.sha256(stderr).hexdigest()==row['stderr_sha256'],'Raw output drift')
        if job['kind']=='normal' or job['kind'] in TOKENS:
            require(type(row['returncode']) is int and row['returncode']==0,'Native upload failed')
            value=validate_result(json.loads(stdout),job,r);require(p.same(value,row['result']),'Raw/parsed result differs')
            if job['kind']=='normal':require(not re.search(rb'Validation Error|SYNC-HAZARD',stderr),'Normal validation error')
            else:require(TOKENS[job['kind']].encode() in stderr,'Actual control diagnostic missing')
            require({x['path'] for x in row['runtime_files']}==set(value['runtime_modules']) and all(x in r['runtime_inputs'] for x in row['runtime_files']),'Runtime file binding differs')
            images+=value['verified_images'];total+=value['verified_bytes']
        else:rejected(job,row['returncode'],stdout,stderr)
    require((r['verified_images'],r['verified_bytes'])==(images,total)==(32,862694560),'Complete changing-image totals differ')
    if live:
        p.check(pathlib.Path(r['probe_evidence']),live=True)
        require(p.q.unchanged(r['frozen_inputs']+r['runtime_inputs']),'Source/build/runtime changed')
    return dict(processes=15,normal_processes=8,core_sync_controls=2,rejection_controls=5,verified_images=images,verified_bytes=total,
                performance_samples=0,physical_GPU_accepted=False,P3_LBEN_01_completed=False)


def run(a):
    require(platform.system()=='Linux','Actual Linux host required');probe=a.probe.resolve();p.check(probe,live=True);prior=read(probe/'receipt.json')
    require(p.q.git(ROOT,'rev-parse','HEAD')==prior['source_commit'] and not p.q.git(ROOT,'status','--porcelain','--untracked-files=no'),'Clean same-revision probe required')
    out=a.output.resolve();build=out.with_name(out.name+'-build')
    require(out.is_relative_to(ROOT/'out') and not out.exists() and not build.exists(),'Fresh consumer roots required')
    out.mkdir(parents=True);build.mkdir();shutil.copytree(probe,out/'probe')
    for n in ('commands','empty','settings','sync-off','xdg','generated'):(out/n).mkdir(mode=0o700)
    shutil.copyfile(probe/'settings/vk_layer_settings.txt',out/'settings/vk_layer_settings.txt');(out/'sync-off/vk_layer_settings.txt').write_text(OFF,encoding='utf-8',newline='\n')
    shutil.copyfile(ROOT/REFERENCE,out/'payload-reference.json')
    sources=set(ROOT/n for n in (*port.OWN,REFERENCE,'tools/linux_llgl_upload.py','tools/linux_llgl_probe.py',p.APPROVAL,'LICENSE'))
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
    for name,argv in [('configure',['cmake','-S',ROOT/'benchmarks/linux_llgl_upload','-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DLLGL_PROBE_BUILD='+str(qualified),'-DVulkan_LIBRARY='+prior['qualification']['loader_path']]),('build',['cmake','--build',build,'--target','linux_llgl_upload','--parallel','2','--verbose','--','-d','keepdepfile'])]:
        row,stdout,stderr=call(name,argv)
        if row['returncode']:print((stdout+stderr).decode(errors='replace')[-12000:],flush=True)
        require(row['returncode']==0,name+' failed; raw logs retained');print(name+' PASS',flush=True)
    inputs=audit(build,call,stdout);save(out/'inputs.json',inputs)
    crypto=record(pathlib.Path(p.q.cache(build/'CMakeCache.txt')['OPENSSL_CRYPTO_LIBRARY']));require('.so' in crypto['path'],'Shared system OpenSSL required')
    adaptation=read(build/'adaptation.json');shutil.copyfile(build/'adaptation.json',out/'adaptation.json')
    for n in ('main.cpp','Platform.hpp'):shutil.copyfile(build/n,out/'generated'/n)
    require(p.q.unchanged(before),'Source changed during consumer build')
    source_inputs=[dict(path=n.relative_to(ROOT).as_posix(),sha256=sha(n)) for n in sorted(sources)]
    with zipfile.ZipFile(out/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for n in sorted(sources):z.write(n,n.relative_to(ROOT).as_posix())
    r=dict(schema_version=1,task='P3-LBEN-01-LLGL-readiness',status='running',source_commit=prior['source_commit'],probe=prior,probe_evidence=str(probe),probe_sha256=sha(out/'probe/receipt.json'),
        output_directory=str(out),adaptation=adaptation,source_inputs=source_inputs,source_zip_sha256=sha(out/'source.zip'),binary=record(build/'linux_llgl_upload'),crypto=crypto,
        reference=read(ROOT/REFERENCE),inputs_sha256=sha(out/'inputs.json'),software_upload_correctness_verified=False,physical_GPU_accepted=False,P3_LBEN_01_completed=False,
        performance_accepted=False,performance_samples=0,verified_images=0,verified_bytes=0,runtime_inputs=[],runs=[])
    r['frozen_inputs']=prior['frozen_inputs']+prior['runtime_inputs']+before+[r['binary'],crypto]+[{k:x[k] for k in ('path','bytes','sha256')} for x in inputs['files']+inputs['depfiles']]
    r['frozen_inputs'] += [record(build/n) for n in ('main.cpp','Platform.hpp','adaptation.json','CMakeCache.txt','build.ninja','compile_commands.json')]
    r['frozen_inputs'] += [record(out/n) for n in ('source.zip','inputs.json','payload-reference.json','settings/vk_layer_settings.txt','sync-off/vk_layer_settings.txt')]
    require(adaptation['source_fingerprint'].encode() in (build/'linux_llgl_upload').read_bytes(),'Compiled consumer fingerprint differs')
    save(out/'receipt.json',r)
    try:
        for job in jobs():
            require(p.q.unchanged(r['frozen_inputs']+r['runtime_inputs']),'Frozen consumer input changed')
            row,stdout,stderr=call(job['name'],arguments(job,r),environment(job,r));row['job']=job;r['runs'].append(row);save(out/'receipt.json',r)
            if job['kind']=='normal' or job['kind'] in TOKENS:
                require(row['returncode']==0,'Native upload failed; raw logs retained');value=validate_result(json.loads(stdout),job,r);row['result']=value
                row['runtime_files']=[record(pathlib.Path(n)) for n in value['runtime_modules']]
                for item in row['runtime_files']:
                    matches=[x for x in r['runtime_inputs'] if x['path']==item['path']];require(not matches or matches==[item],'Runtime file changed')
                    if not matches:r['runtime_inputs'].append(item)
                r['verified_images']+=value['verified_images'];r['verified_bytes']+=value['verified_bytes']
            else:rejected(job,row['returncode'],stdout,stderr)
            save(out/'receipt.json',r);print(job['name']+' PASS',flush=True)
        require(p.q.unchanged(r['frozen_inputs']+r['runtime_inputs']),'Final consumer inputs changed')
        r.update(status='completed',inputs_unchanged=True,software_upload_correctness_verified=True);save(out/'receipt.json',r)
        print(json.dumps(check(out,live=True)),flush=True)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as exc:
        r.update(status='failed',error=str(exc));save(out/'receipt.json',r);raise


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',choices=('run','check'));parser.add_argument('--probe',type=pathlib.Path);parser.add_argument('--output',type=pathlib.Path,required=True);parser.add_argument('--live',action='store_true');a=parser.parse_args()
    if a.stage=='check':print(json.dumps(check(a.output,a.live),indent=2))
    else:require(a.probe is not None,'Qualified same-revision Linux LLGL probe required');run(a)
