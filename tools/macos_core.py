"""Native macOS Core build/CPU/installed-consumer evidence; no GPU acceptance."""
import argparse
import hashlib
import json
import math
import os
import pathlib
import platform
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET
import zipfile

from audit_licenses import sdk_fingerprint
from linux_comparison import ROOT, require, read, save, sha, record, cache, unchanged, GIT

TESTS=dict(core_contracts='CoreTests',adapter_contracts='AdapterTests',memory_budget_contracts='MemoryBudgetTests',
    vertex_layout_contracts='VertexInputTests',binding_layout_contracts='BindingTests',render_pass_contracts='RenderPassTests',
    spirv_interface_contracts='SpirvTests',spirv_vertex_contracts='SpirvVertexTests',spirv_binding_contracts='SpirvBindingTests',
    vulkan_timestamp_math='VulkanTimestampMathTests',vulkan_creation_ownership='VulkanCreationTests')
CORE=('src/foundation/Backend.cpp','src/foundation/VertexInput.cpp','src/foundation/Bindings.cpp',
      'src/foundation/RenderPass.cpp','src/shader/Shader.cpp')
UNITS=(*CORE,*(f'tests/{name}.cpp' for name in TESTS.values()),'examples/Triangle.cpp','examples/IndexedQuads.cpp')
SCOPE=dict(GPU_executed=False,Metal_backend_accepted=False,physical_GPU_accepted=False,
    P3_LNX_15_completed=False,P4_MTL_01_completed=False,P4_MTL_09_completed=False,performance_accepted=False)


def exact(value,expected):
    require(all(k in value and type(value[k]) is type(v) and value[k]==v for k,v in expected.items()),'macOS evidence scope/metadata differs')


def junit(data):
    root=ET.fromstring(data);rows=root.findall('.//testcase')
    require(len(rows)==len(TESTS) and {r.get('name') for r in rows}==set(TESTS),'Missing/duplicate CPU tests')
    for row in rows:
        require(row.get('status')=='run' and not any(row.find(k) is not None for k in ('failure','error','skipped')),'CPU test not passed')
        duration=float(row.get('time','nan'));require(math.isfinite(duration) and duration>=0,'Invalid CPU test duration')


def compile_graph(commands,units,arch,sdk):
    require(type(commands) is list and len(commands)==len(units),'Compiler unit count differs')
    require({row['file'] for row in commands}==set(units),'Unexpected/missing compiled source')
    for row in commands:
        args=shlex.split(row['command'])
        require('-arch' in args and args[args.index('-arch')+1]==arch and '-isysroot' in args and args[args.index('-isysroot')+1]==sdk,'Compiler architecture/SDK differs')
        require('-O2' in args and '-DNDEBUG' in args and any(v in args for v in ('-std=c++23','-std=gnu++23')),'Release/C++23 compiler flags absent')
        require(not any(v.startswith(('-DGFX_HAS_','-flto')) or v in ('-O0','-O1','-O3','-Os','-Ofast') for v in args),'Unexpected backend/optimization')


def validate_python(folder,r):
    value=read(folder/'receipt.json')
    exact(value,dict(source_commit=r['source_commit'],SDK_source_fingerprint=r['SDK_source_fingerprint'],successful=True,inputs_unchanged=True,
        failures=[],errors=[],skipped=[],expected_failures=[],unexpected_successes=[],native_GPU_executed_by_this_Python_suite=False,memory_or_Intel_investigation_resumed=False))
    exact(value['platform'],dict(system='Darwin',machine=r['architecture']))
    require(type(value['tests']) is int and value['tests']>0,'No Python contracts executed')
    require(sha(folder/'unittest.log')==value['log']['sha256'],'Python log drift')
    tested={x['path']:x['sha256'] for x in value['inputs']}
    require(tested['tools/macos_core.py']==next(x['sha256'] for x in r['source_inputs'] if x['path']=='tools/macos_core.py'),'Tested/executed macOS runner differs')
    return value['tests']


