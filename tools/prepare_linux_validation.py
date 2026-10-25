"""Build pinned VVL 1.3.290 for headless Linux; never install to the system."""
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys
import time

from run_linux_device import ROOT, record, require

def main():
    require(platform.system()=='Linux','Actual Linux host required')
    output=ROOT/'out/linux-vulkan-validation-evidence/runtime'
    build_root=ROOT/'out/linux-vvl-runtime-build'
    require(not output.parent.exists() and not build_root.exists(),'Fresh runtime/evidence roots required')
    profile_path=ROOT/'tools/linux-vulkan-validation.json'
    profile=json.loads(profile_path.read_text())
    lock_path=ROOT/profile['source_lock'];lock=json.loads(lock_path.read_text())
    require(len(lock['components'])==6 and lock['components'][0]['commit']=='79f4759c2c492c1be37e18c0318330f2959327cb','Reviewed VVL pins required')
    output.mkdir(parents=True);build_root.mkdir()
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','GFX_EXPECT_VULKAN_'))}
    env['CMAKE_BUILD_PARALLEL_LEVEL']='1'
    commands=[]
    def run(name,args):
        start=time.monotonic()
        with (output/(name+'.log')).open('wb') as log:
            r=subprocess.run([str(a) for a in args],cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,timeout=2400)
        commands.append(dict(name=name,argv=[str(a) for a in args],returncode=r.returncode,seconds=time.monotonic()-start))
        (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        print(name+' exit='+str(r.returncode)+' seconds='+str(round(time.monotonic()-start,1)),flush=True)
        if r.returncode:print((output/(name+'.log')).read_text(errors='replace')[-16000:],flush=True)
        require(r.returncode==0,name+' failed')
    def git(path,*args):return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
    sources={}
    for c in lock['components']:
        name=c['name'];path=ROOT/'.deps'/('Vulkan-Headers' if name=='Vulkan-Headers' else 'vvl-linux/'+name)
        if not path.exists():
            run(name+'-init',['git','init',path]);run(name+'-remote',['git','-C',path,'remote','add','origin',c['url']])
            run(name+'-fetch',['git','-c','gc.auto=0','-C',path,'fetch','--depth','1','origin',c['commit']])
            run(name+'-checkout',['git','-C',path,'checkout','--detach','FETCH_HEAD'])
        require(git(path,'rev-parse','HEAD')==c['commit'] and not git(path,'status','--porcelain','--untracked-files=no'),'Dependency is not clean/pinned: '+name)
        sources[name]=path
    prefix=build_root/'prerequisites'
    common=['-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
        '-DCMAKE_INSTALL_PREFIX='+str(prefix),'-DCMAKE_PREFIX_PATH='+str(prefix),'-DCMAKE_INSTALL_LIBDIR=lib',
        '-DCMAKE_POSITION_INDEPENDENT_CODE=ON','-DCMAKE_CXX_FLAGS_RELEASE='+profile['runtime_release_flags'],
        '-DPython3_EXECUTABLE='+sys.executable,'-DPYTHON_EXECUTABLE='+sys.executable]
    for name in ('Vulkan-Headers','Vulkan-Utility-Libraries','SPIRV-Headers','SPIRV-Tools','robin-hood-hashing'):
        options=[]
        if name=='SPIRV-Tools':options=['-DSPIRV-Headers_SOURCE_DIR='+str(sources['SPIRV-Headers']),'-DSPIRV_WERROR=OFF','-DSPIRV_SKIP_TESTS=ON','-DSPIRV_SKIP_EXECUTABLES=ON']
        if name=='robin-hood-hashing':options=['-DRH_STANDALONE_PROJECT=OFF']
        build=build_root/name
        run(name+'-configure',['cmake','-S',sources[name],'-B',build,*common,*options])
        run(name+'-build',['cmake','--build',build,'--target','install','--parallel',str(profile['prerequisite_parallel'])])
    build=build_root/'Vulkan-ValidationLayers'
    run('layer-configure',['cmake','-S',sources['Vulkan-ValidationLayers'],'-B',build,*common,'-DUPDATE_DEPS=OFF','-DBUILD_TESTS=OFF','-DUSE_MIMALLOC=OFF','-DBUILD_WERROR=OFF','-DBUILD_WSI_XCB_SUPPORT=OFF','-DBUILD_WSI_XLIB_SUPPORT=OFF','-DBUILD_WSI_WAYLAND_SUPPORT=OFF'])
    run('layer-build',['cmake','--build',build,'--parallel',str(profile['layer_parallel'])])
    manifests=list(build.rglob('VkLayer_khronos_validation.json'));libraries=list(build.rglob('libVkLayer_khronos_validation.so'))
    require(len(manifests)==len(libraries)==1,'One layer binary/manifest required')
    manifest=manifests[0];library=libraries[0].resolve()
    data=json.loads(manifest.read_text())
    require(data['layer']['api_version']=='1.3.290' and data['layer']['name']=='VK_LAYER_KHRONOS_validation','Layer manifest differs')
    # Match the generated manifest's real target without rewriting upstream files.
    declared=pathlib.Path(data['layer']['library_path'])
    require((manifest.parent/declared).resolve()==library if not declared.is_absolute() else declared.resolve()==library,'Manifest library target differs')
    for c in lock['components']:require(git(sources[c['name']],'rev-parse','HEAD')==c['commit'] and not git(sources[c['name']],'status','--porcelain','--untracked-files=no'),'Source modified during build')
    receipt=dict(schema_version=1,profile=profile,profile_file=record(profile_path),source_lock=record(lock_path),components=lock['components'],
        layer_manifest=record(manifest),layer_library=record(library),builder=record(pathlib.Path(__file__).resolve()),
        licenses=[record(p) for source in sources.values() for p in source.iterdir() if p.is_file() and p.name.lower().startswith(('license','copying'))],
        runtime_redistributed=False,system_installation=False,diagnostics=[record(p) for p in sorted(output.rglob('*')) if p.is_file()])
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('GFX_LINUX_VVL_RUNTIME='+json.dumps(receipt,separators=(',',':')),flush=True)

if __name__=='__main__':main()
