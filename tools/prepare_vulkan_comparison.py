"""Fresh Windows Vulkan comparison inputs for P3-BEN-01; performs no timed workload."""
import argparse
import hashlib
import importlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import time
from audit_licenses import ROOT,sdk_fingerprint,parse_tlog
from audit_vulkan import system_input

NAMES=('upload','draw','pso','allocation')
MODULES={n:importlib.import_module('run_vulkan_'+n) for n in NAMES}
PLAN=ROOT/'benchmarks/phase3_comparison/contract.json'
GIT=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false']


def require(ok,message):
    if not ok:raise ValueError(message)


def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def save(p,value):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()
def record(p):return dict(path=p.resolve().as_posix(),bytes=p.stat().st_size,sha256=sha(p))
def unchanged(rows):return all(pathlib.Path(r['path']).is_file() and record(pathlib.Path(r['path']))==r for r in rows)
def git(folder,*args):return subprocess.check_output([*GIT,'-C',str(folder),*args],text=True,encoding='utf-8').strip()
def pin(folder,expected):
    require(git(folder,'rev-parse','HEAD')==expected and not git(folder,'status','--porcelain','--untracked-files=no'),'Dependency pin/worktree differs: '+str(folder))
    return {x.casefold() for x in subprocess.check_output([*GIT,'-C',str(folder),'ls-files','-z']).decode().split('\0') if x}


def validate_contract(c):
    require(type(c['schema_version']) is int and c['schema_version']==1 and c['task']=='P3-BEN-01' and c['scope']=='windows-x64-vulkan-graphicssdk-sdl' and c['sdks']==['graphicssdk','sdl'],'Wrong comparison scope')
    require(set(c['profiles'])==set(NAMES),'Incomplete profiles')
    b=c['build'];require(b==dict(generator='Visual Studio 17 2022',architecture='x64',configuration='Release',runtime_library='MultiThreadedDLL',interprocedural_optimization='OFF',SDK_D3D12='ON',SDK_VULKAN='ON',SDL_STATIC='ON',SDL_SHARED='OFF'),'Unreviewed build profile')
    a=c['adapter_policy'];require(a['vendor_id']==4318 and a['preference']=='high' and a['exact_gpu_name_required'] is True and a['SDL_native_handle_verified'] is False,'Wrong initial adapter policy')
    p=c['measurement_policy'];require(p['this_stage']=='identity_probes_only' and type(p['performance_samples']) is int and p['performance_samples']==0 and p['historical_timings_reused'] is p['correctness_accepted'] is p['performance_comparison_accepted'] is p['overall_SDK_ranking'] is False and p['next_gate']=='P3-BEN-02','Probe stage promoted to measurement')
    require(a['attribution']=='unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle' and p['future_required']==['separate_required_validation','full_changing_readback','separate_processes','interleaved_repetitions','retain_all_raw_samples'],'Identity/future acceptance policy drift')
    for n,m in MODULES.items():
        p=c['profiles'][n];require(p['contract']==m.CONTRACT and p['category']==('end_to_end' if n in ('upload','draw') else 'cpu_api') and p['shapes']=={k:list(v) for k,v in m.SHAPES.items()},'Workload/contract drift: '+n)
        require(p['columns']==(['total_ms'] if n=='upload' else ['record_ms','submit_ms','wait_ms','total_ms'] if n=='draw' else ['create_ms','public_release_ms']),'Timing boundary drift: '+n)
        if hasattr(m,'WORK'):require(p['work']=={k:list(v) if isinstance(v,tuple) else v for k,v in m.WORK.items()},'Work count drift: '+n)
    require(c['profiles']['draw']['scissor']==[5,3,55,31] and c['profiles']['draw']['vertices_per_instance']==3 and c['profiles']['allocation']['buffer_bytes']==4096,'Geometry/resource contract drift')
    return c


