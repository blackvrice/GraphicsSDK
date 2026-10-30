"""Build the approved LLGL profile on Linux; minimal software probes, no benchmark."""
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

import linux_comparison as q
import audit_linux_package as gnu
from audit_licenses import source_notices
from prepare_llgl_transfer_src_candidate import patch, RELATIVE
from linux_comparison import ROOT, read, save, sha, record, require

PROFILE = 'llgl-fixed-pin-transfer-src-v1'
APPROVAL = 'benchmarks/phase3_comparison/competitors/llgl-transfer-src-profile.json'
APPROVED_ORIGINAL = 'benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/patch/VKCommandContext.original.cpp'
APPROVED_OVERLAY = 'benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/patch/VKCommandContext.cpp'
OWN = ('benchmarks/linux_llgl_probe/main.cpp', 'benchmarks/linux_llgl_probe/CMakeLists.txt',
       'benchmarks/common/LinuxEnvironment.hpp', 'benchmarks/competitors/sources.lock.json')
VUID = 'VUID-VkPhysicalDeviceProperties2-sType-sType'


def same(a, b):
    if type(a) is not type(b): return False
    if isinstance(b, dict): return a.keys() == b.keys() and all(same(a[k], b[k]) for k in b)
    if isinstance(b, list): return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    return a == b


def exact(row, expected):
    require(all(k in row and same(row[k], v) for k, v in expected.items()), 'Probe identity, work or scope differs')


def fingerprint(inputs, overlay_sha, pin):
    data = 'linux-llgl|' + pin + '|' + PROFILE + '\n'
    data += ''.join(p+'|'+inputs[p]+'\n' for p in sorted(OWN))
    return hashlib.sha256((data+'overlay|'+overlay_sha+'\n').encode()).hexdigest()


def jobs():
    rows = [dict(kind='normal', operation=op, validation=v)
            for v in (False, True) for op in ('identity', 'smoke')]
    rows += [dict(kind='callback-control', operation='callback-control', validation=True),
             dict(kind='wrong-uuid', operation='smoke', validation=False),
             dict(kind='wrong-kind', operation='smoke', validation=False),
             dict(kind='missing-layer', operation='smoke', validation=True)]
    return [dict(j, name=f"{i:02}-{j['kind']}-{j['operation']}-{int(j['validation'])}") for i, j in enumerate(rows)]


def arguments(job, r):
    return [r['binaries']['probe']['path'], '0'*32 if job['kind']=='wrong-uuid' else r['qualification']['device_uuid'],
            'validation' if job['validation'] else 'plain', job['operation'],
            'physical' if job['kind']=='wrong-kind' else 'software-diagnostic']


def environment(job, r):
    a=r['qualification']; out=pathlib.Path(r['output_directory'])
    env=q.overrides(out, a['icd']['path'], a['validation_library'], job['validation'])
    env['LD_LIBRARY_PATH']=str(pathlib.PurePosixPath(r['binaries']['core']['path']).parent)
    if job['kind']=='missing-layer': env['VK_LAYER_PATH']=str(out/'empty')
    return env


def validate_result(value, job, r):
    a=r['qualification']; control=job['kind']=='callback-control'; smoke=job['operation']=='smoke'
    exact(value, dict(schema_version=1, sdk='llgl', task='P3-LBEN-LLGL-readiness', profile_id=PROFILE,
        unmodified_upstream=False, source_fingerprint=r['probe_fingerprint'], LLGL_commit=r['LLGL_commit'],
        selected=a['selected'], environment=a['environment'], mode='software-diagnostic', operation=job['operation'],
        native_handle_verified=True, queue_index=0, validation=job['validation'], control_only=control,
        delivered_errors=1 if control else 0, diagnostic_ids=[VUID] if control else [],
        verified_bytes=12288 if smoke else 0, reference_rounds=3 if smoke else 0,
        submitted_control_work=0, loader_path=a['loader_path'],
        LLGL_path=r['binaries']['core']['path'], backend_path=r['binaries']['backend']['path'],
        loaded_validation_layer=a['validation_library'] if job['validation'] else '',
        instance_API_request_observed=False, performance_samples=0, samples_ms=[], gpu_ms=None,
        physical_GPU_accepted=False, workload_correctness_accepted=False, performance_accepted=False))
    require(type(value['queue_family']) is int and value['queue_family']>=0
            and type(value['queue_flags']) is int and value['queue_flags']&1
            and type(value['loader_api_version']) is int and value['loader_api_version']>=4198400,
            'Actual native graphics queue/loader API absent')
    require(type(value['LLGL_log_errors']) is int and (value['LLGL_log_errors'] in (0,1) if control else value['LLGL_log_errors']==0),
            'Unexpected LLGL error')
    modules=value['runtime_modules']
    require(type(modules) is list and modules==sorted(set(modules)) and modules
            and all(type(p) is str and pathlib.PurePosixPath(p).is_absolute() for p in modules), 'Runtime modules invalid')
    required={x['path'] for x in a['runtime_libraries']}|{r['binaries'][n]['path'] for n in ('core','backend')}
    require(required.issubset(modules) and (a['validation_library'] in modules)==job['validation'], 'Actual module binding differs')
    for p in modules:
        require(p in required or p==a['validation_library'] or any(pathlib.PurePosixPath(p).is_relative_to(base)
            for base in ('/usr/lib','/lib')), 'Unreviewed runtime module')
        require('VkLayer_khronos_validation' not in p or p==a['validation_library'], 'Unpinned validation library')
    return value


