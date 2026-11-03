"""Survey native Metal devices and offline shader tools; never submit GPU work."""
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

import macos_core as core
from linux_comparison import ROOT, require, read, save, sha, record, unchanged, cache, GIT

OWN=('benchmarks/macos_metal_probe/CMakeLists.txt','benchmarks/macos_metal_probe/main.mm',
    'benchmarks/macos_metal_probe/probe.metal','benchmarks/macos_metal_probe/invalid.metal',
    'tools/macos_metal_probe.py','tools/macos_core.py','.github/workflows/macos-core.yml')
SCOPE=dict(GPU_work_submitted=False,SDK_Metal_backend_accepted=False,physical_GPU_accepted=False,
    performance_accepted=False,P3_LNX_15_completed=False,P4_MTL_01_completed=False,P4_MTL_02_completed=False)


def inventory(value,selected=None):
    core.exact(value,dict(schema_version=1,contract='native_macos_metal_inventory_v1',GPU_work_submitted=False,SDK_backend_used=False,selected_registry_id=selected))
    rows=value['devices'];require(type(rows) is list,'Invalid Metal device list');ids=[]
    for row in rows:
        identity=row['registry_id'];require(type(identity) is str and re.fullmatch('[0-9a-f]{16}',identity) and int(identity,16)>0,'Invalid Metal registry identity')
        require(type(row['name']) is str and row['name'],'Missing Metal device name')
        require(all(type(row[k]) is bool for k in ('low_power','removable','headless','unified_memory')),'Invalid Metal capability booleans')
        require(type(row['max_buffer_length']) is int and row['max_buffer_length']>0,'Invalid Metal buffer limit')
        dimensions=row['max_threads_per_threadgroup'];require(type(dimensions) is list and len(dimensions)==3 and all(type(n) is int and n>0 for n in dimensions),'Invalid Metal thread limits')
        ids.append(identity)
    require(ids==sorted(set(ids)),'Duplicate or unordered Metal registry identities')
    primary=value['default_registry_id'];require(primary is None or type(primary) is str and primary in ids,'Default Metal device absent from inventory')
    require(selected is None or selected in ids,'Requested Metal identity absent from inventory')
    modules=value['framework_modules'];require(type(modules) is list and modules==sorted(set(modules)) and all(type(s) is str and s.startswith('/System/Library/Frameworks/') for s in modules),'Invalid framework observations')
    require(all(any('/'+name+'.framework/' in path and pathlib.PurePosixPath(path).name==name for path in modules) for name in ('Metal','Foundation')),'Actual Metal/Foundation frameworks not observed')
    return ids


def unavailable(stderr):
    return any(token in stderr.lower() for token in (b'unable to find utility',b'missing metal toolchain',b'metal toolchain is not installed'))


def rejection(code,stdout,stderr,token):
    require(type(code) is int and code==1 and not stdout and token in stderr,'Unexpected Metal selection/control failure')