def validate_probe(name,sdk,r,context,reference=None):
    MODULES[name].validate_identity(r)
    expected=dict(schema_version=1,sdk=sdk,probe=True,scenario='probe',preference='high',configuration='Release',validation=False,warmup=0,sample_count=0,readback_verified=False,gpu_ms=None,SDL_commit=context['SDL_commit'],source_fingerprint=context['profile_fingerprints'][name],SDK_source_fingerprint=context['SDK_source_fingerprint'],samples_ms=[],loaded_validation_layer='')
    require(all(k in r and r[k]==v and type(r[k]) is type(v) for k,v in expected.items()),'Stale or measured probe: '+name+'/'+sdk)
    require(r['device_name']==context['expected_gpu'] and r['native_vendor_id']==4318,'GPU target differs; no fallback')
    for k,v in r.items():
        if k.startswith(('verified_','created_','released_resource_','used_resources','submitted_')):require(type(v) is int and v==0,'Probe executed a workload: '+k)
    if 'CPU_intervals_ms' in r:require(r['CPU_intervals_ms']==[],'Probe has timing intervals')
    if name in ('draw','pso'):require(r['shader_header_sha256']==context['shader_header_sha256'],'Shader fixture changed')
    if reference is not None:require(all(r[k]==reference[k] for k in MODULES[name].IDENTITY_KEYS),'GPU/driver/environment differs between probes')
    return {k:r[k] for k in MODULES[name].IDENTITY_KEYS}


def cache(path):
    result={}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        if line and not line.startswith(('#','//')) and ':' in line and '=' in line:
            key,value=line.split('=',1);result[key.split(':',1)[0]]=value
    return result


def validate_compiler_flags(text):
    # /GL- explicitly disables LTCG (SDL_mslibc); paths may also contain /GL/.
    flags=set(re.findall(r'(?<!\S)(/[^\s]+)',text))
    require('/O2' in flags and '/MD' in flags and not flags.intersection(('/GL','/GL+','/MDd')),'Unreviewed Release compiler flags')


def validate_target_rejection(name,sdk,code,stdout,stderr):
    message='Selected GPU differs from expected GPU' if sdk=='sdl' else 'Exact unambiguous SDK adapter required' if name=='upload' else 'Unique exact SDK GPU required'
    lines=stderr.decode(errors='replace').strip().splitlines()
    require(type(code) is int and code==1 and not stdout and lines and lines[-1]=='FAIL: '+message,'Wrong-target probe did not reject explicitly')


def validate_cache(c,kind,roots):
    required=dict(CMAKE_GENERATOR='Visual Studio 17 2022',CMAKE_GENERATOR_PLATFORM='x64',CMAKE_MSVC_RUNTIME_LIBRARY='MultiThreadedDLL',CMAKE_INTERPROCEDURAL_OPTIMIZATION='OFF')
    if kind=='sdk':required.update(GFX_ENABLE_D3D12='ON',GFX_ENABLE_VULKAN='ON',GFX_VULKAN_XCB='OFF',GFX_BUILD_TESTS='OFF',GFX_BUILD_EXAMPLES='OFF')
    elif kind=='sdl':required.update(SDL_SHARED='OFF',SDL_STATIC='ON',SDL_GPU='ON',SDL_VULKAN='ON',SDL_TESTS='OFF',SDL_TEST_LIBRARY='OFF')
    else:
        for key,target in [('GFX_INSTALLED_SDK',roots/'sdk-install'),('SDL_BINARY_DIR',roots/'sdl-build')]:require(pathlib.Path(c[key]).resolve()==target.resolve(),'Reused build/cache path: '+key)
        require(pathlib.Path(c['GraphicsSDK_DIR']).resolve().is_relative_to((roots/'sdk-install').resolve()) and pathlib.Path(c['SDL3_DIR']).resolve().is_relative_to((roots/'sdl-build').resolve()),'Package fell back to an old root')
    required['CMAKE_CONFIGURATION_TYPES']='Release'
    require(all(c.get(k)==v for k,v in required.items()),'Build configuration drift: '+kind)