def rejected(job, code, stdout, stderr):
    require(type(code) is int and code==1 and not stdout, 'Rejection succeeded or produced samples')
    token={'wrong-uuid':b'Exact UUID absent or ambiguous', 'wrong-kind':b'Software diagnostics only',
           'missing-layer':b'Required comparison validation layer absent'}[job['kind']]
    require(token in stderr, 'Unrelated rejection')


def compiler_flags(argv):
    require('-O2' in argv and '-DNDEBUG' in argv and '-MD' in argv and '-MF' in argv
            and not any(x.startswith(('-flto','-march=','-mtune=')) or x in ('-O0','-O1','-O3','-Os','-Oz') for x in argv),
            'Unreviewed GNU compiler flags')


def validate_audit(value):
    require(len(value['compile_commands'])>=100 and len(value['depfiles'])==len(value['compile_commands'])
            and {x['component'] for x in value['files']}=={'own','approved_overlay','llgl','vulkan_headers','system'}, 'Incomplete GNU input audit')
    require(len({x['path'] for x in value['files']})==len(value['files']), 'Duplicate compiler input')
    referenced=set()
    for command, dep in zip(value['compile_commands'], value['depfiles']):
        argv=shlex.split(command['executed_command']); compiler_flags(argv)
        filtered=[]; i=0
        while i<len(argv):
            if argv[i] in ('-MF','-MT','-MQ'): i+=2; continue
            if argv[i]!='-MD': filtered.append(argv[i])
            i+=1
        require(filtered==shlex.split(command['command']), 'CMake/Ninja compiler command differs')
        data=dep['raw'].encode()
        require(len(data)==dep['bytes'] and hashlib.sha256(data).hexdigest()==dep['sha256'], 'Raw GNU depfile changed')
        referenced.update(dep['dependencies'])
    require(referenced=={x['path'] for x in value['files']}, 'GNU dependency/input coverage differs')
    for item in value['files']:
        require(pathlib.PurePosixPath(item['path']).is_absolute() and type(item['bytes']) is int and item['bytes']>0
                and re.fullmatch('[0-9a-f]{64}',item['sha256']), 'Invalid input path/size/hash')
        if item['component']=='system': require(item['redistributed'] is False, 'System input redistribution')
        else:
            require(item['selected_license'] in ({'MIT'} if item['component']=='own' else
                {'MIT','Apache-2.0'} if item['component']=='vulkan_headers' else {'BSD-3-Clause'}), 'Compiler license branch changed')