def check(folder,python=None,live=False):
    r=read(folder/'receipt.json');core.exact(r,dict(schema_version=1,scope='macos-metal-availability-v1',status='completed',inputs_unchanged=True,**SCOPE))
    base=read(folder/'core/receipt.json');core.check(folder/'core')
    core.exact(r,dict(source_commit=base['source_commit'],architecture=base['architecture'],SDK_source_fingerprint=base['SDK_source_fingerprint']))
    files={p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file() and p!=folder/'receipt.json'}
    require(files=={x['path'] for x in r['files']},'Probe evidence inventory differs')
    for row in r['files']:
        file=folder/row['path'];require(file.resolve().is_relative_to(folder.resolve()) and file.stat().st_size==row['bytes'] and sha(file)==row['sha256'],'Probe evidence bytes differ')
    with zipfile.ZipFile(folder/'source.zip') as z:
        require(set(z.namelist())==set(OWN) and len(z.namelist())==len(OWN),'Probe source inventory differs')
        require(len(r['source_inputs'])==len(OWN) and {x['path'] for x in r['source_inputs']}==set(OWN),'Probe source binding differs')
        require(all(hashlib.sha256(z.read(row['path'])).hexdigest()==row['sha256'] for row in r['source_inputs']),'Probe source bytes differ')
    commands=read(folder/'commands.json');require(commands==r['commands'],'Probe command receipt differs');last=0
    for command in commands:
        require(type(command['pid']) is int and command['pid']>0 and all(type(command[k]) in (int,float) and math.isfinite(command[k]) for k in ('started','finished')) and last<=command['started']<=command['finished'],'Nonserial Metal probe command');last=command['finished']
    by_name={x['name']:x for x in commands};require(len(by_name)==len(commands),'Duplicate probe commands')
    for name in ('configure','build','architecture','inventory','missing-device','invalid-id','find-metal','find-metallib'):require(name in by_name,'Missing Metal probe gate')
    def output(name,key):return (folder/'commands'/(name+'.'+key)).read_bytes()
    for name in ('configure','build','architecture','inventory'):require(by_name[name]['returncode']==0,'Native probe build/query failed')
    require(output('architecture','stdout').decode().strip()==r['architecture'],'Native Metal probe architecture differs')
    value=json.loads(output('inventory','stdout'));require(value==r['inventory'],'Parsed/raw Metal inventory differs');ids=inventory(value)
    core.exact(r,dict(device_count=len(ids),native_Metal_device_available=bool(ids),device_selection_verified=bool(ids)))
    rejection(by_name['missing-device']['returncode'],output('missing-device','stdout'),output('missing-device','stderr'),b'Requested Metal registry ID absent or ambiguous')
    rejection(by_name['invalid-id']['returncode'],output('invalid-id','stdout'),output('invalid-id','stderr'),b'Invalid registry ID')
    for index,identity in enumerate(ids):
        name='select-'+str(index);require(by_name[name]['returncode']==0,'Exact Metal selection failed')
        selected=json.loads(output(name,'stdout'));inventory(selected,identity)
        require(selected['devices']==value['devices'] and selected['default_registry_id']==value['default_registry_id'],'Metal inventory changed between selectors')
    c=cache(folder/'cache.txt');require(c['CMAKE_OSX_ARCHITECTURES']==r['architecture'] and c['CMAKE_OSX_SYSROOT']==base['sdk_path'] and c['CMAKE_BUILD_TYPE']=='Release','Probe target differs from Core')
    graph=read(folder/'compile_commands.json');require(len(graph)==1 and graph[0]['file']==base['source_directory']+'/benchmarks/macos_metal_probe/main.mm','Unexpected Metal compiler units')
    args=shlex.split(graph[0]['command']);require('-fobjc-arc' in args and '-O2' in args and '-DNDEBUG' in args and args[args.index('-arch')+1]==r['architecture'] and args[args.index('-isysroot')+1]==base['sdk_path'],'Probe compiler flags differ')
    found=all(by_name['find-'+name]['returncode']==0 and output('find-'+name,'stdout').strip() for name in ('metal','metallib'))
    core.exact(r,dict(shader_tools_found=bool(found)))
    if r['offline_shader_status']=='compiled':
        require(found and all(by_name[n]['returncode']==0 for n in ('shader-compile','shader-library')),'Offline shader compiler failed')
        failed=by_name['invalid-shader'];require(failed['returncode']!=0 and b'error:' in output('invalid-shader','stderr').lower(),'Invalid shader did not reach a compiler diagnostic')
        require(set(r['shader_artifacts'])=={'air','metallib'} and all(type(x['bytes']) is int and x['bytes']>0 and re.fullmatch('[0-9a-f]{64}',x['sha256']) for x in r['shader_artifacts'].values()),'Shader artifact proof absent')
        require(r['offline_shader_compile_verified'] is True,'Compiled shader marked unverified')
    else:
        require(r['offline_shader_status']=='unavailable' and r['offline_shader_compile_verified'] is False and r['shader_artifacts']=={},'Shader failure promoted to availability')
        failures=[name for name in ('find-metal','find-metallib','shader-compile','shader-library') if name in by_name and by_name[name]['returncode']!=0]
        require(failures and all(unavailable(output(name,'stderr')) for name in failures),'Unrelated shader failure accepted as unavailable')
    if python:
        core.validate_python(python,base);p=read(python/'receipt.json');tested={x['path']:x['sha256'] for x in p['inputs']}
        require(tested['tools/macos_metal_probe.py']==next(x['sha256'] for x in r['source_inputs'] if x['path']=='tools/macos_metal_probe.py'),'Tested/executed Metal probe runner differs')
    if live:require(unchanged(r['frozen_inputs']),'Metal source/build inputs changed')
    return dict(architecture=r['architecture'],device_count=len(ids),native_Metal_device_available=bool(ids),
        offline_shader_status=r['offline_shader_status'],offline_shader_compile_verified=r['offline_shader_compile_verified'],**SCOPE)


