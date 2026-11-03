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
import macos_metal_textures as textures
import macos_metal_graphics as graphics
from linux_comparison import ROOT, GIT, require, read, save, sha, record, unchanged, cache
from audit_licenses import sdk_fingerprint

OWN=('tools/macos_metal_backend.py','tools/audit_licenses.py','tools/linux_comparison.py',
     'src/backends/metal/Metal.cpp','src/foundation/DeviceGeneration.hpp','tests/MetalSubmissionTests.cpp','tests/macos_metal_consumer/CMakeLists.txt')
UNITS=(*core.CORE,'src/backends/metal/Metal.cpp',*(f'tests/{name}.cpp' for name in core.TESTS.values()),'tests/MetalSubmissionTests.cpp')
OWN_V2=(*OWN,'tests/MetalBufferTests.cpp')
UNITS_V2=(*UNITS,'tests/MetalBufferTests.cpp')
OWN_V3=(*OWN_V2,'tests/MetalTextureTests.cpp','tools/macos_metal_textures.py')
UNITS_V3=(*UNITS_V2,'tests/MetalTextureTests.cpp')
OWN_V4=(*OWN_V3,'src/backends/metal/MetalGraphics.hpp','tests/MetalGraphicsTests.cpp','tests/fixtures/metal/graphics.metal','tools/macos_metal_graphics.py')
UNITS_V4=(*UNITS_V3,'tests/MetalGraphicsTests.cpp')
SIZES=(1,2,3,4,5,7,257,259,4096,65539,1048579)
SCOPE=dict(SDK_Metal_submission_executed=True,resource_or_draw_executed=False,native_error_injected=False,
    native_validation_accepted=False,physical_GPU_accepted=False,performance_accepted=False,
    P3_LNX_15_completed=False,P4_MTL_01_completed=False,P4_MTL_02_completed=False)
SCOPE_V2=dict(SCOPE,resource_or_draw_executed=True,SDK_Metal_buffers_executed=True,internal_tail_dispatch_executed=True,
    draw_executed=False,P4_MTL_03_completed=False)
SCOPE_V3=dict(SCOPE_V2,SDK_Metal_textures_executed=True,clear_load_pass_executed=True,P4_MTL_04_completed=False)
SCOPE_V4=dict(SCOPE_V3,SDK_Metal_graphics_executed=True,draw_executed=True,P4_MTL_05_completed=False)


def result(value,fp,ids):
    core.exact(value,dict(schema_version=1,contract='metal_submission_v1',SDK_source_fingerprint=fp,
        registry_ids=ids,completed_submissions=66,retired_pending_submissions=1,
        **{k:v for k,v in SCOPE.items() if not k.startswith('P')}))
    require(value['default_registry_id'] in ids,'Default Metal adapter absent from observed inventory')
    require(type(value['zero_poll_timed_out']) is bool,'Invalid zero poll outcome')
    require(type(value['assertions']) is int and value['assertions']>=100,'Native contract assertions missing')


def buffer_result(value,fp):
    core.exact(value,dict(schema_version=1,contract='metal_buffer_transfer_v1',SDK_source_fingerprint=fp,
        full_cases=len(SIZES),partial_cases=len(SIZES),verified_bytes=2*sum(SIZES)+19*len(SIZES),
        buffer_transfer_executed=True,internal_tail_dispatch_executed=True,draw_executed=False,
        native_validation_accepted=False,physical_GPU_accepted=False,performance_accepted=False))
    require(type(value['assertions']) is int and value['assertions']>=100,'Native buffer contracts missing')


def payloads(folder):
    expected_names={f'{kind}-{n}.bin' for n in SIZES for kind in ('full','partial')}
    require({p.name for p in folder.iterdir()}==expected_names,'Missing/extra full Metal readback payloads')
    total=0
    for n in SIZES:
        expected=bytes((i*37+n*13+17)%251 for i in range(n))
        for kind,data in (('full',expected),('partial',expected+b'\xd7'*19)):
            actual=(folder/f'{kind}-{n}.bin').read_bytes()
            require(actual==data,'Full Metal payload/partial guard mismatch');total+=len(actual)
    return total


