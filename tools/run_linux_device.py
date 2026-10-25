"""Actual Linux Vulkan device selection on explicitly selected Mesa software ICD.

No physical-GPU, transfer, rendering, validation success or WSI acceptance.
Only child environments receive driver/layer/loader-negative controls.
"""
import hashlib
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import time

from audit_licenses import sdk_fingerprint

ROOT=pathlib.Path(__file__).resolve().parents[1]
HEADERS='b379292b2ab6df5771ba9870d53cf8b2c9295daf'

def require(value,message):
    if not value:raise ValueError(message)

def record(path,external=False):
    return dict(path=str(path) if external else path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def main():
    require(platform.system()=='Linux','Actual Linux host required')
    output=ROOT/'out/linux-vulkan-device-evidence'
    probe_build=ROOT/'out/linux-vulkan-device-probe'
    require(not output.exists() and not probe_build.exists() and not (ROOT/'out/linux-vulkan').exists() and not (ROOT/'out/linux-vulkan-install').exists(),'Fresh build/install/evidence roots required')
    manifests=sorted(pathlib.Path('/usr/share/vulkan/icd.d').glob('lvp_icd*.json'))
    require(len(manifests)==1,'Exactly one observed lavapipe manifest required')
    manifest=manifests[0]
    output.mkdir(parents=True)
    controls=output/'controls';controls.mkdir()
    layer_path=controls/'empty-layers';layer_path.mkdir()
    fake_loader=controls/'unloadable-loader';fake_loader.mkdir()
    (fake_loader/'libvulkan.so.1').write_bytes(b'controlled unloadable Vulkan loader\n')
    runtime_dir=controls/'xdg-runtime';runtime_dir.mkdir(mode=0o700)
    base={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','GFX_EXPECT_VULKAN_'))}
    settings=dict(VK_DRIVER_FILES=str(manifest),VK_ICD_FILENAMES=str(manifest),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(layer_path),XDG_RUNTIME_DIR=str(runtime_dir),LP_NUM_THREADS='2')
    base.update(settings)
    commands=[]
    def run(name,args,expected=0,overrides=None):
        start=time.monotonic()
        child=subprocess.run(args,cwd=ROOT,env={**base,**(overrides or {})},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=600)
        (output/(name+'.stdout')).write_bytes(child.stdout)
        (output/(name+'.stderr')).write_bytes(child.stderr)
        commands.append(dict(name=name,argv=args,returncode=child.returncode,expected=expected,seconds=time.monotonic()-start,overrides=overrides or {}))
        (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
        if child.returncode!=expected:
            print(child.stdout.decode('utf-8',errors='replace')[-12000:],flush=True)
            print(child.stderr.decode('utf-8',errors='replace')[-12000:],file=sys.stderr,flush=True)
        require(child.returncode==expected,name+' failed')
        print(name+' PASS',flush=True)
        return child.stdout.decode('utf-8')
    commit=run('source-commit',['git','rev-parse','HEAD']).strip()
    require(not os.environ.get('GITHUB_SHA') or os.environ['GITHUB_SHA']==commit,'CI revision differs')
    require(not run('source-status',['git','status','--porcelain','--untracked-files=no']).strip(),'Tracked sources changed')
    headers=ROOT/'.deps/Vulkan-Headers'
    require(run('headers-commit',['git','-C',str(headers),'rev-parse','HEAD']).strip()==HEADERS,'Reviewed headers required')
    require(not run('headers-status',['git','-C',str(headers),'status','--porcelain']).strip(),'Headers changed')
    fingerprint=sdk_fingerprint(ROOT)
    packages=run('packages',['dpkg-query','-W','-f=${Package} ${Version}\n','libvulkan1','mesa-vulkan-drivers','vulkan-tools'])
    run('vulkaninfo',['vulkaninfo','--summary'])
    run('configure-sdk',['cmake','--preset','linux-vulkan','-DCMAKE_CXX_COMPILER=g++-14','-DGFX_BUILD_TESTS=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
    run('build-sdk',['cmake','--build','--preset','linux-vulkan','--verbose'])
    run('install-sdk',['cmake','--install','out/linux-vulkan'])
    run('configure-probe',['cmake','-S','tests/linux_vulkan_device','-B',str(probe_build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+str(ROOT/'out/linux-vulkan-install'),'-DGFX_VULKAN_HEADERS='+str(headers)])
    run('build-probe',['cmake','--build',str(probe_build),'--parallel','1','--verbose'])
    executable=probe_build/'gfx_linux_vulkan_device'
    runs=[]
    for name,case,overrides in (
        ('selection-1','selection',{}),('selection-2','selection',{}),
        ('no-driver','no-driver',dict(VK_DRIVER_FILES=str(controls/'absent-icd.json'),VK_ICD_FILENAMES=str(controls/'absent-icd.json'))),
        ('no-loader','no-loader',dict(LD_LIBRARY_PATH=str(fake_loader))),
        ('missing-layer','missing-layer',{}),
    ):
        result=json.loads(run(name,[str(executable),case],overrides=overrides))
        require(result['schema_version']==1 and result['case']==case and result['SDK_source_fingerprint']==fingerprint,'Runtime source/case identity differs')
        if case=='selection':
            require(result['software_adapters']>0 and result['hardware_adapters']==0 and all(a['device_type']==4 for a in result['adapters']),'Software-only classification differs')
            require(result['device_creations']==2*len(result['adapters'])+1 and result['assertions']>=30,'Exact device selection coverage')
            required={'software-kind-mismatch','backend-id-mismatch','missing-exact-id-no-fallback','zero-id-no-fallback','hardware-default-never-falls-back-to-software','uncompiled-backend-enumeration','unsupported-loss-opt-in'}
            require(required<={r['case'] for r in result['rejections']},'Missing selection negative controls')
            require(any('libvulkan_lvp.so' in p for p in result['runtime_modules']),'Actual lavapipe module missing')
        else:
            require(result['device_creations']==0 and result['assertions']>=3,'Unavailable runtime must not create a device')
        runs.append(dict(name=name,result=result))
    require(runs[0]['result']['adapters']==runs[1]['result']['adapters'],'Fresh-process device/driver identities differ')
    run('invalid-mode',[str(executable),'unknown'],1)
    run('extra-argument',[str(executable),'selection','extra'],1)
    modules=sorted(set(runs[0]['result']['runtime_modules']+[runs[0]['result']['loader_path']]))
    libraries=[record(pathlib.Path(p),True) for p in modules]
    library=ROOT/'out/linux-vulkan/libGraphicsSDKCore.a'
    installed=ROOT/'out/linux-vulkan-install/lib/libGraphicsSDKCore.a'
    require(library.read_bytes()==installed.read_bytes(),'Installed SDK differs from built library')
    for path,label in ((ROOT/'out/linux-vulkan/CMakeCache.txt','sdk-cache.txt'),(ROOT/'out/linux-vulkan/compile_commands.json','sdk-compile-commands.json')):
        shutil.copyfile(path,output/label)
    require(sdk_fingerprint(ROOT)==fingerprint,'SDK changed during execution')
    receipt=dict(schema_version=1,task='P3-LNX-04',source_commit=commit,native_SDK_source_fingerprint=fingerprint,
        github_run_id=os.environ.get('GITHUB_RUN_ID'),github_run_attempt=os.environ.get('GITHUB_RUN_ATTEMPT'),
        platform=dict(os_release=platform.freedesktop_os_release(),kernel=platform.release(),machine=platform.machine()),
        packages=packages,environment=settings,removed_inherited_keys=sorted(k for k in os.environ if k not in base),
        driver_manifest=record(manifest,True),driver_manifest_contents=json.loads(manifest.read_text()),runtime_libraries=libraries,
        SDK_library=record(library),installed_library=record(installed),probe=record(executable),runs=runs,
        runner=record(pathlib.Path(__file__).resolve()),inputs=[record(ROOT/p) for p in ('tests/linux_vulkan_device/Device.cpp','tests/linux_vulkan_device/CMakeLists.txt','.github/workflows/linux-vulkan-device.yml','CMakePresets.json')],
        software_Vulkan_device_execution=True,hardware_GPU_accepted=False,transfer_render_accepted=False,validation_layer_success_accepted=False,WSI_accepted=False,
        install_scope='Only installed SDK device probe executed; full consumer/audit acceptance remains separate',
        diagnostics=[record(p) for p in sorted(output.rglob('*')) if p.is_file()])
    (output/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('GFX_LINUX_DEVICE_RECEIPT='+json.dumps(receipt,ensure_ascii=True,separators=(',',':')),flush=True)

if __name__=='__main__':main()
