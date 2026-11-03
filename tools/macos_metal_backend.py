"""Native Metal SDK submission and installed-consumer evidence, separate from GPU acceptance."""
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
import xml.etree.ElementTree as ET
import zipfile

import macos_core as core
import macos_metal_probe as probe
from linux_comparison import ROOT, GIT, require, read, save, sha, record, unchanged, cache
from audit_licenses import sdk_fingerprint

OWN=('tools/macos_metal_backend.py','tools/audit_licenses.py','tools/linux_comparison.py',
     'src/backends/metal/Metal.cpp','tests/MetalSubmissionTests.cpp','tests/macos_metal_consumer/CMakeLists.txt')
UNITS=(*core.CORE,'src/backends/metal/Metal.cpp',*(f'tests/{name}.cpp' for name in core.TESTS.values()),'tests/MetalSubmissionTests.cpp')
SCOPE=dict(SDK_Metal_submission_executed=True,resource_or_draw_executed=False,native_error_injected=False,
    native_validation_accepted=False,physical_GPU_accepted=False,performance_accepted=False,
    P3_LNX_15_completed=False,P4_MTL_01_completed=False,P4_MTL_02_completed=False)


def result(value,fp,ids):
    core.exact(value,dict(schema_version=1,contract='metal_submission_v1',SDK_source_fingerprint=fp,
        registry_ids=ids,completed_submissions=66,retired_pending_submissions=1,
        **{k:v for k,v in SCOPE.items() if not k.startswith('P')}))
    require(value['default_registry_id'] in ids,'Default Metal adapter absent from observed inventory')
    require(type(value['zero_poll_timed_out']) is bool,'Invalid zero poll outcome')
    require(type(value['assertions']) is int and value['assertions']>=100,'Native contract assertions missing')


def junit(data):
    root=ET.fromstring(data);rows=list(root.iter('testcase'))
    names=[r.get('name') for r in rows]
    require(len(rows)==len(core.TESTS)+1 and set(names)==set(core.TESTS)|{'metal_submission_contracts'},'Missing/extra Metal/CPU CTests')
    for row in rows:
        duration=float(row.get('time','nan'))
        require(row.get('status')=='run' and math.isfinite(duration) and duration>=0 and not any(row.find(n) is not None for n in ('failure','error','skipped')),'Failed/skipped Metal/CPU CTest')


def graph(rows,source,arch,sdk):
    require(len(rows)==len(UNITS) and {r['file'] for r in rows}=={source+'/'+n for n in UNITS},'Metal build compiler graph differs')
    for row in rows:
        args=shlex.split(row['command']);backend=row['file'].startswith(source+'/src/')
        require('-arch' in args and args[args.index('-arch')+1]==arch and '-isysroot' in args and args[args.index('-isysroot')+1]==sdk,'Metal build architecture/SDK differs')
        require('-O2' in args and '-DNDEBUG' in args and '-std=c++23' in args,'Metal build optimization/C++23 differs')
        require(('-DGFX_HAS_METAL=1' in args)==backend and not any(v.startswith(('-DGFX_HAS_VULKAN','-DGFX_HAS_D3D12','-flto')) or v in ('-O0','-O1','-O3','-Os','-Ofast') for v in args),'Metal backend/optimization flags differ')
        if row['file'].endswith('/src/backends/metal/Metal.cpp'):
            require('-fobjc-arc' in args and '-x' in args and args[args.index('-x')+1]=='objective-c++','Private Metal unit is not ARC Objective-C++')


