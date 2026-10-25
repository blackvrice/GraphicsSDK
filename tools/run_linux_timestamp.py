"""Run timestamp query, availability, conversion and query-lifetime contracts on Linux software ICD.

Physical GPU, normal VVL, WSI and long-memory acceptance remain separate.
"""
import json
import math
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

SUITES={
    'timestamp':dict(test='vulkan_timestamps',target='gfx_vulkan_timestamp_tests',consumer='vulkan_timestamp_consumer',
        pattern=r'PASS timestamp_assertions=(\d+) measured_pairs=(\d+) dropped_query_recordings=(\d+) draws=(\d+) images=(\d+) depth_images=(\d+) verified_texel_bytes=(\d+) verified_buffer_bytes=(\d+) padded_images=(\d+) adapters=(\d+)',
        fields=('assertions','measured_pairs','dropped_query_recordings','draws','images','depth_images','verified_texel_bytes','verified_buffer_bytes','padded_images','adapters'),minimum_assertions=150),
}

def workload_counts():
    return {'timestamp':dict(measured_pairs=26,dropped_query_recordings=3,draws=3,images=9,depth_images=3,
        verified_texel_bytes=(65*37+1920*1080+3840*2160)*16,verified_buffer_bytes=2*(4099+1048579+16777216)+65539+2097155,padded_images=3)}