def source_paths():
    files=[ROOT/'CMakeLists.txt',*(ROOT/'include').rglob('*.hpp'),*(ROOT/'src').rglob('*.hpp'),*(ROOT/'src').rglob('*.cpp'),PLAN,pathlib.Path(__file__),ROOT/'benchmarks/competitors/sources.lock.json',ROOT/'benchmarks/common/Adapter.hpp',ROOT/'benchmarks/common/Environment.cpp',ROOT/'licenses/policy.json',ROOT/'licenses/vulkan-presentation-policy.json']
    files += [p for n in NAMES for p in (ROOT/('benchmarks/vulkan_'+n)).iterdir() if p.is_file() and p.suffix in ('.cpp','.hpp','.txt')]
    files += [ROOT/n for n in ('tests/fixtures/SpirvFixtures.hpp','tests/fixtures/spirv/fullscreen.spvasm','tests/fixtures/spirv/red.spvasm','tests/fixtures/spirv/green.spvasm')]
    files += [ROOT/('tools/run_vulkan_'+n+'.py') for n in NAMES]
    files += [ROOT/'tools/audit_licenses.py',ROOT/'tools/audit_vulkan.py',*(ROOT/'cmake').glob('*.in')]
    return sorted(set(files))


def collect_inputs(roots,tracked):
    logs=[];inputs=set();projects=[('sdk',roots/'sdk-build'),('sdl',roots/'sdl-build')]+[(n,roots/'consumers'/n) for n in NAMES]
    for kind,build in projects:
        validate_cache(cache(build/'CMakeCache.txt'),kind,roots);inputs.add(build/'CMakeCache.txt')
        reads=[p for p in build.rglob('CL.read.1.tlog') if 'Release' in p.parts];commands=[p for p in build.rglob('CL.command.1.tlog') if 'Release' in p.parts];require(reads and commands,'Missing actual Release compiler evidence: '+kind)
        for p in commands:
            text=p.read_bytes().decode('utf-16');validate_compiler_flags(text)
        for p in reads:
            for raw in parse_tlog(p.read_bytes()):
                q=pathlib.Path(raw).resolve()
                if q.suffix.casefold() in ('.obj','.pch','.pdb','.idb','.tlog','.res'):continue
                require(q.is_file(),'Missing compiler input: '+raw)
                if q.is_relative_to(ROOT/'.deps/SDL'):require(q.relative_to(ROOT/'.deps/SDL').as_posix().casefold() in tracked['sdl'],'Untracked SDL compiler input')
                elif q.is_relative_to(ROOT/'.deps/Vulkan-Headers'):require(q.relative_to(ROOT/'.deps/Vulkan-Headers').as_posix().casefold() in tracked['headers'],'Untracked Vulkan compiler input')
                elif q.is_relative_to(roots):pass
                elif q.is_relative_to(ROOT):require(q.relative_to(ROOT).as_posix().casefold().startswith(('include/','src/','benchmarks/','tests/fixtures/')),'Unexpected own compiler input')
                else:require(system_input(q),'Unreviewed external compiler input: '+raw)
                inputs.add(q)
        logs.extend(reads+commands)
        if kind in NAMES:
            links=[p for p in build.rglob('link.read.1.tlog') if 'Release' in p.parts];require(len(links)==1,'Unexpected consumer link trace count')
            expected={(roots/'sdk-install/lib/GraphicsSDKCore.lib').resolve(),(roots/'sdl-build/Release/SDL3-static.lib').resolve()};seen=set()
            for raw in parse_tlog(links[0].read_bytes()):
                if raw.casefold().endswith('.lib'):
                    q=pathlib.Path(raw).resolve();require(q in expected or system_input(q),'Unexpected linked library: '+raw);inputs.add(q);seen.add(q)
            require(expected<=seen,'New SDK/SDL libraries not both read by linker');logs.extend(links)
        for pattern in ('Lib.command.1.tlog','link.command.1.tlog'):
            logs.extend(p for p in build.rglob(pattern) if 'Release' in p.parts)
        inputs.update(p for p in build.rglob('CMake*C*Compiler.cmake') if p.is_file())
    return sorted(inputs),sorted(set(logs))