def check(folder,python=None,live=False):
    r=read(folder/'receipt.json')
    exact(r,dict(schema_version=1,scope='macos-core-cpu-v1',status='completed',inputs_unchanged=True,system='Darwin',pointer_bits=64,**SCOPE))
    require(r['architecture'] in ('arm64','x86_64') and len(r['source_commit'])==40,'Unknown native architecture/revision')
    actual={x.relative_to(folder).as_posix() for x in folder.rglob('*') if x.is_file() and x!=folder/'receipt.json'}
    require(actual=={x['path'] for x in r['files']},'Native evidence inventory differs')
    for item in r['files']:
        path=folder/item['path'];require(path.resolve().is_relative_to(folder.resolve()) and path.stat().st_size==item['bytes'] and sha(path)==item['sha256'],'Native evidence byte drift')
    with zipfile.ZipFile(folder/'source.zip') as z:
        require(len(z.namelist())==len(r['source_inputs']) and set(z.namelist())=={x['path'] for x in r['source_inputs']},'Source inventory differs')
        for item in r['source_inputs']:require(hashlib.sha256(z.read(item['path'])).hexdigest()==item['sha256'],'Source byte drift')
        source={x['path']:x['sha256'] for x in r['source_inputs']}
        keys=sorted(n for n in source if n=='CMakeLists.txt' or n.startswith('include/') and n.endswith('.hpp') or n.startswith('src/') and n.endswith(('.cpp','.hpp')))
        require(hashlib.sha256(''.join(n+'|'+source[n]+'\n' for n in keys).encode()).hexdigest()==r['SDK_source_fingerprint'],'SDK source fingerprint differs')
        require(len(r['installed_headers'])==len({x['path'] for x in r['installed_headers']}) and
            {x['path'] for x in r['installed_headers']}=={n.removeprefix('include/') for n in source if n.startswith('include/') and n.endswith('.hpp')},'Installed header coverage differs')
        for item in r['installed_headers']:
            require(source['include/'+item['path']]==item['sha256'],'Installed public header differs from source')
    for build in ('core','consumer'):
        c=cache(folder/(build+'-cache.txt'))
        require(c['CMAKE_BUILD_TYPE']=='Release' and c['CMAKE_OSX_ARCHITECTURES']==r['architecture'] and c['CMAKE_OSX_SYSROOT']==r['sdk_path'],'Build cache target differs')
        require(c['CMAKE_CXX_FLAGS_RELEASE']=='-O2 -DNDEBUG' and c['CMAKE_INTERPROCEDURAL_OPTIMIZATION']=='OFF' and c['CMAKE_UNITY_BUILD']=='OFF','Build policy differs')
    c=cache(folder/'core-cache.txt')
    require(all(c[k]=='OFF' for k in ('GFX_ENABLE_D3D12','GFX_ENABLE_VULKAN','GFX_VULKAN_XCB','GFX_VULKAN_VALIDATION_TESTS')) and c['GFX_BUILD_TESTS']=='ON','GPU backend or missing CPU tests')
    c=cache(folder/'consumer-cache.txt')
    require(c['GraphicsSDK_DIR']==r['install_directory']+'/lib/cmake/GraphicsSDK','Consumer package escaped fresh installation')
    compile_graph(read(folder/'core-commands.json'),[r['source_directory']+'/'+n for n in UNITS],r['architecture'],r['sdk_path'])
    commands=read(folder/'consumer-commands.json')
    compile_graph(commands,[r['source_directory']+'/tests/macos_core_consumer/main.cpp'],r['architecture'],r['sdk_path'])
    require(r['install_directory']+'/include' in shlex.split(commands[0]['command']) and r['source_directory']+'/include' not in commands[0]['command'],'Consumer did not use installed headers')
    junit((folder/'cpu-tests.xml').read_bytes())
    exact(read(folder/'consumer.json'),dict(schema_version=1,contract='installed_core_cpu_only_v1',SDK_source_fingerprint=r['SDK_source_fingerprint'],pointer_bits=64,D3D12_compiled=False,Vulkan_compiled=False,Metal_compiled=False,GPU_executed=False))
    require(json.loads((folder/'commands/consumer.stdout').read_text())==read(folder/'consumer.json'),'Consumer raw/parsed output differs')
    require(read(folder/'commands.json')==r['commands'],'Command receipt drift')
    require([row['name'] for row in r['commands']]==['os','xcode','cmake','ninja','compiler-path','sdk-path','sdk-version','compiler',
        'configure','build','list-tests','ctest','install','library-arch','consumer-configure','consumer-build','consumer-arch','consumer'],'Missing/extra native commands')
    require('Apple clang' in (folder/'commands/compiler.stdout').read_text() and (folder/'commands/library-arch.stdout').read_text().strip()==r['architecture'] and (folder/'commands/consumer-arch.stdout').read_text().strip()==r['architecture'],'Native toolchain/artifact architecture differs')
    last=0
    for command in r['commands']:
        require(command['returncode']==0 and type(command['pid']) is int and command['pid']>0 and all(type(command[k]) in (int,float) and math.isfinite(command[k]) for k in ('started','finished')) and last<=command['started']<=command['finished'],'Failed/overlapping native command')
        last=command['finished']
    if live:require(unchanged(r['frozen_inputs']),'Source/build/install input changed')
    tests=validate_python(python,r) if python else None
    return dict(architecture=r['architecture'],CPU_CTests=len(TESTS),installed_consumer_passed=True,Python_contracts=tests,**SCOPE)