def run(a):
    require(platform.system()=='Darwin','Actual macOS required');core.check(a.core,live=True);base=read(a.core/'receipt.json')
    require(platform.machine()==base['architecture'],'Core/probe architecture differs')
    git=lambda *args:subprocess.check_output([*GIT,*args],cwd=ROOT,text=True).strip()
    require(git('rev-parse','HEAD')==base['source_commit'] and not git('status','--porcelain','--untracked-files=no'),'Clean same-revision Core/probe required')
    out=a.output.resolve();build=out.with_name(out.name+'-build');require(out.is_relative_to(ROOT/'out') and not out.exists() and not build.exists(),'Fresh probe/build outputs required')
    out.mkdir(parents=True);(out/'commands').mkdir();shutil.copytree(a.core,out/'core')
    frozen=base['frozen_inputs']+[record(ROOT/n) for n in OWN];source_inputs=[dict(path=n,sha256=sha(ROOT/n)) for n in OWN]
    env={k:v for k,v in os.environ.items() if k not in ('CC','CXX','OBJCXX','CFLAGS','CXXFLAGS','OBJCXXFLAGS','CPPFLAGS','LDFLAGS','SDKROOT','CPATH','LIBRARY_PATH') and not k.startswith(('DYLD_','LD_','GFX_'))};commands=[]
    def call(name,args,required=True):
        started=time.monotonic();child=subprocess.Popen(list(map(str,args)),cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:stdout,stderr=child.communicate(timeout=300)
        except subprocess.TimeoutExpired:child.kill();child.communicate();raise
        for key,data in (('stdout',stdout),('stderr',stderr)):(out/'commands'/(name+'.'+key)).write_bytes(data)
        row=dict(name=name,argv=list(map(str,args)),pid=child.pid,started=started,finished=time.monotonic(),returncode=child.returncode);commands.append(row);save(out/'commands.json',commands)
        require(not required or child.returncode==0,name+' failed: '+(stdout+stderr).decode(errors='replace')[-5000:]);return row,stdout,stderr
    compiler=(a.core/'commands/compiler-path.stdout').read_text().strip()
    call('configure',['cmake','-S',ROOT/'benchmarks/macos_metal_probe','-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_OBJCXX_COMPILER='+compiler,
        '-DCMAKE_OSX_ARCHITECTURES='+base['architecture'],'-DCMAKE_OSX_SYSROOT='+base['sdk_path'],'-DCMAKE_OBJCXX_FLAGS_RELEASE=-O2 -DNDEBUG',
        '-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF','-DCMAKE_UNITY_BUILD=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
    call('build',['cmake','--build',build,'--verbose']);binary=build/'metal_availability';call('architecture',['lipo','-archs',binary])
    _,stdout,_=call('inventory',[binary]);value=json.loads(stdout);ids=inventory(value)
    call('missing-device',[binary,'--require-device','0'*16],False);call('invalid-id',[binary,'--require-device','invalid'],False)
    for index,identity in enumerate(ids):call('select-'+str(index),[binary,'--require-device',identity])
    tools={name:call('find-'+name,['xcrun','--sdk','macosx','--find',name],False)[0]['returncode']==0 for name in ('metal','metallib')}
    compiled=False;artifacts={}
    if all(tools.values()):
        row,_,stderr=call('shader-compile',['xcrun','--sdk','macosx','metal','-c',ROOT/'benchmarks/macos_metal_probe/probe.metal','-o',build/'probe.air'],False)
        if row['returncode']==0:
            row,_,stderr=call('shader-library',['xcrun','--sdk','macosx','metallib',build/'probe.air','-o',build/'probe.metallib'],False)
            if row['returncode']==0:
                call('invalid-shader',['xcrun','--sdk','macosx','metal','-c',ROOT/'benchmarks/macos_metal_probe/invalid.metal','-o',build/'invalid.air'],False)
                require(not (build/'invalid.air').exists(),'Invalid shader emitted a binary')
                artifacts={name:record(build/file) for name,file in (('air','probe.air'),('metallib','probe.metallib'))};compiled=True
        if not compiled:require(unavailable(stderr),'Unrelated offline Metal compiler failure')
    require(unchanged(frozen),'Probe source changed')
    with zipfile.ZipFile(out/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for n in OWN:z.write(ROOT/n,n)
    for src,dst in (('CMakeCache.txt','cache.txt'),('compile_commands.json','compile_commands.json')):shutil.copyfile(build/src,out/dst)
    frozen += [record(binary),record(build/'CMakeCache.txt'),record(build/'compile_commands.json')]+list(artifacts.values())
    r=dict(schema_version=1,scope='macos-metal-availability-v1',status='completed',inputs_unchanged=True,source_commit=base['source_commit'],
        architecture=base['architecture'],SDK_source_fingerprint=base['SDK_source_fingerprint'],inventory=value,device_count=len(ids),native_Metal_device_available=bool(ids),
        device_selection_verified=bool(ids),shader_tools_found=all(tools.values()),offline_shader_status='compiled' if compiled else 'unavailable',offline_shader_compile_verified=compiled,
        shader_artifacts=artifacts,source_inputs=source_inputs,frozen_inputs=frozen,commands=commands,
        files=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()],**SCOPE)
    save(out/'receipt.json',r);print(json.dumps(check(out,live=True),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',choices=('run','check'));parser.add_argument('--core',type=pathlib.Path)
    parser.add_argument('--output',type=pathlib.Path,required=True);parser.add_argument('--python',type=pathlib.Path);parser.add_argument('--live',action='store_true');a=parser.parse_args()
    if a.stage=='run':require(a.core is not None,'Explicit Core evidence required');run(a)
    else:print(json.dumps(check(a.output,a.python,a.live),indent=2))
