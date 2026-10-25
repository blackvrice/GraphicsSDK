"""Accept actual headless Linux core/sync validation on a pinned software ICD."""
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import time
import xml.etree.ElementTree as ET

from audit_licenses import sdk_fingerprint
from run_linux_device import ROOT, HEADERS, record, require
import run_linux_draw as draw
import run_linux_input as inputs
import run_linux_depth as depth
import run_linux_timestamp as timestamp

SUITES={
 'buffer':dict(test='vulkan_transfer_validation',target='gfx_vulkan_tests',consumer='vulkan_consumer',pattern=r'PASS assertions=(\d+) transfers=(\d+) verified_bytes=(\d+) adapters=(\d+)',fields=('assertions','transfers','verified_bytes','adapters'),minimum_assertions=350),
 'texture':dict(test='vulkan_texture_validation',target='gfx_vulkan_texture_tests',consumer='vulkan_texture_consumer',pattern=r'PASS assertions=(\d+) roundtrips=(\d+) verified_texel_bytes=(\d+) padded_roundtrips=(\d+) adapters=(\d+)',fields=('assertions','roundtrips','verified_texel_bytes','padded_roundtrips','adapters'),minimum_assertions=400),
 **draw.SUITES,**inputs.SUITES,**depth.SUITES,**timestamp.SUITES,
}
TESTS={'color-pass':'vulkan_color_pass_validation','draw':'vulkan_draw_validation','vertex':'vulkan_vertex_validation','binding':'vulkan_binding_validation','depth':'vulkan_depth_validation','timestamp':'vulkan_timestamp_validation'}
EXPECTED={'buffer':dict(transfers=24,verified_bytes=28521273),'texture':dict(roundtrips=37,verified_texel_bytes=249685916,padded_roundtrips=20),**draw.workload_counts(),**inputs.workload_counts(),**depth.workload_counts(),**timestamp.workload_counts()}

