"""Physical Linux GPU preflight and required-validation acceptance; no software fallback."""
import argparse
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
from run_linux_device import ROOT, HEADERS, require
from run_linux_validation import SUITES, EXPECTED, TESTS

OUT=ROOT/'out/linux-hardware-evidence'
BUILD=ROOT/'out/linux-hardware-sdk'
INSTALL=ROOT/'out/linux-hardware-install'
PROBE=ROOT/'out/linux-hardware-probe'
CONSUMER=ROOT/'out/linux-hardware-consumer'


def record(path):
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):digest.update(chunk)
    return dict(path=str(path),bytes=path.stat().st_size,sha256=digest.hexdigest())


def physical(identity,expected,fingerprint,pci):
    require(identity['schema_version']==1 and type(identity['schema_version']) is int and identity['case']=='selection' and identity['SDK_source_fingerprint']==fingerprint,'Native source identity differs')
    adapters=identity['adapters'];n=len(adapters)
    require(n>0 and type(identity['software_adapters']) is int and identity['software_adapters']==0 and type(identity['hardware_adapters']) is int and identity['hardware_adapters']==n,'Physical GPU required; software/CPU or empty candidate set rejected')
    require(all(type(a['device_type']) is int and a['device_type'] in (1,2) for a in adapters),'Only native integrated/discrete GPU kinds accepted; virtual/other/CPU rejected')
    require(len(expected)==len(set(expected))==n and {a['device_uuid'] for a in adapters}==set(expected),'Exact expected UUID set differs; no adapter fallback')
    for a in adapters:
        require(all(type(a[k]) is str and re.fullmatch('[0-9a-f]{32}',a[k]) and int(a[k],16) for k in ('device_uuid','driver_uuid')),'Invalid native UUID')
        value=14695981039346656037
        for b in bytes.fromhex(a['device_uuid']):value=((value^b)*1099511628211)&((1<<64)-1)
        require(type(a['id']) is str and a['id']==str(value) and value>0,'Exact UUID-derived adapter ID differs')
        require(all(type(a[k]) is int and a[k]>0 for k in ('vendor_id','device_id','api_version_raw','driver_version_raw')),'Native driver/device metadata missing')
        require(any(p['vendor_id']==a['vendor_id'] and p['device_id']==a['device_id'] and p['driver'] in ('nvidia','amdgpu','i915','xe','nouveau') for p in pci),'No corroborating physical PCI GPU/driver; WSL/virtual renderers are not hardware acceptance')
    require(identity['device_creations']==2*n,'Exact selected-device creation coverage differs')
    required={'software-kind-mismatch','backend-id-mismatch','missing-exact-id-no-fallback','zero-id-no-fallback','uncompiled-backend-enumeration','unsupported-loss-opt-in'}
    require(required<={x['case'] for x in identity['rejections']},'Selection rejection controls missing')
    return adapters


def suite_result(name,stdout,identity,fingerprint):
    suite=SUITES[name];n=len(identity['adapters'])
    require('source='+fingerprint+' validation=1' in stdout,'Required validation or source identity missing')
    found=re.findall(suite['pattern'],stdout);require(len(found)==1,'Normal suite summary missing')
    counts=dict(zip(suite['fields'],map(int,found[0])))
    require(counts['adapters']==n and counts['assertions']>=suite['minimum_assertions']*n and all(counts[k]==v*n for k,v in EXPECTED[name].items()),'Normal workload/adapter count differs')
    actual=re.findall(r'adapter=(.*?) id=(\d+)',stdout)
    require(len(actual)==n and set(actual)=={(a['name'],a['id']) for a in identity['adapters']},'Normal suite used a different adapter')
    return counts