def check(folder,python=None,live=False):
    r=read(folder/'receipt.json')
    core.exact(r,dict(schema_version=1,scope='macos-metal-submission-v1',status='completed',inputs_unchanged=True,**SCOPE))
    probe.check(folder/'availability')
    base=read(folder/'availability/core/receipt.json')
    core.exact(r,dict(source_commit=base['source_commit'],architecture=base['architecture'],SDK_source_fingerprint=base['SDK_source_fingerprint']))
    actual={p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file() and p!=folder/'receipt.json'}
    require(len(r['files'])==len(actual) and actual=={x['path'] for x in r['files']},'Metal evidence inventory differs')
    for item in r['files']:
        p=folder/item['path'];require(p.resolve().is_relative_to(folder.resolve()) and p.stat().st_size==item['bytes'] and sha(p)==item['sha256'],'Metal evidence byte drift')
    with zipfile.ZipFile(folder/'source.zip') as z:
        require(len(z.namelist())==len(OWN) and set(z.namelist())==set(OWN),'Metal source snapshot differs')
        require(len(r['source_inputs'])==len(OWN) and {p['path'] for p in r['source_inputs']}==set(OWN),'Metal frozen source inventory differs')
        for item in r['source_inputs']:require(hashlib.sha256(z.read(item['path'])).hexdigest()==item['sha256'],'Metal source byte drift')
        with zipfile.ZipFile(folder/'availability/core/source.zip') as c:
            for name in ('src/backends/metal/Metal.cpp','tests/MetalSubmissionTests.cpp','tests/macos_metal_consumer/CMakeLists.txt'):
                require(z.read(name)==c.read(name),'Core/backend source snapshots differ')
    ids=probe.inventory(read(folder/'availability/receipt.json')['inventory'])
    require(ids,'No native Metal device; cannot accept submission evidence')
    for name in ('native','consumer'):
        value=read(folder/(name+'.json'));result(value,r['SDK_source_fingerprint'],ids)
        require(value==json.loads((folder/'commands'/(name+'.stdout')).read_text()),'Raw/parsed Metal output differs')
    require(read(folder/'native.json')['default_registry_id']==read(folder/'consumer.json')['default_registry_id'],'Installed/native default selection changed')
    junit((folder/'tests.xml').read_bytes())
    graph(read(folder/'sdk-commands.json'),base['source_directory'],r['architecture'],base['sdk_path'])
    c=cache(folder/'sdk-cache.txt')
    require(c['GFX_ENABLE_METAL']=='ON' and all(c[k]=='OFF' for k in ('GFX_ENABLE_D3D12','GFX_ENABLE_VULKAN','GFX_BUILD_EXAMPLES')) and c['GFX_BUILD_TESTS']=='ON','Metal test build policy differs')
    for prefix in ('sdk','consumer'):
        c=cache(folder/(prefix+'-cache.txt'))
        core.exact(c,dict(CMAKE_BUILD_TYPE='Release',CMAKE_OSX_ARCHITECTURES=r['architecture'],CMAKE_OSX_SYSROOT=base['sdk_path'],CMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG',CMAKE_INTERPROCEDURAL_OPTIMIZATION='OFF',CMAKE_UNITY_BUILD='OFF'))
    c=cache(folder/'consumer-cache.txt');require(c['GraphicsSDK_DIR']==r['install_directory']+'/lib/cmake/GraphicsSDK','Metal consumer escaped fresh package')
    rows=read(folder/'consumer-commands.json')
    core.compile_graph(rows,[base['source_directory']+'/tests/MetalSubmissionTests.cpp'],r['architecture'],base['sdk_path'])
    require(r['install_directory']+'/include' in shlex.split(rows[0]['command']) and base['source_directory']+'/include' not in rows[0]['command'],'Metal consumer did not use installed public headers')
    require(r['installed_headers']==base['installed_headers'],'Metal installed headers differ from same-source Core')
    commands=read(folder/'commands.json');require(commands==r['commands'],'Metal command receipt drift')
    require([c['name'] for c in commands]==['configure','build','list-tests','ctest','native','install','library-arch','consumer-configure','consumer-build','consumer-arch','consumer'],'Missing/extra Metal command processes')
    last=0
    for command in commands:
        require(command['returncode']==0 and type(command['pid']) is int and command['pid']>0 and all(type(command[k]) in (int,float) and math.isfinite(command[k]) for k in ('started','finished')) and last<=command['started']<=command['finished'],'Failed/overlapping Metal commands')
        last=command['finished']
    for name in ('library-arch','consumer-arch'):require((folder/'commands'/(name+'.stdout')).read_text().strip()==r['architecture'],'Native Metal artifact architecture differs')
    if live:require(unchanged(r['frozen_inputs']) and sdk_fingerprint(ROOT)==r['SDK_source_fingerprint'],'Metal build/source/install input changed')
    count=None
    if python:
        count=core.validate_python(python,base)
        tested={x['path']:x['sha256'] for x in read(python/'receipt.json')['inputs']}
        require(tested['tools/macos_metal_backend.py']==next(x['sha256'] for x in r['source_inputs'] if x['path']=='tools/macos_metal_backend.py'),'Executed/tested Metal runner differs')
    return dict(architecture=r['architecture'],registry_ids=ids,CPU_CTests=11,Metal_CTests=1,native_and_installed_submissions=201,
        installed_consumer_passed=True,Python_contracts=count,**SCOPE)


def run(a):
    require(platform.system()=='Darwin','Native macOS required for Metal submission')
    probe.check(a.availability,live=True)
    base=read(a.availability/'core/receipt.json')
    require(platform.machine()==base['architecture'],'Metal/Core native architecture differs')
    require(probe.inventory(read(a.availability/'receipt.json')['inventory']),'Metal device required for submission; no skip')
    commit=subprocess.check_output([*GIT,'rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    require(commit==base['source_commit'] and not subprocess.check_output([*GIT,'status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True).strip(),'Metal/Core source differs or tracked checkout dirty')
    out=a.output.resolve();require(out.is_relative_to(ROOT/'out') and not out.exists(),'Fresh output under out/ required')
    out.mkdir(parents=True);shutil.copytree(a.availability,out/'availability');(out/'commands').mkdir()
    frozen=[record(ROOT/n) for n in OWN];source_inputs=[dict(path=n,sha256=sha(ROOT/n)) for n in OWN]
    frozen += base['frozen_inputs']
    build=out.with_name(out.name+'-build');consumer=out.with_name(out.name+'-consumer');install=out.with_name(out.name+'-install')
    require(not any(p.exists() for p in (build,consumer,install)),'Fresh Metal build/install roots required')
    env={k:v for k,v in os.environ.items() if k not in ('CC','CXX','OBJCXX','CFLAGS','CXXFLAGS','OBJCXXFLAGS','CPPFLAGS','LDFLAGS','SDKROOT','CPATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','CMAKE_PREFIX_PATH') and not k.startswith(('DYLD_','LD_','GFX_','VK_','MTL_','METAL_'))}
    commands=[]
    def call(name,argv):
        started=time.monotonic();child=subprocess.Popen(list(map(str,argv)),cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:stdout,stderr=child.communicate(timeout=600)
        except subprocess.TimeoutExpired:child.kill();child.communicate();raise
        (out/'commands'/(name+'.stdout')).write_bytes(stdout);(out/'commands'/(name+'.stderr')).write_bytes(stderr)
        commands.append(dict(name=name,argv=list(map(str,argv)),pid=child.pid,started=started,finished=time.monotonic(),returncode=child.returncode));save(out/'commands.json',commands)
        require(child.returncode==0,name+' failed: '+(stdout+stderr).decode(errors='replace')[-8000:]);return stdout.decode().strip()
    compiler=(a.availability/'core/commands/compiler-path.stdout').read_text().strip()
    flags=['-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER='+compiler,'-DCMAKE_OSX_ARCHITECTURES='+base['architecture'],'-DCMAKE_OSX_SYSROOT='+base['sdk_path'],
        '-DCMAKE_CXX_FLAGS=','-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG','-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF','-DCMAKE_UNITY_BUILD=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
    call('configure',['cmake','-S',ROOT,'-B',build,*flags,'-DCMAKE_OBJCXX_COMPILER='+compiler,'-DCMAKE_OBJCXX_FLAGS=','-DCMAKE_OBJCXX_FLAGS_RELEASE=-O2 -DNDEBUG',
        '-DCMAKE_INSTALL_PREFIX='+str(install),'-DCMAKE_INSTALL_LIBDIR=lib','-DGFX_ENABLE_METAL=ON','-DGFX_ENABLE_D3D12=OFF','-DGFX_ENABLE_VULKAN=OFF','-DGFX_BUILD_EXAMPLES=OFF','-DGFX_BUILD_TESTS=ON'])
    call('build',['cmake','--build',build,'--parallel','3','--verbose'])
    listing=json.loads(call('list-tests',['ctest','--test-dir',build,'--show-only=json-v1']))
    require(len(listing['tests'])==12 and {t['name'] for t in listing['tests']}==set(core.TESTS)|{'metal_submission_contracts'},'Unexpected Metal/CPU suite')
    call('ctest',['ctest','--test-dir',build,'--output-on-failure','--no-tests=error','--timeout','120','--output-junit',out/'tests.xml'])
    fp=base['SDK_source_fingerprint'];save(out/'native.json',json.loads(call('native',[build/'gfx_metal_tests',fp])))
    call('install',['cmake','--install',build]);call('library-arch',['lipo','-archs',install/'lib/libGraphicsSDKCore.a'])
    call('consumer-configure',['cmake','-S',ROOT/'tests/macos_metal_consumer','-B',consumer,*flags,'-DCMAKE_PREFIX_PATH='+str(install)])
    call('consumer-build',['cmake','--build',consumer,'--verbose']);call('consumer-arch',['lipo','-archs',consumer/'macos_metal_consumer'])
    save(out/'consumer.json',json.loads(call('consumer',[consumer/'macos_metal_consumer',fp])))
    for name,path in (('sdk',build),('consumer',consumer)):
        for source,target in (('CMakeCache.txt','cache.txt'),('compile_commands.json','commands.json')):(out/(name+'-'+target)).write_bytes((path/source).read_bytes())
    headers=[dict(path=p.relative_to(install/'include').as_posix(),sha256=sha(p)) for p in sorted((install/'include').rglob('*.hpp'))]
    frozen += [record(p) for p in (build/'CMakeCache.txt',build/'compile_commands.json',consumer/'CMakeCache.txt',consumer/'compile_commands.json',build/'gfx_metal_tests',install/'lib/libGraphicsSDKCore.a',consumer/'macos_metal_consumer',*(install/'include').rglob('*.hpp'))]
    require(unchanged(frozen) and sdk_fingerprint(ROOT)==fp,'Source/build/install changed during Metal execution')
    with zipfile.ZipFile(out/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for n in OWN:z.write(ROOT/n,n)
    r=dict(schema_version=1,scope='macos-metal-submission-v1',status='completed',source_commit=commit,architecture=base['architecture'],SDK_source_fingerprint=fp,
        github_run_id=os.environ.get('GITHUB_RUN_ID'),install_directory=str(install),inputs_unchanged=True,source_inputs=source_inputs,installed_headers=headers,
        frozen_inputs=frozen,commands=commands,files=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()],**SCOPE)
    save(out/'receipt.json',r);print(json.dumps(check(out,live=True),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',choices=('run','check'));parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--availability',type=pathlib.Path);parser.add_argument('--python',type=pathlib.Path);parser.add_argument('--live',action='store_true');a=parser.parse_args()
    if a.stage=='run':run(a)
    else:print(json.dumps(check(a.output,a.python,a.live),indent=2))