def main():
    require(platform.system()=='Linux','Actual Linux host required')
    output=ROOT/'out/linux-vulkan-validation-evidence'
    runtime_path=output/'runtime/receipt.json';require(runtime_path.is_file() and set(p.name for p in output.iterdir())=={'runtime'},'Fresh prepared runtime required')
    runtime=json.loads(runtime_path.read_text());library=ROOT/runtime['layer_library']['path'];manifest=ROOT/runtime['layer_manifest']['path']
    for row in (runtime['layer_library'],runtime['layer_manifest']):require(record(ROOT/row['path'])==row,'Runtime changed before execution')
    build=ROOT/'out/linux-vulkan-validation';install=ROOT/'out/linux-vulkan-validation-install';consumer=ROOT/'out/linux-validation-consumer';identity_build=ROOT/'out/linux-validation-identity'
    require(all(not p.exists() for p in (build,install,consumer,identity_build)),'Fresh SDK/install/consumer roots required')
    drivers=sorted(pathlib.Path('/usr/share/vulkan/icd.d').glob('lvp_icd*.json'));require(len(drivers)==1,'One observed lavapipe ICD required')
    empty=output/'empty-layers';empty.mkdir();settings=output/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_bytes(b'')
    xdg=output/'xdg-runtime';xdg.mkdir(mode=0o700)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','GFX_EXPECT_VULKAN_','LD_'))}
    overrides=dict(VK_DRIVER_FILES=str(drivers[0]),VK_ICD_FILENAMES=str(drivers[0]),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(manifest.parent),VK_LAYER_SETTINGS_PATH=str(settings),GFX_EXPECT_VULKAN_LAYER=str(library),XDG_RUNTIME_DIR=str(xdg),LP_NUM_THREADS='2')
    env.update(overrides);commands=[]
    def run(name,args,expected=0,extra=None):
        start=time.monotonic();child=subprocess.run([str(a) for a in args],cwd=ROOT,env={**env,**(extra or {})},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=600)
        (output/(name+'.stdout')).write_bytes(child.stdout);(output/(name+'.stderr')).write_bytes(child.stderr)
        commands.append(dict(name=name,argv=[str(a) for a in args],returncode=child.returncode,expected=expected,environment=extra or {},seconds=time.monotonic()-start))
        (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if child.returncode!=expected:print((child.stdout+child.stderr).decode(errors='replace')[-20000:],flush=True)
        require(child.returncode==expected,name+' failed');print(name+' PASS',flush=True)
        return child.stdout.decode(),child.stderr.decode()
    def passed(junit,name):
        cases=ET.parse(junit).getroot().findall('.//testcase')
        require(len(cases)==1 and cases[0].attrib['name']==name and not any(cases[0].find(t) is not None for t in ('failure','error','skipped')),'Exact test failed/skipped')
    def loaded(text):
        paths={pathlib.Path(p.strip()).resolve() for p in re.findall(r'calling init:\s+([^\n]*libVkLayer_khronos_validation\.so)\s*$',text,re.M)}
        require(paths=={library.resolve()},'Actual loaded layer differs or was not observed')
        return sorted(str(p) for p in paths)
    commit=run('source-commit',['git','rev-parse','HEAD'])[0].strip();require(commit==os.environ.get('GITHUB_SHA',commit),'CI revision differs')
    require(not run('source-status',['git','status','--porcelain','--untracked-files=no'])[0].strip(),'Tracked source changed')
    require(run('headers-commit',['git','-C',ROOT/'.deps/Vulkan-Headers','rev-parse','HEAD'])[0].strip()==HEADERS,'Headers pin differs')
    fp=sdk_fingerprint(ROOT)
    run('packages',['dpkg-query','-W','-f=${Package} ${Version}\n','libvulkan1','mesa-vulkan-drivers','vulkan-tools'])
    run('vulkaninfo',['vulkaninfo','--summary'])
    run('configure-sdk',['cmake','--preset','linux-vulkan-validation','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
    run('build-suites',['cmake','--build','--preset','linux-vulkan-validation','--target',*[s['target'] for s in SUITES.values()],'gfx_vulkan_validation_tests','--verbose'])
    run('install-sdk',['cmake','--install',build])
    run('configure-identity',['cmake','-S','tests/linux_vulkan_device','-B',identity_build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+str(install),'-DGFX_VULKAN_HEADERS='+str(ROOT/'.deps/Vulkan-Headers')])
    run('build-identity',['cmake','--build',identity_build,'--parallel','1'])
    identity=json.loads(run('identity',[identity_build/'gfx_linux_vulkan_device','selection'])[0]);require(identity['SDK_source_fingerprint']==fp and identity['software_adapters']==1 and identity['hardware_adapters']==0,'Software device identity differs')
    run('configure-consumer',['cmake','-S','tests/vulkan_consumer','-B',consumer,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+str(install)])
    run('build-consumer',['cmake','--build',consumer,'--parallel','1','--target',*[s['consumer'] for s in SUITES.values()],'--verbose'])
    results=[]
    for name,suite in SUITES.items():
        test=TESTS.get(name,suite['test']);junit=output/(name+'-ctest.xml')
        a=run('ctest-'+name,['ctest','--preset','linux-vulkan-validation','-R','^'+test+'$','--verbose','--output-junit',junit],extra={'LD_DEBUG':'libs'});passed(junit,test)
        b=run('installed-'+name,[consumer/suite['consumer'],'--validation'],extra={'LD_DEBUG':'libs'})
        for kind,(stdout,stderr) in (('in-tree-ctest',a),('installed-consumer',b)):
            require('source='+fp+' validation=1' in stdout,'Normal suite did not request validation')
            matches=re.findall(suite['pattern'],stdout);require(len(matches)==1,'Normal suite summary missing')
            counts=dict(zip(suite['fields'],map(int,matches[0])))
            require(counts['adapters']==1 and counts['assertions']>=suite['minimum_assertions'] and all(counts[k]==v for k,v in EXPECTED[name].items()),'Normal workload differs')
            require('adapter='+identity['adapters'][0]['name']+' id='+identity['adapters'][0]['id'] in stdout,'Normal adapter differs')
            results.append(dict(suite=name,kind=kind,**counts,loaded_layer_paths=loaded(stdout+'\n'+stderr),normal_error_count_at_suite_assertions=0))
        require(results[-1]['assertions']==results[-2]['assertions'],'Same suites differ')
        text,error=run('missing-layer-'+name,[consumer/suite['consumer'],'--validation'],1,{'VK_LAYER_PATH':str(empty)})
        require('VK_LAYER_KHRONOS_validation is not installed' in error and 'PASS ' not in text,'Missing layer was not an explicit failure')
    junit=output/'controls-ctest.xml'
    out,err=run('ctest-controls',['ctest','--preset','linux-vulkan-validation','-R','^vulkan_validation_controls$','--verbose','--output-junit',junit],extra={'LD_DEBUG':'libs'});passed(junit,'vulkan_validation_controls')
    require('source='+fp+' isolated_expected_errors=true' in out,'Isolated control source differs')
    controls=re.findall(r'PASS assertions=(\d+) adapters=(\d+) native_core_errors=(\d+) native_sync_hazards=(\d+) sync_disabled_controls=(\d+) synthetic_errors=(\d+) invalid_GPU_submissions=(\d+)',out)
    require(len(controls)==1 and tuple(map(int,controls[0][1:]))==(1,32,1,1,1025,0),'Native/sync/counter controls differ')
    require(len(re.findall(r'loaded_layer=',out))==3 and len(loaded(out+'\n'+err))==1,'Native controls did not observe all layer instances')
    require((build/'libGraphicsSDKCore.a').read_bytes()==(install/'lib/libGraphicsSDKCore.a').read_bytes(),'Installed SDK library differs')
    for row in (runtime['layer_library'],runtime['layer_manifest']):require(record(ROOT/row['path'])==row,'Runtime changed during execution')
    require(sdk_fingerprint(ROOT)==fp,'SDK changed during execution')
    (output/'sdk-cache.txt').write_bytes((build/'CMakeCache.txt').read_bytes());(output/'sdk-compile-commands.json').write_bytes((build/'compile_commands.json').read_bytes())
    input_paths=['CMakePresets.json','tests/vulkan_consumer/CMakeLists.txt','tests/VulkanValidationTests.cpp','src/backends/vulkan/VulkanValidation.hpp','tools/prepare_linux_validation.py','tools/linux-vulkan-validation.json','tools/vulkan-validation.lock.json','.github/workflows/linux-vulkan-validation.yml']
    input_paths+=['tools/run_linux_'+n+'.py' for n in ('device','draw','input','depth','timestamp')]
    input_paths+=[p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'tests').glob('Vulkan*Tests.cpp'))]+[p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'tests').glob('Vulkan*Support.hpp'))]+[p.relative_to(ROOT).as_posix() for p in sorted((ROOT/'tests/fixtures').glob('*.hpp'))]
    receipt=dict(schema_version=1,task='P3-LNX-11',source_commit=commit,native_SDK_source_fingerprint=fp,github_run_id=os.environ.get('GITHUB_RUN_ID'),platform=dict(os_release=platform.freedesktop_os_release(),kernel=platform.release(),machine=platform.machine()),environment=overrides,
        runtime_receipt=record(runtime_path),identity=identity,results=results,controls=dict(zip(('assertions','adapters','native_core_errors','native_sync_hazards','sync_disabled_controls','synthetic_errors','invalid_GPU_submissions'),map(int,controls[0]))),
        required_layer_execution_accepted=True,hardware_GPU_accepted=False,WSI_accepted=False,GPU_assisted_accepted=False,long_memory_accepted=False,
        SDK_library=record(build/'libGraphicsSDKCore.a'),installed_library=record(install/'lib/libGraphicsSDKCore.a'),settings=record(settings/'vk_layer_settings.txt'),runner=record(pathlib.Path(__file__).resolve()),inputs=[record(ROOT/p) for p in sorted(set(input_paths))],diagnostics=[record(p) for p in sorted(output.rglob('*')) if p.is_file()])
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('GFX_LINUX_VALIDATION_RECEIPT='+json.dumps(receipt,separators=(',',':')),flush=True)

if __name__=='__main__':main()