def check(folder,live=False):
    r=read(folder/'receipt.json');require(r['task']=='P3-BEN-01' and r['status']=='prepared' and r['inputs_unchanged'] is True and type(r['performance_samples']) is int and r['performance_samples']==0 and r['historical_timings_reused'] is r['performance_comparison_accepted'] is r['overall_SDK_ranking'] is False,'Not an accepted input-preparation receipt')
    require(sha(folder/'contract.json')==r['contract_sha256'] and sha(folder/'inputs.json')==r['inputs_sha256'],'Contract/input inventory changed');validate_contract(read(folder/'contract.json'))
    reference=None;expected={(n,s) for n in NAMES for s in ('graphicssdk','sdl')};require({(p['profile'],p['sdk']) for p in r['probes']}==expected and len(r['probes'])==8,'Missing profile/SDK probe')
    for p in r['probes']:
        for key in ('stdout','stderr'):require(sha(folder/p[key])==p[key+'_sha256'],'Probe log changed')
        command=next(c for c in r['commands'] if c['name']==p['profile']+'-'+p['sdk']+'-probe');require(command['returncode']==command['expected']==0 and command['argv'][1:]==['--probe',p['sdk'],'high',r['expected_gpu']],'Probe command differs')
        data=read(folder/p['stdout']);identity=validate_probe(p['profile'],p['sdk'],data,r,reference)
        if reference is None:reference=identity
        require(identity==r['identity'],'Stored GPU identity changed')
    require(len(r['rejections'])==8 and {(p['profile'],p['sdk']) for p in r['rejections']}==expected,'Missing exact-target rejection controls')
    for p in r['rejections']:
        name=p['profile']+'-'+p['sdk']+'-wrong-target';command=next(c for c in r['commands'] if c['name']==name)
        require(sha(folder/p['stderr'])==p['stderr_sha256'] and command['returncode']==p['returncode'] and command['expected']==1 and command['argv'][1:]==['--probe',p['sdk'],'high','GraphicsSDK BEN01 absent GPU'],'Expected-GPU rejection changed')
        validate_target_rejection(p['profile'],p['sdk'],p['returncode'],(folder/'commands'/(name+'.stdout')).read_bytes(),(folder/p['stderr']).read_bytes())
    if live:require(unchanged(read(folder/'inputs.json')['files']) and sdk_fingerprint(ROOT)==r['SDK_source_fingerprint'] and all(MODULES[n].fingerprint()==r['profile_fingerprints'][n] for n in NAMES),'Live build/source input changed')
    return dict(task='P3-BEN-01',input_preparation_verified=True,performance_samples=0,performance_comparison_accepted=False,overall_SDK_ranking=False)