def audit(build, overlay, call, build_stdout=None):
    if call:
        row,stdout,stderr=call('executed-commands',['ninja','-C',build,'-t','commands','linux_llgl_probe'])
        require(row['returncode']==0,'Ninja command audit failed')
    else: stdout=subprocess.check_output(['ninja','-C',str(build),'-t','commands','linux_llgl_probe'])
    actual={}
    for line in stdout.decode().splitlines():
        argv=shlex.split(line)
        if '-c' in argv and '-o' in argv:
            key=(argv[argv.index('-c')+1],argv[argv.index('-o')+1])
            require(key not in actual, 'Duplicate Ninja compiler command'); actual[key]=line
    commands=read(build/'compile_commands.json'); paths=set(); depfiles=[]
    for command in commands:
        argv=shlex.split(command['command'])
        key=(argv[argv.index('-c')+1],argv[argv.index('-o')+1])
        require(key in actual,'CMake source/output absent from Ninja target graph')
        command['executed_command']=actual.pop(key)
        executed=shlex.split(command['executed_command']); compiler_flags(executed)
        if build_stdout is not None:
            require(('] '+command['executed_command']).encode() in build_stdout, 'Compiler command absent from actual verbose build')
        dep=pathlib.Path(command['directory'])/executed[executed.index('-MF')+1]
        require(dep.is_file(), 'Compiler depfile not retained')
        raw=dep.read_bytes().decode(); dependencies=gnu.dependencies(raw,pathlib.Path(command['directory']))
        paths.update(dependencies); depfiles.append(dict(**record(dep),raw=raw,dependencies=[str(p) for p in dependencies]))
    require(not actual and len(commands)>=100 and len(depfiles)==len(commands), 'Incomplete actual LLGL compiler inputs')
    require(overlay in paths and ROOT/'.deps/LLGL'/RELATIVE not in paths, 'Overlay did not replace exactly the compiled upstream context')
    llgl=ROOT/'.deps/LLGL'; headers=ROOT/'.deps/Vulkan-Headers'
    tracked={p:set(q.git(p,'ls-files').splitlines()) for p in (llgl,headers)}
    own={ROOT/p for p in OWN}; rows=[]; notices=set()
    for p in sorted(paths):
        row=record(p)
        if p in own: row.update(component='own', selected_license='MIT')
        elif p==overlay:
            row.update(component='approved_overlay', selected_license='BSD-3-Clause')
        elif p.is_relative_to(llgl):
            relative=p.relative_to(llgl).as_posix(); require(relative in tracked[llgl], 'Untracked LLGL compiler input')
            blocks=source_notices(p.read_text(encoding='utf-8-sig'))
            require('BSD' in '\n'.join(blocks), 'Unreviewed LLGL license branch: '+relative)
            notices.update(blocks); row.update(component='llgl', selected_license='BSD-3-Clause', dependency_commit=read(ROOT/APPROVAL)['upstream_commit'])
        elif p.is_relative_to(headers):
            require(p.relative_to(headers).as_posix() in tracked[headers], 'Untracked Vulkan compiler input')
            text=p.read_text(encoding='utf-8-sig'); licenses=re.findall(r'SPDX-License-Identifier:\s*([^\r\n*]+)',text)
            require(licenses and all(x.strip() in ('Apache-2.0','MIT','Apache-2.0 OR MIT','MIT OR Apache-2.0') for x in licenses), 'Unreviewed Vulkan header license')
            notices.update(source_notices(text)); row.update(component='vulkan_headers', selected_license='MIT' if any('MIT' in x for x in licenses) else 'Apache-2.0', dependency_commit=q.HEADERS)
        else:
            require(any(p.is_relative_to(pathlib.Path(base)) for base in ('/usr/include','/usr/lib','/lib')), 'Unreviewed GNU input: '+str(p))
            row.update(component='system', redistributed=False)
        rows.append(row)
    result=dict(files=rows, depfiles=depfiles, compile_commands=commands, notice_blocks=sorted(notices),
                actual_verbose_commands_verified=build_stdout is not None,
                scope='Actual retained GNU compiler dependencies; runtime shared objects recorded separately; no symbol reachability claim')
    validate_audit(result); return result


def safe(folder, name):
    p=pathlib.PurePosixPath(name)
    require(not p.is_absolute() and '..' not in p.parts and '\\' not in name and ':' not in name, 'Unsafe archive path')
    path=(folder/name).resolve(); require(path.is_relative_to(folder.resolve()) and path.is_file(), 'Archive path escaped')
    return path