def driver_libraries(manifest,stderr,loader):
    declared=pathlib.Path(json.loads(manifest.read_text())['ICD']['library_path'])
    paths=[pathlib.Path(p.strip()) for p in re.findall(r'calling init:\s+(/[^\n]+)\s*$',stderr,re.M)]
    matches={p.resolve() for p in paths if p.name==declared.name or ('/' in str(declared) and p.resolve()==(manifest.parent/declared).resolve())}
    require(len(matches)==1,'Actual ICD shared-library load was not observed unambiguously')
    return [record(p) for p in sorted(matches|{pathlib.Path(loader).resolve()})]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage',choices=('preflight','accept'))
    parser.add_argument('--icd',type=pathlib.Path,required=True)
    parser.add_argument('--device-uuids',required=True,help='Comma-separated exact UUIDs of every physical device visible through this ICD')
    args=parser.parse_args();expected=args.device_uuids.split(',')
    require(all(re.fullmatch('[0-9a-f]{32}',x) and int(x,16) for x in expected) and len(set(expected))==len(expected),'Explicit nonzero lowercase UUID set required')
    require(platform.system()=='Linux','Actual Linux host required')
    manifest=args.icd.resolve();require(manifest.is_file() and manifest.suffix=='.json','Existing explicit ICD manifest required')
    if args.stage=='preflight':
        require(all(not p.exists() for p in (OUT,BUILD,INSTALL,PROBE,CONSUMER)),'Fresh hardware roots required')
        OUT.mkdir(parents=True);(OUT/'empty-layers').mkdir();(OUT/'xdg-runtime').mkdir(mode=0o700)
    else:require((OUT/'preflight.json').is_file() and not (OUT/'receipt.json').exists(),'Successful preflight and no existing acceptance required')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','GFX_EXPECT_VULKAN_','LD_'))}
    env.update(VK_DRIVER_FILES=str(manifest),VK_ICD_FILENAMES=str(manifest),VK_LAYER_PATH=str(OUT/'empty-layers'),VK_LOADER_LAYERS_DISABLE='~implicit~',XDG_RUNTIME_DIR=str(OUT/'xdg-runtime'))
    commands=[] if args.stage=='preflight' else json.loads((OUT/'commands.json').read_text())
    def run(name,argv,code=0,extra=None):
        start=time.monotonic();p=subprocess.run(list(map(str,argv)),cwd=ROOT,env={**env,**(extra or {})},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=600)
        (OUT/(name+'.stdout')).write_bytes(p.stdout);(OUT/(name+'.stderr')).write_bytes(p.stderr)
        commands.append(dict(name=name,argv=list(map(str,argv)),expected=code,returncode=p.returncode,seconds=time.monotonic()-start,environment=extra or {}))
        (OUT/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if p.returncode!=code:print((p.stdout+p.stderr).decode(errors='replace')[-16000:],flush=True)
        require(p.returncode==code,name+' failed');print(name+' PASS',flush=True)
        return p.stdout.decode(),p.stderr.decode()
    source=run(args.stage+'-source',['git','rev-parse','HEAD'])[0].strip()
    require(not os.environ.get('GITHUB_SHA') or os.environ['GITHUB_SHA']==source,'CI revision differs')
    require(not run(args.stage+'-status',['git','status','--porcelain','--untracked-files=no'])[0].strip(),'Tracked files changed')
    fingerprint=sdk_fingerprint(ROOT)
    headers=ROOT/'.deps/Vulkan-Headers'
    require(run(args.stage+'-headers',['git','-C',headers,'rev-parse','HEAD'])[0].strip()==HEADERS and not run(args.stage+'-headers-status',['git','-C',headers,'status','--porcelain'])[0].strip(),'Clean pinned Vulkan headers required')
    pci=[]
    for device in sorted(pathlib.Path('/sys/bus/pci/devices').glob('*')):
        if (device/'class').read_text().strip() not in ('0x030000','0x030200','0x038000'):continue
        pci.append(dict(address=device.name,vendor_id=int((device/'vendor').read_text(),16),device_id=int((device/'device').read_text(),16),driver=(device/'driver').resolve().name if (device/'driver').exists() else None))
    (OUT/(args.stage+'-pci.json')).write_text(json.dumps(pci,indent=2)+'\n')
    if args.stage=='preflight':
        run('vulkaninfo',['vulkaninfo','--summary'])
        run('configure-sdk',['cmake','--preset','linux-vulkan-validation','-B',BUILD,'-DCMAKE_INSTALL_PREFIX='+str(INSTALL),'-DCMAKE_CXX_COMPILER=g++-14','-DGFX_BUILD_TESTS=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        run('build-sdk',['cmake','--build',BUILD,'--parallel','1'])
        run('install-sdk',['cmake','--install',BUILD])
        run('configure-probe',['cmake','-S','tests/linux_vulkan_device','-B',PROBE,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+str(INSTALL),'-DGFX_VULKAN_HEADERS='+str(headers)])
        run('build-probe',['cmake','--build',PROBE,'--parallel','1'])
        observations=[];libraries=[]
        for i in (1,2):
            stdout,stderr=run('selection-'+str(i),[PROBE/'gfx_linux_vulkan_device','selection'],extra={'LD_DEBUG':'libs'})
            observation=json.loads(stdout);observations.append(observation)
            current=driver_libraries(manifest,stderr,observation['loader_path'])
            require(not libraries or libraries==current,'Loaded loader/ICD changed between processes');libraries=current
        require(observations[0]['adapters']==observations[1]['adapters'],'Fresh native identity/driver differs')
        preflight=dict(schema_version=1,source_commit=source,native_SDK_source_fingerprint=fingerprint,expected_UUIDs=expected,driver_manifest=record(manifest),runtime_libraries=libraries,SDK_library=record(BUILD/'libGraphicsSDKCore.a'),probe=record(PROBE/'gfx_linux_vulkan_device'),identity=observations[0],pci=pci,physical_preflight_passed=False,hardware_GPU_accepted=False)
        try:physical(observations[0],expected,fingerprint,pci);preflight['physical_preflight_passed']=True
        except ValueError as error:
            preflight['rejection']=str(error);(OUT/'preflight.json').write_text(json.dumps(preflight,indent=2)+'\n');raise
        (OUT/'preflight.json').write_text(json.dumps(preflight,indent=2)+'\n')
        print('Physical preflight PASS; common suites and validation still pending',flush=True);return
    preflight=json.loads((OUT/'preflight.json').read_text());identity=preflight['identity']
    require(preflight['physical_preflight_passed'] and preflight['source_commit']==source and preflight['native_SDK_source_fingerprint']==fingerprint and preflight['expected_UUIDs']==expected and preflight['driver_manifest']==record(manifest),'Preflight scope changed')
    physical(identity,expected,fingerprint,pci)
    for row in preflight['runtime_libraries']+[preflight['SDK_library'],preflight['probe']]:require(record(pathlib.Path(row['path']))==row,'Preflight runtime/SDK/probe binary changed')
    runtime_path=ROOT/'out/linux-vulkan-validation-evidence/runtime/receipt.json';runtime=json.loads(runtime_path.read_text())
    require(runtime['profile']==json.loads((ROOT/'tools/linux-vulkan-validation.json').read_text()) and runtime['components']==json.loads((ROOT/'tools/vulkan-validation.lock.json').read_text())['components'],'Pinned VVL source/profile differs')
    layer=ROOT/runtime['layer_library']['path'];layer_manifest=ROOT/runtime['layer_manifest']['path']
    for row in (runtime['layer_library'],runtime['layer_manifest']):require(record(ROOT/row['path'])['sha256']==row['sha256'],'Prepared validation runtime changed')
    settings=OUT/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_bytes(b'')
    env.update(VK_LAYER_PATH=str(layer_manifest.parent),VK_LAYER_SETTINGS_PATH=str(settings),GFX_EXPECT_VULKAN_LAYER=str(layer))
    def loaded(stderr):
        paths={pathlib.Path(p.strip()).resolve() for p in re.findall(r'calling init:\s+([^\n]*libVkLayer_khronos_validation\.so)\s*$',stderr,re.M)}
        require(paths=={layer.resolve()},'Actual pinned layer load path differs')
    run('configure-suites',['cmake','-S',ROOT,'-B',BUILD,'-DGFX_BUILD_TESTS=ON'])
    run('build-suites',['cmake','--build',BUILD,'--parallel','1','--target',*[s['target'] for s in SUITES.values()],'gfx_vulkan_validation_tests'])
    run('install-suites-sdk',['cmake','--install',BUILD])
    run('configure-consumer',['cmake','-S','tests/vulkan_consumer','-B',CONSUMER,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_PREFIX_PATH='+str(INSTALL)])
    run('build-consumer',['cmake','--build',CONSUMER,'--parallel','1'])
    fresh=json.loads(run('selection-before',[PROBE/'gfx_linux_vulkan_device','selection'])[0]);require(fresh['adapters']==identity['adapters'],'Preflight devices/driver changed')
    results=[]
    for name,suite in SUITES.items():
        test=TESTS.get(name,suite['test']);junit=OUT/(name+'.xml')
        executions=[('in-tree',run('ctest-'+name,['ctest','--test-dir',BUILD,'-R','^'+test+'$','--verbose','--output-junit',junit],extra={'LD_DEBUG':'libs'})),('installed',run('installed-'+name,[CONSUMER/suite['consumer'],'--validation'],extra={'LD_DEBUG':'libs'}))]
        cases=ET.parse(junit).getroot().findall('.//testcase');require(len(cases)==1 and cases[0].attrib['name']==test and not any(cases[0].find(x) is not None for x in ('error','failure','skipped')),'Required CTest failed/skipped')
        for kind,(stdout,stderr) in executions:
            counts=suite_result(name,stdout,identity,fingerprint);loaded(stdout+'\n'+stderr);results.append(dict(suite=name,kind=kind,**counts,normal_error_count_at_suite_assertions=0))
        require(results[-1]['assertions']==results[-2]['assertions'],'In-tree/installed assertions differ')
        stdout,stderr=run('missing-layer-'+name,[CONSUMER/suite['consumer'],'--validation'],1,{'VK_LAYER_PATH':str(OUT/'empty-layers')})
        require('VK_LAYER_KHRONOS_validation is not installed' in stderr and 'PASS ' not in stdout,'Missing layer did not explicitly fail')
    stdout,stderr=run('validation-controls',[BUILD/'gfx_vulkan_validation_tests'],extra={'LD_DEBUG':'libs'});loaded(stdout+'\n'+stderr)
    n=len(identity['adapters']);control=re.findall(r'PASS assertions=(\d+) adapters=(\d+) native_core_errors=(\d+) native_sync_hazards=(\d+) sync_disabled_controls=(\d+) synthetic_errors=(\d+) invalid_GPU_submissions=(\d+)',stdout)
    require(len(control)==1 and tuple(map(int,control[0][1:]))==(n,32*n,n,n,1025*n,0),'Native/sync error controls differ')
    final=json.loads(run('selection-after',[PROBE/'gfx_linux_vulkan_device','selection'])[0]);require(final['adapters']==identity['adapters'],'Native device/driver changed during suites')
    require((BUILD/'libGraphicsSDKCore.a').read_bytes()==(INSTALL/'lib/libGraphicsSDKCore.a').read_bytes() and sdk_fingerprint(ROOT)==fingerprint,'SDK changed')
    for row in preflight['runtime_libraries']+[preflight['SDK_library'],preflight['probe']]:require(record(pathlib.Path(row['path']))==row,'Runtime/SDK/probe changed during suites')
    for row in (runtime['layer_library'],runtime['layer_manifest']):require(record(ROOT/row['path'])['sha256']==row['sha256'],'Validation runtime changed')
    receipt=dict(schema_version=1,task='P3-LNX-15',source_commit=source,native_SDK_source_fingerprint=fingerprint,github_run_id=os.environ.get('GITHUB_RUN_ID'),platform=dict(os_release=platform.freedesktop_os_release(),kernel=platform.release()),preflight=preflight,results=results,controls=list(map(int,control[0])),runtime_receipt=record(runtime_path),SDK_library=record(BUILD/'libGraphicsSDKCore.a'),hardware_GPU_accepted=True,WSI_accepted=False,long_memory_accepted=False,diagnostics=[record(p) for p in sorted(OUT.rglob('*')) if p.is_file()])
    (OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('GFX_LINUX_HARDWARE_RECEIPT='+json.dumps(receipt,separators=(',',':')),flush=True)


if __name__=='__main__':main()