def build(args):
    require(platform.system()=='Windows' and platform.architecture()[0]=='64bit','Windows x64 MSVC host required')
    roots=args.roots.resolve();require(roots.is_relative_to(ROOT/'out') and not roots.exists(),'New isolated roots under out/ required');require(args.gpu_name and args.cmake.is_file(),'Exact GPU name and CMake executable required');contract=validate_contract(read(PLAN));lock=read(ROOT/'benchmarks/competitors/sources.lock.json');sdl=lock['sdl']['commit'];headers=read(ROOT/'licenses/vulkan-presentation-policy.json')['dependency']['commit'];require(read(ROOT/'licenses/policy.json')['reviewed_commits']['sdl']==sdl,'Unreviewed SDL pin')
    tracked={'sdl':pin(ROOT/'.deps/SDL',sdl),'headers':pin(ROOT/'.deps/Vulkan-Headers',headers)};before=[record(p) for p in source_paths()];fp=sdk_fingerprint(ROOT);fps={n:MODULES[n].fingerprint() for n in NAMES};roots.mkdir(parents=True);out=roots/'evidence';out.mkdir();shutil.copyfile(PLAN,out/'contract.json');(out/'empty-settings').mkdir()
    r=dict(task='P3-BEN-01',status='building',source_commit=git(ROOT,'rev-parse','HEAD'),SDK_source_fingerprint=fp,profile_fingerprints=fps,shader_header_sha256=sha(ROOT/'tests/fixtures/SpirvFixtures.hpp'),SDL_commit=sdl,Vulkan_Headers_commit=headers,expected_gpu=args.gpu_name,roots=roots.relative_to(ROOT).as_posix(),contract_sha256=sha(out/'contract.json'),performance_samples=0,historical_timings_reused=False,performance_comparison_accepted=False,overall_SDK_ranking=False,probes=[],rejections=[],commands=[])
    def write():save(out/'receipt.json',r)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_'))};env.update(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(out/'empty-settings'),VK_LAYER_SETTINGS_PATH=str(out/'empty-settings'))
    r['environment_overrides']={k:v for k,v in env.items() if k.startswith('VK_')};write()
    def call(name,argv,expected=0,timeout=1200):
        a=time.monotonic();p=subprocess.run(list(map(str,argv)),cwd=ROOT,env=env,capture_output=True,timeout=timeout);stdout=out/'commands'/(name+'.stdout');stderr=out/'commands'/(name+'.stderr');stdout.parent.mkdir(exist_ok=True);stdout.write_bytes(p.stdout);stderr.write_bytes(p.stderr);r['commands'].append(dict(name=name,argv=list(map(str,argv)),returncode=p.returncode,expected=expected,seconds=time.monotonic()-a));write();require(p.returncode==expected,name+' failed; raw logs retained');return p,stdout,stderr
    try:
        common=['-G','Visual Studio 17 2022','-A','x64','-DCMAKE_CONFIGURATION_TYPES=Release','-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL','-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF']
        cmake=str(args.cmake.resolve());call('sdk-configure',[cmake,'-S',ROOT,'-B',roots/'sdk-build',*common,'-DGFX_ENABLE_D3D12=ON','-DGFX_ENABLE_VULKAN=ON','-DGFX_VULKAN_XCB=OFF','-DGFX_BUILD_TESTS=OFF','-DGFX_BUILD_EXAMPLES=OFF','-DCMAKE_INSTALL_PREFIX='+str(roots/'sdk-install')]);call('sdk-build',[cmake,'--build',roots/'sdk-build','--config','Release','--target','GraphicsSDKCore','--parallel','1']);call('sdk-install',[cmake,'--install',roots/'sdk-build','--config','Release'])
        call('sdl-configure',[cmake,'-S',ROOT/'.deps/SDL','-B',roots/'sdl-build',*common,'-DSDL_SHARED=OFF','-DSDL_STATIC=ON','-DSDL_GPU=ON','-DSDL_VULKAN=ON','-DSDL_TESTS=OFF','-DSDL_TEST_LIBRARY=OFF']);call('sdl-build',[cmake,'--build',roots/'sdl-build','--config','Release','--target','SDL3-static','--parallel','1'])
        for n in NAMES:
            b=roots/'consumers'/n;call(n+'-configure',[cmake,'-S',ROOT/('benchmarks/vulkan_'+n),'-B',b,*common,'-DGFX_INSTALLED_SDK='+str(roots/'sdk-install'),'-DSDL_BINARY_DIR='+str(roots/'sdl-build')]);call(n+'-build',[cmake,'--build',b,'--config','Release','--target','vulkan_'+n,'--parallel','1'])
        library=roots/'sdk-install/lib/GraphicsSDKCore.lib';require(sha(library)==sha(roots/'sdk-build/Release/GraphicsSDKCore.lib') and fp.encode() in library.read_bytes(),'Installed current SDK library differs');headers_installed=sorted((roots/'sdk-install/include/graphics').glob('*.hpp'));require(headers_installed and all(p.read_bytes()==(ROOT/'include/graphics'/p.name).read_bytes() for p in headers_installed),'Installed public headers differ')
        inputs,logs=collect_inputs(roots,tracked);inputs+=source_paths()+headers_installed+[args.cmake.resolve(),ROOT/'CMakeLists.txt',library,roots/'sdk-build/Release/GraphicsSDKCore.lib',roots/'sdl-build/Release/SDL3-static.lib',pathlib.Path(os.environ['WINDIR'])/'System32/vulkan-1.dll'];binaries={n:roots/'consumers'/n/'Release'/('vulkan_'+n+'.exe') for n in NAMES};inputs+=list(binaries.values());frozen=[record(p) for p in sorted(set(inputs+logs))];save(out/'inputs.json',dict(scope='Conservative actual compiler/linker reads and recorded build tools/artifacts; no symbol reachability or legal approval implied',files=frozen));r['inputs_sha256']=sha(out/'inputs.json');r['source_inputs']=before;r['binaries']={n:record(p) for n,p in binaries.items()};r['installed_SDK_library']=record(library);r['SDL_library']=record(roots/'sdl-build/Release/SDL3-static.lib')
        for p in logs+[p for p in inputs if p.name=='CMakeCache.txt' or p.name.endswith('Compiler.cmake')]:
            if p.is_relative_to(roots):q=out/'build-inputs'/p.relative_to(roots);q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
        reference=None
        for n,exe in binaries.items():
            require(fps[n].encode() in exe.read_bytes() and fp.encode() in exe.read_bytes(),'Stale consumer binary')
            for sdk in ('graphicssdk','sdl'):
                p,stdout,stderr=call(n+'-'+sdk+'-probe',[exe,'--probe',sdk,'high',args.gpu_name],timeout=60);require(not re.search(rb'Validation Error|SYNC-HAZARD',p.stderr),'Unexpected native validation error');data=json.loads(p.stdout);identity=validate_probe(n,sdk,data,r,reference)
                if reference is None:reference=identity;r['identity']=identity
                r['probes'].append(dict(profile=n,sdk=sdk,stdout=stdout.relative_to(out).as_posix(),stderr=stderr.relative_to(out).as_posix(),stdout_sha256=sha(stdout),stderr_sha256=sha(stderr)));write()
                p,_,stderr=call(n+'-'+sdk+'-wrong-target',[exe,'--probe',sdk,'high','GraphicsSDK BEN01 absent GPU'],expected=1,timeout=60);validate_target_rejection(n,sdk,p.returncode,p.stdout,p.stderr);r['rejections'].append(dict(profile=n,sdk=sdk,returncode=p.returncode,stderr=stderr.relative_to(out).as_posix(),stderr_sha256=sha(stderr)));write()
        pin(ROOT/'.deps/SDL',sdl);pin(ROOT/'.deps/Vulkan-Headers',headers);r['inputs_unchanged']=unchanged(frozen) and unchanged(before) and sdk_fingerprint(ROOT)==fp and all(MODULES[n].fingerprint()==fps[n] for n in NAMES);require(r['inputs_unchanged'],'Inputs changed during preparation');r['status']='prepared';write();check(out,live=True);print('PASS fresh SDK/SDL + four consumers, eight same-GPU probes/eight target rejections; no performance samples')
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:r.update(status='failed',error=str(e));write();print(str(e));return 1
    return 0


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',choices=('build','check'));p.add_argument('--roots',type=pathlib.Path,required=True);p.add_argument('--cmake',type=pathlib.Path);p.add_argument('--gpu-name');p.add_argument('--live',action='store_true');a=p.parse_args()
    if a.stage=='check':print(json.dumps(check(a.roots/'evidence',a.live),indent=2));return 0
    require(a.cmake is not None,'Explicit CMake executable required');return build(a)


if __name__=='__main__':raise SystemExit(main())