def check(folder, live=False):
    r=read(folder/'receipt.json')
    exact(r, dict(schema_version=1, task='P3-LBEN-LLGL-readiness', status='completed', inputs_unchanged=True,
        profile_id=PROFILE, unmodified_upstream=False, minimal_buffer_reference_accepted=True,
        physical_GPU_accepted=False, workload_correctness_accepted=False, performance_accepted=False, performance_samples=0))
    q.check(folder/'qualification')
    require(sha(folder/'qualification/receipt.json')==r['qualification_sha256']
            and same(read(folder/'qualification/receipt.json'),r['qualification']), 'Qualification changed')
    exact(r['qualification'],dict(mode='software-diagnostic',physical_probe_qualified=False,P3_BEN_07_completed=False))
    require(r['source_commit']==r['qualification']['source_commit'], 'Qualification revision differs')
    require(sha(folder/'source.zip')==r['source_zip_sha256'], 'Executed source archive changed')
    with zipfile.ZipFile(folder/'source.zip') as z:
        require(len(z.namelist())==len(set(z.namelist()))==len(r['source_inputs'])
                and set(z.namelist())=={x['path'] for x in r['source_inputs']}, 'Source archive inventory changed')
        for item in r['source_inputs']: require(hashlib.sha256(z.read(item['path'])).hexdigest()==item['sha256'], 'Source archive byte drift')
        approval=json.loads(z.read(APPROVAL)); exact(approval,dict(profile_id=PROFILE,upstream_commit=r['LLGL_commit'],added_lines=10,unmodified_upstream=False))
        original=z.read('patch/original.cpp'); changed=z.read('patch/overlay.cpp')
        require(original==(ROOT/APPROVED_ORIGINAL).read_bytes() and changed==(ROOT/APPROVED_OVERLAY).read_bytes()
                and patch(original.decode()).encode()==changed and len(changed.splitlines())-len(original.splitlines())==10
                and hashlib.sha256(changed).hexdigest()==r['overlay_sha256'], 'Approved ten-line overlay changed')
        sources={x['path']:x['sha256'] for x in r['source_inputs']}
        require(fingerprint(sources,r['overlay_sha256'],r['LLGL_commit'])==r['probe_fingerprint'], 'Probe fingerprint differs')
    require(sha(folder/'inputs.json')==r['inputs_sha256'], 'GNU input audit changed')
    inputs=read(folder/'inputs.json')
    validate_audit(inputs)
    require(inputs['actual_verbose_commands_verified'] is True, 'Actual compiler command log missing')
    require((folder/'settings/vk_layer_settings.txt').read_bytes()==(folder/'qualification/settings/vk_layer_settings.txt').read_bytes(), 'Required sync settings changed')
    expected=jobs(); require(len(r['runs'])==len(expected), 'Missing/extra probe process'); last=0; verified=0
    for row,job in zip(r['runs'],expected):
        env=environment(job,r)
        require(same(row['job'],job) and row['argv']==arguments(job,r) and row['overrides'].keys()==env.keys()
                and all(row['overrides'][k].replace('\\','/')==v.replace('\\','/') for k,v in env.items()), 'Command/environment/order differs')
        require(type(row['pid']) is int and row['pid']>0 and last<=row['started']<=row['finished'], 'Process/serialization differs'); last=row['finished']
        stdout,stderr=(safe(folder,row[k]).read_bytes() for k in ('stdout','stderr'))
        require(hashlib.sha256(stdout).hexdigest()==row['stdout_sha256'] and hashlib.sha256(stderr).hexdigest()==row['stderr_sha256'], 'Raw probe output changed')
        if job['kind'] in ('normal','callback-control'):
            require(type(row['returncode']) is int and row['returncode']==0, 'Native probe failed')
            value=validate_result(json.loads(stdout),job,r); require(same(value,row['result']), 'Raw/parsed probe mismatch')
            if job['kind']=='normal': require(not re.search(rb'Validation Error|SYNC-HAZARD',stderr), 'Normal validation error')
            else: require(VUID.encode() in stderr, 'Actual callback diagnostic not logged')
            runtime=row['runtime_files']; require({x['path'] for x in runtime}==set(value['runtime_modules']), 'Runtime files missing')
            require(all(x in r['runtime_inputs'] for x in runtime), 'Runtime file binding changed')
            verified+=value['verified_bytes']
        else: rejected(job,row['returncode'],stdout,stderr)
    require(verified==24576 and type(r['verified_bytes']) is int and r['verified_bytes']==verified, 'Minimal full-reference byte total differs')
    if live:
        q.check(pathlib.Path(r['qualification_evidence']),live=True)
        require(q.unchanged(r['frozen_inputs']+r['runtime_inputs']), 'Source/build/runtime changed')
        q.pin(ROOT/'.deps/LLGL',r['LLGL_commit']); q.pin(ROOT/'.deps/Vulkan-Headers',q.HEADERS)
    return dict(processes=8,normal_probes=4,actual_callback_controls=1,rejection_controls=3,
                verified_bytes=verified,performance_samples=0,physical_GPU_accepted=False,workload_correctness_accepted=False)


