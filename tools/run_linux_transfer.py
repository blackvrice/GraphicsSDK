"""Run unchanged public buffer/queue/fence/lifetime contracts on Linux software ICD.

Physical GPU, normal VVL execution and long-memory acceptance remain separate.
"""
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import time
import xml.etree.ElementTree as ET

from audit_licenses import sdk_fingerprint
from run_linux_device import HEADERS, ROOT, record, require

SIZES=(1,3,4,257,4097,65539,1048579,8388611)

def main():
    require(platform.system()=='Linux','Actual Linux host required')
    output=ROOT/'out/linux-vulkan-transfer-evidence'
    consumer=ROOT/'out/linux-vulkan-transfer-consumer'
    identity_build=ROOT/'out/linux-vulkan-transfer-identity'
    require(all(not p.exists() for p in (output,consumer,identity_build,ROOT/'out/linux-vulkan',ROOT/'out/linux-vulkan-install')),'Fresh roots required; preserve earlier evidence')
    manifests=sorted(pathlib.Path('/usr/share/vulkan/icd.d').glob('lvp_icd*.json'))
    require(len(manifests)==1,'Exactly one observed lavapipe manifest required')
    manifest=manifests[0]
    output.mkdir(parents=True)
    layers=output/'empty-layers';layers.mkdir()
    runtime=output/'xdg-runtime';runtime.mkdir(mode=0o700)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','GFX_EXPECT_VULKAN_'))}
    overrides=dict(VK_DRIVER_FILES=str(manifest),VK_ICD_FILENAMES=str(manifest),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(layers),XDG_RUNTIME_DIR=str(runtime),LP_NUM_THREADS='2')
    env.update(overrides)
    commands=[]
    def run(name,args,expected=0):
        start=time.monotonic()
        child=subprocess.run(args,cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=600)
        (output/(name+'.stdout')).write_bytes(child.stdout)
        (output/(name+'.stderr')).write_bytes(child.stderr)
        commands.append(dict(name=name,argv=args,returncode=child.returncode,expected=expected,seconds=time.monotonic()-start))
        (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
        if child.returncode!=expected:
            print(child.stdout.decode('utf-8',errors='replace')[-12000:],flush=True)
            print(child.stderr.decode('utf-8',errors='replace')[-12000:],flush=True)
        require(child.returncode==expected,name+' failed')
        print(name+' PASS',flush=True)
        return child.stdout.decode('utf-8')
    commit=run('source-commit',['git','rev-parse','HEAD']).strip()
    require(not os.environ.get('GITHUB_SHA') or os.environ['GITHUB_SHA']==commit,'CI revision differs')
    require(not run('source-status',['git','status','--porcelain','--untracked-files=no']).strip(),'Tracked source changed')
    headers=ROOT/'.deps/Vulkan-Headers'
    require(run('headers-commit',['git','-C',str(headers),'rev-parse','HEAD']).strip()==HEADERS,'Reviewed headers required')
    require(not run('headers-status',['git','-C',str(headers),'status','--porcelain']).strip(),'Headers changed')
    fingerprint=sdk_fingerprint(ROOT)
    packages=run('packages',['dpkg-query','-W','-f=${Package} ${Version}\n','libvulkan1','mesa-vulkan-drivers','vulkan-tools'])
    run('vulkaninfo',['vulkaninfo','--summary'])
    run('configure-sdk',['cmake','--preset','linux-vulkan','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
    run('build-transfer',['cmake','--build','--preset','linux-vulkan','--target','gfx_vulkan_tests','--verbose'])
    run('install-sdk',['cmake','--install','out/linux-vulkan'])
    prefix=str(ROOT/'out/linux-vulkan-install')
    run('configure-identity',['cmake','-S','tests/linux_vulkan_device','-B',str(identity_build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+prefix,'-DGFX_VULKAN_HEADERS='+str(headers)])
    run('build-identity',['cmake','--build',str(identity_build),'--parallel','1'])
    identity=json.loads(run('identity',[str(identity_build/'gfx_linux_vulkan_device'),'selection']))
    require(identity['SDK_source_fingerprint']==fingerprint and identity['software_adapters']==1 and identity['hardware_adapters']==0 and identity['adapters'][0]['device_type']==4,'Software runtime/source identity differs')
    run('configure-consumer',['cmake','-S','tests/vulkan_consumer','-B',str(consumer),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+prefix])
    run('build-consumer',['cmake','--build',str(consumer),'--parallel','1','--target','vulkan_consumer','--verbose'])
    junit=output/'transfer-ctest.xml'
    ctest_output=run('ctest-transfer',['ctest','--preset','linux-vulkan','-R','^vulkan_transfer$','--verbose','--output-junit',str(junit)])
    cases=ET.parse(junit).getroot().findall('.//testcase')
    require(len(cases)==1 and cases[0].attrib['name']=='vulkan_transfer' and not any(cases[0].find(t) is not None for t in ('failure','error','skipped')),'Exact CTest failed/skipped or not selected')
    consumer_output=run('installed-transfer',[str(consumer/'vulkan_consumer')])
    def validate(text):
        require('source='+fingerprint+' validation=0' in text,'Executed SDK source/profile differs')
        matches=re.findall(r'PASS assertions=(\d+) transfers=(\d+) verified_bytes=(\d+) adapters=(\d+)',text)
        require(len(matches)==1,'Missing/duplicate suite summary')
        counts=dict(zip(('assertions','transfers','verified_bytes','adapters'),map(int,matches[0])))
        require(counts['assertions']>=350 and counts['transfers']==len(SIZES)*3 and counts['verified_bytes']==sum(SIZES)*3 and counts['adapters']==1,'Transfer workload differs')
        adapters=re.findall(r'adapter=(.+) id=(\d+) vendor=(\d+) software=(\d+)',text)
        require(len(adapters)==1 and adapters[0][0]==identity['adapters'][0]['name'] and adapters[0][1]==identity['adapters'][0]['id'] and adapters[0][3]=='1','Transfer adapter is not the observed software device')
        return counts
    results=[dict(kind='in-tree-ctest',**validate(ctest_output)),dict(kind='installed-consumer',**validate(consumer_output))]
    require(results[0]['assertions']==results[1]['assertions'],'Same source suites differ')
    for name,exe in (('in-tree',ROOT/'out/linux-vulkan/gfx_vulkan_tests'),('installed',consumer/'vulkan_consumer')):
        for suffix,args in (('unknown',['--validaton']),('extra',['--validation','extra'])):
            run(name+'-'+suffix,[str(exe),*args],1)
            require('Usage:' in (output/(name+'-'+suffix+'.stderr')).read_text(),'Invalid CLI did not fail before work')
        run(name+'-missing-layer',[str(exe),'--validation'],1)
        require('VK_LAYER_KHRONOS_validation is not installed' in (output/(name+'-missing-layer.stderr')).read_text(),'Missing layer did not fail explicitly')
    library=ROOT/'out/linux-vulkan/libGraphicsSDKCore.a'
    installed=ROOT/'out/linux-vulkan-install/lib/libGraphicsSDKCore.a'
    require(library.read_bytes()==installed.read_bytes(),'Installed SDK library differs')
    for path,label in ((ROOT/'out/linux-vulkan/CMakeCache.txt','sdk-cache.txt'),(ROOT/'out/linux-vulkan/compile_commands.json','sdk-compile-commands.json')):shutil.copyfile(path,output/label)
    require(sdk_fingerprint(ROOT)==fingerprint,'SDK changed during execution')
    receipt=dict(schema_version=1,task='P3-LNX-05',source_commit=commit,native_SDK_source_fingerprint=fingerprint,
        github_run_id=os.environ.get('GITHUB_RUN_ID'),github_run_attempt=os.environ.get('GITHUB_RUN_ATTEMPT'),
        platform=dict(os_release=platform.freedesktop_os_release(),kernel=platform.release(),machine=platform.machine()),
        packages=packages,environment=overrides,identity=identity,driver_manifest=record(manifest,True),
        runtime_libraries=[record(pathlib.Path(p),True) for p in identity['runtime_modules']],
        SDK_library=record(library),installed_library=record(installed),binaries=[record(ROOT/'out/linux-vulkan/gfx_vulkan_tests'),record(consumer/'vulkan_consumer')],
        results=results,zero_poll_actual_timeout_observation='not counted; unchanged suite accepts completed or Timeout and retries with finite wait',
        payload_size_bytes=list(SIZES),payloads_per_size=3,runner=record(pathlib.Path(__file__).resolve()),
        inputs=[record(ROOT/p) for p in ('tests/VulkanTests.cpp','tests/vulkan_consumer/CMakeLists.txt','tests/linux_vulkan_device/Device.cpp','tests/linux_vulkan_device/CMakeLists.txt','tools/run_linux_device.py','.github/workflows/linux-vulkan-transfer.yml','CMakePresets.json')],
        software_buffer_contracts_executed=True,hardware_GPU_accepted=False,validation_layer_success_accepted=False,WSI_accepted=False,long_memory_accepted=False,
        installation_scope='Installed public buffer suite executed; full multi-feature consumer/compiler-input/license audit remains P3-LNX-12',
        diagnostics=[record(p) for p in sorted(output.rglob('*')) if p.is_file()])
    (output/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('GFX_LINUX_TRANSFER_RECEIPT='+json.dumps(receipt,ensure_ascii=True,separators=(',',':')),flush=True)

if __name__=='__main__':main()