def run(a):
    require(platform.system()=='Darwin' and platform.machine()==a.architecture,'Native macOS architecture required')
    require(platform.architecture()[0]=='64bit','64-bit Python required')
    out=a.output.resolve();require(out.is_relative_to(ROOT/'out') and not out.exists(),'Fresh output under out/ required')
    def git(*args):return subprocess.check_output([*GIT,*args],cwd=ROOT,text=True).strip()
    require(not git('status','--porcelain','--untracked-files=no'),'Clean tracked checkout required')
    commit=git('rev-parse','HEAD');require(not os.environ.get('GITHUB_SHA') or os.environ['GITHUB_SHA']==commit,'CI/source revision differs')
    tracked=git('ls-files').splitlines()
    selected=[n for n in tracked if n.startswith(('include/','src/','tests/','examples/','cmake/')) and n.endswith(('.cpp','.hpp','.cmake','.in','CMakeLists.txt'))]
    selected+=['CMakeLists.txt','tools/macos_core.py','.github/workflows/macos-core.yml','LICENSE']
    frozen=[record(ROOT/n) for n in sorted(set(selected))];fp=sdk_fingerprint(ROOT)
    out.mkdir(parents=True);(out/'commands').mkdir();commands=[]
    base={k:v for k,v in os.environ.items() if k not in ('CC','CXX','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS','SDKROOT','CPATH','CPLUS_INCLUDE_PATH','LIBRARY_PATH','CMAKE_PREFIX_PATH') and not k.startswith(('DYLD_','LD_','GFX_','VK_'))}
    def call(name,argv):
        started=time.monotonic();child=subprocess.Popen(list(map(str,argv)),cwd=ROOT,env=base,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:stdout,stderr=child.communicate(timeout=600)
        except subprocess.TimeoutExpired:child.kill();stdout,stderr=child.communicate();raise
        (out/'commands'/(name+'.stdout')).write_bytes(stdout);(out/'commands'/(name+'.stderr')).write_bytes(stderr)
        commands.append(dict(name=name,argv=list(map(str,argv)),pid=child.pid,started=started,finished=time.monotonic(),returncode=child.returncode));save(out/'commands.json',commands)
        require(child.returncode==0,name+' failed: '+(stdout+stderr).decode(errors='replace')[-6000:]);return stdout.decode().strip()
    for name,args in (('os',['sw_vers']),('xcode',['xcodebuild','-version']),('cmake',['cmake','--version']),('ninja',['ninja','--version'])):call(name,args)
    compiler=call('compiler-path',['xcrun','--find','clang++']);sdk=str(pathlib.Path(call('sdk-path',['xcrun','--sdk','macosx','--show-sdk-path'])).resolve());call('sdk-version',['xcrun','--sdk','macosx','--show-sdk-version']);call('compiler',[compiler,'--version'])
    build=out.with_name(out.name+'-build');consumer=out.with_name(out.name+'-consumer');install=out.with_name(out.name+'-install')
    require(not any(p.exists() for p in (build,consumer,install)),'Fresh build/install roots required')
    flags=['-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER='+compiler,'-DCMAKE_OSX_ARCHITECTURES='+a.architecture,'-DCMAKE_OSX_SYSROOT='+sdk,
        '-DCMAKE_CXX_FLAGS=','-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG','-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF','-DCMAKE_UNITY_BUILD=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
    call('configure',['cmake','-S',ROOT,'-B',build,*flags,'-DCMAKE_INSTALL_PREFIX='+str(install),'-DCMAKE_INSTALL_LIBDIR=lib','-DGFX_ENABLE_D3D12=OFF','-DGFX_ENABLE_VULKAN=OFF','-DGFX_VULKAN_XCB=OFF','-DGFX_VULKAN_VALIDATION_TESTS=OFF','-DGFX_BUILD_TESTS=ON','-DGFX_BUILD_EXAMPLES=ON'])
    call('build',['cmake','--build',build,'--parallel','3','--verbose'])
    listing=json.loads(call('list-tests',['ctest','--test-dir',build,'--show-only=json-v1']))
    require(len(listing['tests'])==len(TESTS) and {r['name'] for r in listing['tests']}==set(TESTS),'Unexpected CPU suite')
    call('ctest',['ctest','--test-dir',build,'--output-on-failure','--no-tests=error','--timeout','120','--output-junit',out/'cpu-tests.xml'])
    call('install',['cmake','--install',build]);call('library-arch',['lipo','-archs',install/'lib/libGraphicsSDKCore.a'])
    call('consumer-configure',['cmake','-S',ROOT/'tests/macos_core_consumer','-B',consumer,*flags,'-DCMAKE_PREFIX_PATH='+str(install)])
    call('consumer-build',['cmake','--build',consumer,'--verbose']);call('consumer-arch',['lipo','-archs',consumer/'macos_core_consumer'])
    value=json.loads(call('consumer',[consumer/'macos_core_consumer',fp]));save(out/'consumer.json',value)
    for name,folder in (('core',build),('consumer',consumer)):
        for src,dst in (('CMakeCache.txt','cache.txt'),('compile_commands.json','commands.json')):(out/(name+'-'+dst)).write_bytes((folder/src).read_bytes())
    require(unchanged(frozen) and sdk_fingerprint(ROOT)==fp,'Sources changed during native build')
    with zipfile.ZipFile(out/'source.zip','w',zipfile.ZIP_DEFLATED) as z:
        for item in frozen:z.write(item['path'],pathlib.Path(item['path']).relative_to(ROOT).as_posix())
    installed=[dict(path=p.relative_to(install/'include').as_posix(),sha256=sha(p)) for p in sorted((install/'include').rglob('*.hpp'))]
    require({x['path'] for x in installed}=={p.relative_to(ROOT/'include').as_posix() for p in (ROOT/'include').rglob('*.hpp')},'Installed header inventory differs')
    frozen+= [record(p) for p in (build/'CMakeCache.txt',build/'compile_commands.json',consumer/'CMakeCache.txt',consumer/'compile_commands.json',install/'lib/libGraphicsSDKCore.a',consumer/'macos_core_consumer')]
    frozen+= [record(p) for p in (install/'include').rglob('*.hpp')]
    r=dict(schema_version=1,scope='macos-core-cpu-v1',status='completed',source_commit=commit,system='Darwin',architecture=a.architecture,pointer_bits=64,
        github_run_id=os.environ.get('GITHUB_RUN_ID'),runner_image=os.environ.get('ImageOS'),source_directory=str(ROOT),install_directory=str(install),sdk_path=sdk,
        SDK_source_fingerprint=fp,inputs_unchanged=True,source_inputs=[dict(path=pathlib.Path(x['path']).relative_to(ROOT).as_posix(),sha256=x['sha256']) for x in frozen[:len(set(selected))]],
        installed_headers=installed,frozen_inputs=frozen,commands=commands,files=[dict(path=p.relative_to(out).as_posix(),bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(out.rglob('*')) if p.is_file()],**SCOPE)
    save(out/'receipt.json',r);print(json.dumps(check(out,live=True),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stage',choices=('run','check'));parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--architecture',choices=('arm64','x86_64'));parser.add_argument('--python',type=pathlib.Path);parser.add_argument('--live',action='store_true');a=parser.parse_args()
    if a.stage=='run':run(a)
    else:print(json.dumps(check(a.output,a.python,a.live),indent=2))