def junit(data,version=1):
    root=ET.fromstring(data);rows=list(root.iter('testcase'))
    names=[r.get('name') for r in rows]
    wanted=set(core.TESTS)|{'metal_submission_contracts'}|({'metal_buffer_contracts'} if version>=2 else set())|({'metal_texture_pass_contracts'} if version>=3 else set())|({'metal_graphics_contracts'} if version==4 else set())
    require(len(rows)==len(wanted) and set(names)==wanted,'Missing/extra Metal/CPU CTests')
    for row in rows:
        duration=float(row.get('time','nan'))
        require(row.get('status')=='run' and math.isfinite(duration) and duration>=0 and not any(row.find(n) is not None for n in ('failure','error','skipped')),'Failed/skipped Metal/CPU CTest')


def graph(rows,source,arch,sdk,version=1):
    units={1:UNITS,2:UNITS_V2,3:UNITS_V3,4:UNITS_V4}[version]
    require(len(rows)==len(units) and {r['file'] for r in rows}=={source+'/'+n for n in units},'Metal build compiler graph differs')
    for row in rows:
        args=shlex.split(row['command']);backend=row['file'].startswith(source+'/src/')
        require('-arch' in args and args[args.index('-arch')+1]==arch and '-isysroot' in args and args[args.index('-isysroot')+1]==sdk,'Metal build architecture/SDK differs')
        require('-O2' in args and '-DNDEBUG' in args and ('-std=c++23' in args or not backend and '-std=gnu++23' in args),'Metal build optimization/C++23 differs')
        require(('-DGFX_HAS_METAL=1' in args)==backend and not any(v.startswith(('-DGFX_HAS_VULKAN','-DGFX_HAS_D3D12','-flto')) or v in ('-O0','-O1','-O3','-Os','-Ofast') for v in args),'Metal backend/optimization flags differ')
        if row['file'].endswith('/src/backends/metal/Metal.cpp'):
            require('-fobjc-arc' in args and '-x' in args and args[args.index('-x')+1]=='objective-c++','Private Metal unit is not ARC Objective-C++')