def main():
    require(platform.system()=='Linux','Actual Linux host required')
    output=ROOT/'out/linux-vulkan-timestamp-evidence'
    consumer=ROOT/'out/linux-vulkan-timestamp-consumer'
    identity_build=ROOT/'out/linux-vulkan-timestamp-identity'
    metadata_build=ROOT/'out/linux-vulkan-timestamp-metadata'
    require(all(not p.exists() for p in (output,consumer,identity_build,metadata_build,ROOT/'out/linux-vulkan',ROOT/'out/linux-vulkan-install')),'Fresh roots required; preserve earlier evidence')
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
    run('build-suites',['cmake','--build','--preset','linux-vulkan','--target',*([s['target'] for s in SUITES.values()]+['gfx_vulkan_timestamp_math_tests']),'--verbose'])
    run('install-sdk',['cmake','--install','out/linux-vulkan'])
    prefix=str(ROOT/'out/linux-vulkan-install')
    run('configure-identity',['cmake','-S','tests/linux_vulkan_device','-B',str(identity_build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+prefix,'-DGFX_VULKAN_HEADERS='+str(headers)])
    run('build-identity',['cmake','--build',str(identity_build),'--parallel','1'])
    identity=json.loads(run('identity',[str(identity_build/'gfx_linux_vulkan_device'),'selection']))
    require(identity['SDK_source_fingerprint']==fingerprint and identity['software_adapters']==1 and identity['hardware_adapters']==0 and len(identity['adapters'])==1 and identity['adapters'][0]['device_type']==4,'Software runtime/source identity differs')
    run('configure-metadata',['cmake','-S','tests/linux_vulkan_timestamp','-B',str(metadata_build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+prefix,'-DGFX_VULKAN_HEADERS='+str(headers)])
    run('build-metadata',['cmake','--build',str(metadata_build),'--parallel','1'])
    metadata=json.loads(run('metadata',[str(metadata_build/'gfx_linux_timestamp_metadata')]))
    require(metadata['SDK_source_fingerprint']==fingerprint and metadata['adapter_id']==identity['adapters'][0]['id'] and metadata['device_type']==4 and metadata['SDK_timestamp_queries'] and 36<=metadata['timestamp_valid_bits']<=64 and math.isfinite(metadata['timestamp_period_nanoseconds']) and metadata['timestamp_period_nanoseconds']>0,'Native queue timestamp metadata differs')
    math_output=run('ctest-math',['ctest','--preset','linux-vulkan','-R','^vulkan_timestamp_math$','--verbose','--output-junit',str(output/'math-ctest.xml')])
    math_cases=ET.parse(output/'math-ctest.xml').getroot().findall('.//testcase')
    require(len(math_cases)==1 and math_cases[0].attrib['name']=='vulkan_timestamp_math' and not any(math_cases[0].find(t) is not None for t in ('failure','error','skipped')) and re.findall(r'PASS timestamp_math_assertions=(\d+) GPU_calls=(\d+)',math_output)==[('3775','0')],'CPU wrap/period conversion suite failed or skipped')
    run('configure-consumer',['cmake','-S','tests/vulkan_consumer','-B',str(consumer),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+prefix])
    run('build-consumer',['cmake','--build',str(consumer),'--parallel','1','--target',*[s['consumer'] for s in SUITES.values()],'--verbose'])
    results=[]
    for name,suite in SUITES.items():
        junit=output/(name+'-ctest.xml')
        ctest=run('ctest-'+name,['ctest','--preset','linux-vulkan','-R','^'+suite['test']+'$','--verbose','--output-junit',str(junit)])
        cases=ET.parse(junit).getroot().findall('.//testcase')
        require(len(cases)==1 and cases[0].attrib['name']==suite['test'] and not any(cases[0].find(t) is not None for t in ('failure','error','skipped')),'Exact CTest failed/skipped or not selected')
        installed=run('installed-'+name,[str(consumer/suite['consumer'])])
        for kind,text in (('in-tree-ctest',ctest),('installed-consumer',installed)):
            require('source='+fingerprint+' validation=0' in text,'Executed SDK source/profile differs')
            matches=re.findall(suite['pattern'],text)
            require(len(matches)==1,'Missing/duplicate suite summary')
            counts=dict(zip(suite['fields'],map(int,matches[0])))
            require(counts['assertions']>=suite['minimum_assertions'] and counts['adapters']==1 and all(counts[k]==v for k,v in workload_counts()[name].items()),'Graphics workload differs')
            require(re.findall(r'adapter=(.+) id=(\d+)',text)==[(identity['adapters'][0]['name'],identity['adapters'][0]['id'])],'Graphics adapter differs')
            measured=re.findall(r'TIMESTAMP_RESULT pair=(\d+) ms=([0-9.eE+\-]+) require_positive=([01])',text)
            require(len(measured)==26 and [int(p[0]) for p in measured]==list(range(26)) and sum(p[2]=='1' for p in measured)==7,'Actual elapsed result diagnostics missing')
            elapsed=[dict(pair=int(i),milliseconds=float(ms),required_positive=bool(int(positive))) for i,ms,positive in measured]
            require(all(math.isfinite(p['milliseconds']) and p['milliseconds']>=0 and (not p['required_positive'] or p['milliseconds']>0) for p in elapsed),'Native-backed elapsed values invalid')
            immediate=re.findall(r'TIMESTAMP_IMMEDIATE ready=([01])',text)
            require(len(immediate)==23,'Immediate availability observations missing')
            results.append(dict(suite=name,kind=kind,**counts,elapsed_measurements=elapsed,immediate_ready=immediate.count('1'),immediate_not_ready=immediate.count('0')))
        require(results[-1]['assertions']==results[-2]['assertions'],'Same source suites differ')
        for kind,exe in (('in-tree',ROOT/'out/linux-vulkan'/suite['target']),('installed',consumer/suite['consumer'])):
            for suffix,args in (('unknown',['--validaton']),('extra',['--validation','extra']),('missing-layer',['--validation'])):
                label=name+'-'+kind+'-'+suffix
                text=run(label,[str(exe),*args],1)
                error=(output/(label+'.stderr')).read_text()
                require(('VK_LAYER_KHRONOS_validation is not installed' in error and 'PASS ' not in text) if suffix=='missing-layer' else ('Usage:' in error and not text.strip()),'Negative control differs')
    library=ROOT/'out/linux-vulkan/libGraphicsSDKCore.a'
    installed=ROOT/'out/linux-vulkan-install/lib/libGraphicsSDKCore.a'
    require(library.read_bytes()==installed.read_bytes(),'Installed SDK library differs')
    for path,label in ((ROOT/'out/linux-vulkan/CMakeCache.txt','sdk-cache.txt'),(ROOT/'out/linux-vulkan/compile_commands.json','sdk-compile-commands.json')):shutil.copyfile(path,output/label)
    require(sdk_fingerprint(ROOT)==fingerprint,'SDK changed during execution')
    receipt=dict(schema_version=1,task='P3-LNX-10',source_commit=commit,native_SDK_source_fingerprint=fingerprint,
        github_run_id=os.environ.get('GITHUB_RUN_ID'),github_run_attempt=os.environ.get('GITHUB_RUN_ATTEMPT'),
        platform=dict(os_release=platform.freedesktop_os_release(),kernel=platform.release(),machine=platform.machine()),
        packages=packages,environment=overrides,identity=identity,driver_manifest=record(manifest,True),runtime_libraries=[record(pathlib.Path(p),True) for p in identity['runtime_modules']],
        SDK_library=record(library),installed_library=record(installed),binaries=[record(p) for s in SUITES.values() for p in (ROOT/'out/linux-vulkan'/s['target'],consumer/s['consumer'])],
        results=results,selected_queue_timestamp_metadata=metadata,CPU_math=dict(assertions=3775,GPU_calls=0),
        workload=dict(copy_sizes=[4099,1048579,16777216],queued_copy_sizes=[65539,2097155],graphics_extents=[[65,37],[1920,1080],[3840,2160]],empty_generation_repeats=16),
        color_tolerance=dict(RGBA8UNorm='2.1/255',RGBA16Float=0.004),depth_tolerance=dict(format='D32Float',absolute_error_exclusive=0.000001,finite_required=True),padding_sentinels=dict(timestamp='0xb3'),
        wrap_scope='Synthetic boundary/wrap/masking/order/cycle tests execute CPU conversion math only; actual native counter wrap or full-cycle delay not reproduced',
        shader_scope='Existing embedded SPIR-V fixtures; no fresh assembly/semantic validation or HLSL compilation in this run',
        runner=record(pathlib.Path(__file__).resolve()),inputs=[record(ROOT/p) for p in ('tests/VulkanTimestampTests.cpp','tests/VulkanTimestampMathTests.cpp','tests/VulkanDepthSupport.hpp','tests/fixtures/DepthFixtures.hpp','tests/linux_vulkan_timestamp/Metadata.cpp','tests/linux_vulkan_timestamp/CMakeLists.txt','tests/vulkan_consumer/CMakeLists.txt','tests/linux_vulkan_device/Device.cpp','tests/linux_vulkan_device/CMakeLists.txt','tools/run_linux_device.py','.github/workflows/linux-vulkan-timestamp.yml','CMakePresets.json')],
        software_graphics_contracts_executed=True,hardware_GPU_accepted=False,validation_layer_success_accepted=False,WSI_accepted=False,long_memory_accepted=False,
        installation_scope='Installed public timestamp suite executed; full multi-feature consumer/compiler-input/license audit remains P3-LNX-12',
        diagnostics=[record(p) for p in sorted(output.rglob('*')) if p.is_file()])
    (output/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('GFX_LINUX_TIMESTAMP_RECEIPT='+json.dumps(receipt,ensure_ascii=True,separators=(',',':')),flush=True)

if __name__=='__main__':main()