def run(a):
    require(platform.system()=='Linux','Actual Linux host required'); qualification=a.qualification.resolve()
    q.check(qualification,live=True); prior=read(qualification/'receipt.json')
    exact(prior,dict(mode='software-diagnostic',physical_probe_qualified=False,P3_BEN_07_completed=False))
    require(q.git(ROOT,'rev-parse','HEAD')==prior['source_commit'] and not q.git(ROOT,'status','--porcelain','--untracked-files=no'), 'Clean same-revision qualification required')
    pin=read(ROOT/APPROVAL)['upstream_commit']; q.pin(ROOT/'.deps/LLGL',pin); q.pin(ROOT/'.deps/Vulkan-Headers',q.HEADERS)
    out=a.output.resolve(); build=out.with_name(out.name+'-build')
    require(out.is_relative_to(ROOT/'out') and not out.exists() and not build.exists(), 'Fresh output/build roots required')
    out.mkdir(parents=True); build.mkdir(); shutil.copytree(qualification,out/'qualification')
    for name in ('commands','empty','settings','xdg','patch','licenses'): (out/name).mkdir(mode=0o700)
    shutil.copyfile(qualification/'settings/vk_layer_settings.txt',out/'settings/vk_layer_settings.txt')
    original=subprocess.check_output([*q.GIT,'-C',str(ROOT/'.deps/LLGL'),'show',pin+':'+RELATIVE]); changed=patch(original.decode()).encode()
    require(original==(ROOT/APPROVED_ORIGINAL).read_bytes() and changed==(ROOT/APPROVED_OVERLAY).read_bytes(), 'Exact approved upstream/overlay bytes required')
    (out/'patch/original.cpp').write_bytes(original); (out/'patch/overlay.cpp').write_bytes(changed)
    for source,dest in ((ROOT/'LICENSE',out/'licenses/GraphicsSDK-MIT.txt'),(ROOT/'.deps/LLGL/LICENSE.txt',out/'licenses/LLGL-BSD-3-Clause.txt')): shutil.copyfile(source,dest)
    own=set(ROOT/p for p in (*OWN,APPROVAL,APPROVED_ORIGINAL,APPROVED_OVERLAY,'tools/linux_llgl_probe.py','tools/prepare_llgl_transfer_src_candidate.py','tools/audit_linux_package.py','tools/audit_licenses.py','tools/linux_comparison.py','tools/linux-vulkan-validation.json','tools/vulkan-validation.lock.json','LICENSE'))
    sources=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(own)]
    sources += [dict(path='patch/'+name,sha256=sha(out/'patch'/name)) for name in ('original.cpp','overlay.cpp')]
    with zipfile.ZipFile(out/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(own): z.write(p,p.relative_to(ROOT).as_posix())
        for name in ('original.cpp','overlay.cpp'): z.write(out/'patch'/name,'patch/'+name)
    base={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_','LD_')) and k not in ('DISPLAY','WAYLAND_DISPLAY')}
    commands=[]
    def call(name,argv,env=None):
        start=time.monotonic(); child=subprocess.Popen(list(map(str,argv)),cwd=ROOT,env={**base,**(env or {})},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try: stdout,stderr=child.communicate(timeout=900)
        except subprocess.TimeoutExpired: child.kill(); stdout,stderr=child.communicate(); (out/'commands'/(name+'.stdout')).write_bytes(stdout); (out/'commands'/(name+'.stderr')).write_bytes(stderr); raise
        row=dict(argv=list(map(str,argv)),overrides=env or {},pid=child.pid,started=start,finished=time.monotonic(),returncode=child.returncode)
        for key,data in (('stdout',stdout),('stderr',stderr)):
            p=out/'commands'/(name+'.'+key); p.write_bytes(data); row[key]=p.relative_to(out).as_posix(); row[key+'_sha256']=sha(p)
        commands.append(dict(name=name,**row)); save(out/'commands.json',commands); return row,stdout,stderr
    loader=prior['loader_path']; overlay=out/'patch/overlay.cpp'
    for name,argv in [('configure',['cmake','-S',ROOT/'benchmarks/linux_llgl_probe','-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER=gcc-14','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DGFX_LLGL_OVERLAY='+str(overlay),'-DVulkan_INCLUDE_DIR='+str(ROOT/'.deps/Vulkan-Headers/include'),'-DVulkan_LIBRARY='+loader]),('build',['cmake','--build',build,'--target','linux_llgl_probe','--parallel','2','--verbose','--','-d','keepdepfile'])]:
        row,stdout,stderr=call(name,argv)
        if row['returncode']: print((stdout+stderr).decode(errors='replace')[-12000:],flush=True)
        require(row['returncode']==0,name+' failed; raw logs retained'); print(name+' PASS',flush=True)
    inputs=audit(build,overlay,call,stdout); save(out/'inputs.json',inputs)
    binaries={n:record(build/p) for n,p in [('probe','linux_llgl_probe'),('core','lib/libLLGL.so'),('backend','lib/libLLGL_Vulkan.so')]}
    r=dict(schema_version=1,task='P3-LBEN-LLGL-readiness',status='running',source_commit=prior['source_commit'],qualification=prior,
        qualification_evidence=str(qualification),qualification_sha256=sha(out/'qualification/receipt.json'),output_directory=str(out),LLGL_commit=pin,
        profile_id=PROFILE,unmodified_upstream=False,source_inputs=sources,source_zip_sha256=sha(out/'source.zip'),overlay_sha256=sha(overlay),
        probe_fingerprint=fingerprint({x['path']:x['sha256'] for x in sources},sha(overlay),pin),binaries=binaries,inputs_sha256=sha(out/'inputs.json'),
        minimal_buffer_reference_accepted=False,physical_GPU_accepted=False,workload_correctness_accepted=False,performance_accepted=False,performance_samples=0,
        verified_bytes=0,runtime_inputs=[],runs=[])
    r['frozen_inputs']=prior['frozen_inputs']+[record(p) for p in sorted(own)]+[
        {k:item[k] for k in ('path','bytes','sha256')} for item in inputs['files']+inputs['depfiles']]+list(binaries.values())+[
        record(overlay),record(out/'patch/original.cpp'),record(out/'source.zip'),record(out/'inputs.json'),record(out/'settings/vk_layer_settings.txt')]
    r['frozen_inputs'] += [record(build/n) for n in ('CMakeCache.txt','build.ninja','compile_commands.json','probe-identity.txt')]
    require((build/'probe-identity.txt').read_text().startswith('linux-llgl|'+pin+'|'+PROFILE+'\n'), 'CMake profile identity differs')
    require(r['probe_fingerprint'].encode() in (build/'linux_llgl_probe').read_bytes(), 'Compiled probe fingerprint differs')
    save(out/'receipt.json',r)
    try:
        for job in jobs():
            require(q.unchanged(r['frozen_inputs']+r['runtime_inputs']), 'Frozen inputs changed')
            row,stdout,stderr=call(job['name'],arguments(job,r),environment(job,r)); row['job']=job; r['runs'].append(row); save(out/'receipt.json',r)
            if job['kind'] in ('normal','callback-control'):
                require(row['returncode']==0,'Native LLGL probe failed; raw logs retained'); value=validate_result(json.loads(stdout),job,r); row['result']=value
                row['runtime_files']=[record(pathlib.Path(p)) for p in value['runtime_modules']]
                for item in row['runtime_files']:
                    matches=[x for x in r['runtime_inputs'] if x['path']==item['path']]
                    require(not matches or matches==[item], 'Actual runtime file changed')
                    if not matches: r['runtime_inputs'].append(item)
                r['verified_bytes']+=value['verified_bytes']
            else: rejected(job,row['returncode'],stdout,stderr)
            save(out/'receipt.json',r); print(job['name']+' PASS',flush=True)
        require(q.unchanged(r['frozen_inputs']+r['runtime_inputs']), 'Final source/runtime changed')
        r.update(status='completed',inputs_unchanged=True,minimal_buffer_reference_accepted=True); save(out/'receipt.json',r)
        print(json.dumps(check(out,live=True)),flush=True)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as exc:
        r.update(status='failed',error=str(exc)); save(out/'receipt.json',r); raise


def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('stage',choices=('run','check'))
    parser.add_argument('--qualification',type=pathlib.Path); parser.add_argument('--output',type=pathlib.Path,required=True); parser.add_argument('--live',action='store_true'); a=parser.parse_args()
    if a.stage=='check': print(json.dumps(check(a.output,a.live),indent=2)); return
    require(a.qualification is not None,'Same-revision BEN-07 software qualification required'); run(a)


if __name__=='__main__': main()