def check(folder,python=None,live=False):
    r=read(folder/'receipt.json')
    versions={'macos-metal-submission-v1':1,'macos-metal-buffers-v2':2,'macos-metal-textures-v3':3,'macos-metal-graphics-v4':4}
    require(r['scope'] in versions,'Unknown Metal execution scope')
    version=versions[r['scope']];own={1:OWN,2:OWN_V2,3:OWN_V3,4:OWN_V4}[version];scope={1:SCOPE,2:SCOPE_V2,3:SCOPE_V3,4:SCOPE_V4}[version]
    core.exact(r,dict(schema_version=1,status='completed',inputs_unchanged=True,**scope))
    probe.check(folder/'availability')
    base=read(folder/'availability/core/receipt.json')
    core.exact(r,dict(source_commit=base['source_commit'],architecture=base['architecture'],SDK_source_fingerprint=base['SDK_source_fingerprint']))
    actual={p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file() and p!=folder/'receipt.json'}
    require(len(r['files'])==len(actual) and actual=={x['path'] for x in r['files']},'Metal evidence inventory differs')
    for item in r['files']:
        p=folder/item['path'];require(p.resolve().is_relative_to(folder.resolve()) and p.stat().st_size==item['bytes'] and sha(p)==item['sha256'],'Metal evidence byte drift')
    with zipfile.ZipFile(folder/'source.zip') as z:
        require(len(z.namelist())==len(own) and set(z.namelist())==set(own),'Metal source snapshot differs')
        require(len(r['source_inputs'])==len(own) and {p['path'] for p in r['source_inputs']}==set(own),'Metal frozen source inventory differs')
        for item in r['source_inputs']:require(hashlib.sha256(z.read(item['path'])).hexdigest()==item['sha256'],'Metal source byte drift')
        with zipfile.ZipFile(folder/'availability/core/source.zip') as c:
            for name in ('src/backends/metal/Metal.cpp','src/foundation/DeviceGeneration.hpp','tests/MetalSubmissionTests.cpp','tests/macos_metal_consumer/CMakeLists.txt'):
                require(z.read(name)==c.read(name),'Core/backend source snapshots differ')
            if version>=2:require(z.read('tests/MetalBufferTests.cpp')==c.read('tests/MetalBufferTests.cpp'),'Core/buffer source snapshot differs')
            if version>=3:require(z.read('tests/MetalTextureTests.cpp')==c.read('tests/MetalTextureTests.cpp'),'Core/texture source snapshot differs')
            if version==4:
                for name in ('src/backends/metal/MetalGraphics.hpp','tests/MetalGraphicsTests.cpp'):require(z.read(name)==c.read(name),'Core/graphics source snapshot differs')
    ids=probe.inventory(read(folder/'availability/receipt.json')['inventory'])
    require(ids,'No native Metal device; cannot accept submission evidence')
    for name in ('native','consumer'):
        value=read(folder/(name+'.json'));result(value,r['SDK_source_fingerprint'],ids)
        require(value==json.loads((folder/'commands'/(name+'.stdout')).read_text()),'Raw/parsed Metal output differs')
    require(read(folder/'native.json')['default_registry_id']==read(folder/'consumer.json')['default_registry_id'],'Installed/native default selection changed')
    verified=None
    if version>=2:
        for name in ('buffer-native','buffer-consumer'):
            value=read(folder/(name+'.json'));buffer_result(value,r['SDK_source_fingerprint'])
            require(value==json.loads((folder/'commands'/(name+'.stdout')).read_text()),'Raw/parsed buffer output differs')
            require(payloads(folder/(name+'-payloads'))==value['verified_bytes'],'Metal verified byte count differs')
        verified=2*value['verified_bytes']
    if version>=3:
        for name in ('texture-native','texture-consumer'):
            value=read(folder/(name+'.json'));textures.result(value,r['SDK_source_fingerprint'])
            require(value==json.loads((folder/'commands'/(name+'.stdout')).read_text()),'Raw/parsed texture output differs')
            require(textures.payloads(folder/(name+'-payloads'))==value['verified_bytes'],'Verified texture byte count differs')
    if version==4:
        for name in ('graphics-native','graphics-consumer'):
            value=read(folder/(name+'.json'));graphics.result(value,r['SDK_source_fingerprint'])
            require(value==json.loads((folder/'commands'/(name+'.stdout')).read_text()),'Raw/parsed graphics output differs')
            require(graphics.payloads(folder/(name+'-payloads'))==value['verified_bytes'],'Graphics verified byte count differs')
    junit((folder/'tests.xml').read_bytes(),version)
    graph(read(folder/'sdk-commands.json'),base['source_directory'],r['architecture'],base['sdk_path'],version)
    c=cache(folder/'sdk-cache.txt')
    require(c['GFX_ENABLE_METAL']=='ON' and all(c[k]=='OFF' for k in ('GFX_ENABLE_D3D12','GFX_ENABLE_VULKAN','GFX_BUILD_EXAMPLES')) and c['GFX_BUILD_TESTS']=='ON','Metal test build policy differs')
    for prefix in ('sdk','consumer'):
        c=cache(folder/(prefix+'-cache.txt'))
        core.exact(c,dict(CMAKE_BUILD_TYPE='Release',CMAKE_OSX_ARCHITECTURES=r['architecture'],CMAKE_OSX_SYSROOT=base['sdk_path'],CMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG',CMAKE_INTERPROCEDURAL_OPTIMIZATION='OFF',CMAKE_UNITY_BUILD='OFF'))
    c=cache(folder/'consumer-cache.txt');require(c['GraphicsSDK_DIR']==r['install_directory']+'/lib/cmake/GraphicsSDK','Metal consumer escaped fresh package')
    rows=read(folder/'consumer-commands.json')
    consumer_units=['tests/MetalSubmissionTests.cpp']+(['tests/MetalBufferTests.cpp'] if version>=2 else [])+(['tests/MetalTextureTests.cpp'] if version>=3 else [])+(['tests/MetalGraphicsTests.cpp'] if version==4 else [])
    core.compile_graph(rows,[base['source_directory']+'/'+n for n in consumer_units],r['architecture'],base['sdk_path'])
    require(all(r['install_directory']+'/include' in shlex.split(row['command']) and base['source_directory']+'/include' not in row['command'] for row in rows),'Metal consumer did not use installed public headers')
    require(r['installed_headers']==base['installed_headers'],'Metal installed headers differ from same-source Core')
    commands=read(folder/'commands.json');require(commands==r['commands'],'Metal command receipt drift')
    wanted=['configure','build','list-tests','ctest','native']+(['buffer-native'] if version>=2 else [])+(['texture-native'] if version>=3 else [])+(['graphics-native'] if version==4 else [])+['install','library-arch','consumer-configure','consumer-build','consumer-arch','consumer']+(['buffer-consumer-arch','buffer-consumer'] if version>=2 else [])+(['texture-consumer-arch','texture-consumer'] if version>=3 else [])+(['graphics-consumer-arch','graphics-consumer'] if version==4 else [])
    require([c['name'] for c in commands]==wanted,'Missing/extra Metal command processes')
    last=0
    for command in commands:
        require(command['returncode']==0 and type(command['pid']) is int and command['pid']>0 and all(type(command[k]) in (int,float) and math.isfinite(command[k]) for k in ('started','finished')) and last<=command['started']<=command['finished'],'Failed/overlapping Metal commands')
        last=command['finished']
    for name in ('library-arch','consumer-arch'):require((folder/'commands'/(name+'.stdout')).read_text().strip()==r['architecture'],'Native Metal artifact architecture differs')
    if version>=2:require((folder/'commands/buffer-consumer-arch.stdout').read_text().strip()==r['architecture'],'Buffer consumer architecture differs')
    if version>=3:require((folder/'commands/texture-consumer-arch.stdout').read_text().strip()==r['architecture'],'Texture consumer architecture differs')
    if version==4:require((folder/'commands/graphics-consumer-arch.stdout').read_text().strip()==r['architecture'],'Graphics consumer architecture differs')
    if live:require(unchanged(r['frozen_inputs']) and sdk_fingerprint(ROOT)==r['SDK_source_fingerprint'],'Metal build/source/install input changed')
    count=None
    if python:
        count=core.validate_python(python,base)
        tested={x['path']:x['sha256'] for x in read(python/'receipt.json')['inputs']}
        require(tested['tools/macos_metal_backend.py']==next(x['sha256'] for x in r['source_inputs'] if x['path']=='tools/macos_metal_backend.py'),'Executed/tested Metal runner differs')
        if version>=3:require(tested['tools/macos_metal_textures.py']==next(x['sha256'] for x in r['source_inputs'] if x['path']=='tools/macos_metal_textures.py'),'Executed/tested texture checker differs')
        if version==4:require(tested['tools/macos_metal_graphics.py']==next(x['sha256'] for x in r['source_inputs'] if x['path']=='tools/macos_metal_graphics.py'),'Executed/tested graphics checker differs')
    value=dict(architecture=r['architecture'],registry_ids=ids,CPU_CTests=11,Metal_CTests=version,native_and_installed_submissions=201,
        installed_consumer_passed=True,Python_contracts=count,**scope)
    if version>=2:
        value['marker_contract_submissions']=value.pop('native_and_installed_submissions')
        value.update(verified_buffer_bytes=verified,buffer_payload_files=44,buffer_cases_per_execution=22)
    if version>=3:value.update(texture_payload_files=88,verified_texture_bytes=2*sum(r['size'] for r in textures.catalog()),texture_cases_per_execution=44)
    if version==4:value.update(graphics_payload_files=36,verified_graphics_bytes=2*sum(r['size'] for r in graphics.catalog()),graphics_cases_per_execution=18)
    return value


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
    frozen=[record(ROOT/n) for n in OWN_V4];source_inputs=[dict(path=n,sha256=sha(ROOT/n)) for n in OWN_V4]
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
    require(len(listing['tests'])==15 and {t['name'] for t in listing['tests']}==set(core.TESTS)|{'metal_submission_contracts','metal_buffer_contracts','metal_texture_pass_contracts','metal_graphics_contracts'},'Unexpected Metal/CPU suite')
    call('ctest',['ctest','--test-dir',build,'--output-on-failure','--no-tests=error','--timeout','120','--output-junit',out/'tests.xml'])
    fp=base['SDK_source_fingerprint'];save(out/'native.json',json.loads(call('native',[build/'gfx_metal_tests',fp])))
    save(out/'buffer-native.json',json.loads(call('buffer-native',[build/'gfx_metal_buffer_tests',fp,out/'buffer-native-payloads'])))
    save(out/'texture-native.json',json.loads(call('texture-native',[build/'gfx_metal_texture_tests',fp,out/'texture-native-payloads'])))
    save(out/'graphics-native.json',json.loads(call('graphics-native',[build/'gfx_metal_graphics_tests',ROOT/'tests/fixtures/metal/graphics.metal',fp,out/'graphics-native-payloads'])))
    call('install',['cmake','--install',build]);call('library-arch',['lipo','-archs',install/'lib/libGraphicsSDKCore.a'])
    call('consumer-configure',['cmake','-S',ROOT/'tests/macos_metal_consumer','-B',consumer,*flags,'-DCMAKE_PREFIX_PATH='+str(install)])
    call('consumer-build',['cmake','--build',consumer,'--verbose']);call('consumer-arch',['lipo','-archs',consumer/'macos_metal_consumer'])
    save(out/'consumer.json',json.loads(call('consumer',[consumer/'macos_metal_consumer',fp])))
    call('buffer-consumer-arch',['lipo','-archs',consumer/'macos_metal_buffer_consumer'])
    save(out/'buffer-consumer.json',json.loads(call('buffer-consumer',[consumer/'macos_metal_buffer_consumer',fp,out/'buffer-consumer-payloads'])))
    call('texture-consumer-arch',['lipo','-archs',consumer/'macos_metal_texture_consumer'])
    save(out/'texture-consumer.json',json.loads(call('texture-consumer',[consumer/'macos_metal_texture_consumer',fp,out/'texture-consumer-payloads'])))
    call('graphics-consumer-arch',['lipo','-archs',consumer/'macos_metal_graphics_consumer'])
    save(out/'graphics-consumer.json',json.loads(call('graphics-consumer',[consumer/'macos_metal_graphics_consumer',ROOT/'tests/fixtures/metal/graphics.metal',fp,out/'graphics-consumer-payloads'])))
    for name,path in (('sdk',build),('consumer',consumer)):
        for source,target in (('CMakeCache.txt','cache.txt'),('compile_commands.json','commands.json')):(out/(name+'-'+target)).write_bytes((path/source).read_bytes())
    headers=[dict(path=p.relative_to(install/'include').as_posix(),sha256=sha(p)) for p in sorted((install/'include').rglob('*.hpp'))]
    frozen += [record(p) for p in (build/'CMakeCache.txt',build/'compile_commands.json',consumer/'CMakeCache.txt',consumer/'compile_commands.json',build/'gfx_metal_tests',build/'gfx_metal_buffer_tests',install/'lib/libGraphicsSDKCore.a',consumer/'macos_metal_consumer',consumer/'macos_metal_buffer_consumer',*(install/'include').rglob('*.hpp'))]
    require(unchanged(frozen) and sdk_fingerprint(ROOT)==fp,'Source/build/install changed during Metal execution')
    frozen += [record(build/'gfx_metal_texture_tests'),record(consumer/'macos_metal_texture_consumer'),record(build/'gfx_metal_graphics_tests'),record(consumer/'macos_metal_graphics_consumer')]
    with zipfile.ZipFile(out/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for n in OWN_V4:z.write(ROOT/n,n)
    r=dict(schema_version=1,scope='macos-metal-graphics-v4',status='completed',source_commit=commit,architecture=base['architecture'],SDK_source_fingerprint=fp,
        github_run_id=os.environ.get('GITHUB_RUN_ID'),install_directory=str(install),inputs_unchanged=True,source_inputs=source_inputs,installed_headers=headers,
        frozen_inputs=frozen,commands=commands,files=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()],**SCOPE_V4)
    save(out/'receipt.json',r);print(json.dumps(check(out,live=True),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',choices=('run','check'));parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--availability',type=pathlib.Path);parser.add_argument('--python',type=pathlib.Path);parser.add_argument('--live',action='store_true');a=parser.parse_args()
    if a.stage=='run':run(a)
    else:print(json.dumps(check(a.output,a.python,a.live),indent=2))
