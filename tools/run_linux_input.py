"""Run existing vertex/index/instance and resource binding contracts on Linux software ICD.

Physical GPU, normal VVL, WSI and long-memory acceptance remain separate.
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

FORMATS={'RGBA8UNorm':4,'RGBA16Float':8}
SUITES={
    'vertex':dict(test='vulkan_vertex_input',target='gfx_vulkan_vertex_tests',consumer='vulkan_vertex_consumer',
        pattern=r'PASS vertex_assertions=(\d+) draws=(\d+) indexed_draws=(\d+) images=(\d+) verified_texel_bytes=(\d+) padded_images=(\d+) adapters=(\d+)',
        fields=('assertions','draws','indexed_draws','images','verified_texel_bytes','padded_images','adapters'),minimum_assertions=380),
    'binding':dict(test='vulkan_resource_bindings',target='gfx_vulkan_binding_tests',consumer='vulkan_binding_consumer',
        pattern=r'PASS binding_assertions=(\d+) draws=(\d+) snapshots=(\d+) images=(\d+) verified_texel_bytes=(\d+) padded_images=(\d+) adapters=(\d+)',
        fields=('assertions','draws','snapshots','images','verified_texel_bytes','padded_images','adapters'),minimum_assertions=280),
}

def workload_counts():
    # Include contract draws and controlled retirement; only readbacks count images.
    return {
        'vertex':dict(draws=75,indexed_draws=39,images=73,
            verified_texel_bytes=(2*(8+6+2)*65*37+2*65*37+1920*1080+3840*2160)*12+65*37*4,padded_images=69),
        'binding':dict(draws=78,snapshots=129,images=40,
            verified_texel_bytes=(18*68*36+1920*1080+3840*2160)*12,padded_images=36),
    }

def main():
    require(platform.system()=='Linux','Actual Linux host required')
    output=ROOT/'out/linux-vulkan-input-evidence'
    consumer=ROOT/'out/linux-vulkan-input-consumer'
    identity_build=ROOT/'out/linux-vulkan-input-identity'
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
    run('build-suites',['cmake','--build','--preset','linux-vulkan','--target',*[s['target'] for s in SUITES.values()],'--verbose'])
    run('install-sdk',['cmake','--install','out/linux-vulkan'])
    prefix=str(ROOT/'out/linux-vulkan-install')
    run('configure-identity',['cmake','-S','tests/linux_vulkan_device','-B',str(identity_build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+prefix,'-DGFX_VULKAN_HEADERS='+str(headers)])
    run('build-identity',['cmake','--build',str(identity_build),'--parallel','1'])
    identity=json.loads(run('identity',[str(identity_build/'gfx_linux_vulkan_device'),'selection']))
    require(identity['SDK_source_fingerprint']==fingerprint and identity['software_adapters']==1 and identity['hardware_adapters']==0 and len(identity['adapters'])==1 and identity['adapters'][0]['device_type']==4,'Software runtime/source identity differs')
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
            results.append(dict(suite=name,kind=kind,**counts))
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
    receipt=dict(schema_version=1,task='P3-LNX-08',source_commit=commit,native_SDK_source_fingerprint=fingerprint,
        github_run_id=os.environ.get('GITHUB_RUN_ID'),github_run_attempt=os.environ.get('GITHUB_RUN_ATTEMPT'),
        platform=dict(os_release=platform.freedesktop_os_release(),kernel=platform.release(),machine=platform.machine()),
        packages=packages,environment=overrides,identity=identity,driver_manifest=record(manifest,True),runtime_libraries=[record(pathlib.Path(p),True) for p in identity['runtime_modules']],
        SDK_library=record(library),installed_library=record(installed),binaries=[record(p) for s in SUITES.values() for p in (ROOT/'out/linux-vulkan'/s['target'],consumer/s['consumer'])],
        results=results,workload=dict(formats_bytes_per_texel=FORMATS,vertex_components=[1,2,3,4],color_attributes=['Float4','UNorm8x4'],memory_paths=['Upload','Device'],index_formats=['UInt16','UInt32'],base_vertices=[-1,0,1],vertex_extent=[65,37],large_indexed_extents=[[1920,1080],[3840,2160]],binding_sources=['RGBA8UNorm','RGBA16Float'],binding_targets=['RGBA8UNorm','RGBA16Float'],binding_filters=['Point','Linear'],binding_address_modes=['Clamp','Repeat'],binding_small_extent=[68,36],binding_large_extents=[[1920,1080],[3840,2160]],binding_small_cross_product_cases=32),
        color_tolerance=dict(vertex=dict(RGBA8UNorm='1.01/255',RGBA16Float=0.002),binding=dict(RGBA8UNorm='2.1/255',RGBA16Float=0.004)),padding_sentinels=dict(vertex='0xb3',binding='0xb3'),
        shader_scope='Existing embedded SPIR-V fixtures; no fresh assembly/semantic validation or HLSL compilation in this run',
        runner=record(pathlib.Path(__file__).resolve()),inputs=[record(ROOT/p) for p in ('tests/VulkanVertexTests.cpp','tests/VulkanBindingTests.cpp','tests/VulkanVertexSupport.hpp','tests/VulkanBindingSupport.hpp','tests/fixtures/VertexFixtures.hpp','tests/fixtures/BindingFixtures.hpp','tests/vulkan_consumer/CMakeLists.txt','tests/linux_vulkan_device/Device.cpp','tests/linux_vulkan_device/CMakeLists.txt','tools/run_linux_device.py','.github/workflows/linux-vulkan-input.yml','CMakePresets.json')],
        software_graphics_contracts_executed=True,hardware_GPU_accepted=False,validation_layer_success_accepted=False,WSI_accepted=False,long_memory_accepted=False,
        installation_scope='Installed public vertex/index/instance and resource binding suites executed; full multi-feature consumer/compiler-input/license audit remains P3-LNX-12',
        diagnostics=[record(p) for p in sorted(output.rglob('*')) if p.is_file()])
    (output/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('GFX_LINUX_INPUT_RECEIPT='+json.dumps(receipt,ensure_ascii=True,separators=(',',':')),flush=True)

if __name__=='__main__':main()
