"""Linux SDK/SDL identity and required-validation probes; no timed workload.

Software diagnostics never complete BEN-07. Physical mode requires an explicit
UUID and corroborating PCI driver; SDL identity remains unique-name attribution.
"""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import time

from audit_licenses import sdk_fingerprint

ROOT=pathlib.Path(__file__).resolve().parents[1]
HEADERS='b379292b2ab6df5771ba9870d53cf8b2c9295daf'
OWN=['benchmarks/common/LinuxEnvironment.hpp','benchmarks/linux_comparison/CMakeLists.txt','benchmarks/linux_comparison/Probe.cpp','benchmarks/linux_comparison/Selection.cpp','benchmarks/competitors/sources.lock.json']
SDKS=('graphicssdk','sdl')
GIT=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false','-c','core.preloadIndex=false']

def require(ok,message):
    if not ok:raise ValueError(message)
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def save(p,value):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for part in iter(lambda:f.read(1024*1024),b''):h.update(part)
    return h.hexdigest()

def fingerprint():return hashlib.sha256(''.join(n+'|'+sha(ROOT/n)+'\n' for n in sorted(OWN)).encode()).hexdigest()
def record(p):return dict(path=p.resolve().as_posix(),bytes=p.stat().st_size,sha256=sha(p))
def unchanged(rows):return all(pathlib.Path(x['path']).is_file() and record(pathlib.Path(x['path']))==x for x in rows)
def git(folder,*args):return subprocess.check_output([*GIT,'-C',str(folder),*args],text=True).strip()
def pin(folder,commit):require(git(folder,'rev-parse','HEAD')==commit and not git(folder,'status','--porcelain','--untracked-files=no'),'Dependency pin or tracked bytes changed')

def cache(path):
    result={}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        if not line.startswith(('#','//')) and ':' in line and '=' in line:
            key,value=line.split('=',1);result[key.split(':',1)[0]]=value
    return result

def packages(c,install,sdl):
    require(c['CMAKE_BUILD_TYPE']=='Release' and pathlib.Path(c['GraphicsSDK_DIR']).resolve().is_relative_to(install.resolve())
            and pathlib.Path(c['SDL3_DIR']).resolve().is_relative_to(sdl.resolve()),'Probe package escaped fresh SDK/SDL roots')

def driver_libraries(manifest,stderr,loader):
    declared=pathlib.Path(read(manifest)['ICD']['library_path'])
    paths=[pathlib.Path(p.strip()) for p in re.findall(r'calling init:\s+(/[^\n]+)\s*$',stderr,re.M)]
    matches={p.resolve() for p in paths if p.name==declared.name or ('/' in str(declared) and p.resolve()==(manifest.parent/declared).resolve())}
    require(len(matches)==1,'Actual ICD shared-library load not observed unambiguously')
    return [record(p) for p in sorted(matches|{pathlib.Path(loader).resolve()})]

def identity(d):
    require(type(d['name']) is str and d['name'] and all(type(d[k]) is str and re.fullmatch('[0-9a-f]{32}',d[k]) and int(d[k],16) for k in ('device_uuid','driver_uuid')),'Invalid native name/UUID')
    value=14695981039346656037
    for byte in bytes.fromhex(d['device_uuid']):value=((value^byte)*1099511628211)&((1<<64)-1)
    require(type(d['adapter_id']) is str and d['adapter_id']==str(value) and value>0,'UUID-derived ID must be an exact decimal string')
    require(all(type(d[k]) is int and 0<=d[k]<=0xffffffff for k in ('vendor_id','device_id','driver_version_raw','api_version_raw','device_type'))
            and d['api_version_raw']>=4198400 and d['driver_version_raw']>0 and d['device_type'] in (0,1,2,3,4) and d['device_luid'] is None,'Invalid Linux native metadata; no LUID required')
    return d

def select(rows,uuid,mode,pci):
    require(rows and len({d['device_uuid'] for d in rows})==len(rows) and len({identity(d)['adapter_id'] for d in rows})==len(rows),'Empty/duplicate native UUID or ID')
    matches=[d for d in rows if d['device_uuid']==uuid];require(len(matches)==1,'Exact UUID absent or ambiguous')
    d=matches[0];require(sum(x['name']==d['name'] for x in rows)==1,'Ambiguous public GPU name rejected')
    require(mode in ('physical','software-diagnostic'),'Unknown probe mode')
    if mode=='physical':
        require(d['device_type'] in (1,2),'Physical probe rejects software, virtual and other devices')
        require(any(p['vendor_id']==d['vendor_id'] and p['device_id']==d['device_id'] and p['driver'] in ('nvidia','amdgpu','i915','xe','nouveau') for p in pci),'No corroborating PCI GPU/driver')
    else:require(d['device_type']==4,'Software diagnostic requires native CPU device')
    return d

def validate_result(r,job,context):
    expected=dict(schema_version=1,task='P3-BEN-07',contract='linux_native_uuid_public_name_probe_v1',sdk=job['sdk'],mode=job['mode'],probe=True,validation=job['validation'],logical_device_created=True,
                  source_fingerprint=context['probe_fingerprint'],SDK_source_fingerprint=context['SDK_source_fingerprint'],SDL_commit=context['SDL_commit'],
                  identity_attribution='unique_exact_name_to_native_vulkan_not_SDL_native_handle',SDL_native_handle_verified=False,
                  validation_error_count=0,sample_count=0,samples_ms=[],gpu_ms=None,readback_verified=False,benchmark_accepted=False)
    require(all(k in r and r[k]==v and type(r[k]) is type(v) for k,v in expected.items()),'Probe metadata/timing/acceptance drift')
    require(r['inventory']==context['inventory'] and r['selected']==select(r['inventory'],context['device_uuid'],job['mode'],context['pci']),'Inventory/selected device changed')
    e=r['environment'];require(e==context['environment'] and e['platform']=='Linux' and e['pointer_bits']==64 and type(e['ram_bytes']) is int and e['ram_bytes']>0
            and all(type(e[k]) is str and e[k] for k in ('kernel','machine','cpu','compiler')) and e['compiler'].startswith('GCC '),'Native Linux environment changed')
    require(r['loader_path']==context['loader_path'] and pathlib.PurePosixPath(r['loader_path']).is_absolute() and r['loader_path'] in r['runtime_modules'],'Actual loader changed or not observed')
    require(r['loaded_validation_layer']==(context['validation_library'] if job['validation'] else ''),'Pinned validation mode/path changed')
    require((context['validation_library'] in r['runtime_modules'])==job['validation'],'Loaded validation library contamination')
    return r

def jobs(mode):
    result=[dict(sdk=sdk,mode=mode,validation=validation,kind='probe',repetition=rep) for rep in range(2) for validation in (False,True) for sdk in SDKS]
    result += [dict(sdk=sdk,mode=('physical' if mode=='software-diagnostic' else 'software-diagnostic') if kind=='wrong-kind' else mode,validation=kind=='missing-layer',kind=kind,repetition=0) for sdk in SDKS for kind in ('wrong-uuid','missing-layer','wrong-kind')]
    return [dict(j,name=f"{i:02}-{j['kind']}-{j['sdk']}-{int(j['validation'])}-{j['repetition']}") for i,j in enumerate(result)]

def overrides(folder,icd,layer,validation):
    env=dict(VK_DRIVER_FILES=icd,VK_ICD_FILENAMES=icd,VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(folder/'empty'),VK_LAYER_SETTINGS_PATH=str(folder/'empty'),
             SDL_VIDEODRIVER='offscreen',XDG_RUNTIME_DIR=str(folder/'xdg'),LD_DEBUG='libs')
    if validation:env.update(VK_LAYER_PATH=str(pathlib.Path(layer).parent),VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',VK_LAYER_SETTINGS_PATH=str(folder/'settings'),GFX_EXPECT_VULKAN_LAYER=layer,VK_KHRONOS_VALIDATION_VALIDATE_SYNC='true',VK_LOADER_DEBUG='layer')
    return env

def control(job,code,stdout,stderr):
    require(code==1 and type(code) is int and not stdout,'Control produced output or succeeded')
    text={'wrong-uuid':b'FAIL: Exact UUID absent or ambiguous','wrong-kind':b'FAIL: Requested probe kind differs from native device type','missing-layer':b'FAIL: Required comparison validation layer absent'}[job['kind']]
    require(text in stderr,'Unrelated error cannot satisfy native control')

def check(folder,live=False):
    r=read(folder/'receipt.json');require(r['status']=='completed' and r['task']=='P3-BEN-07' and r['inputs_unchanged'] is True,'Incomplete probe campaign')
    mode=r['mode'];require(r['physical_probe_qualified']==(mode=='physical') and type(r['physical_probe_qualified']) is bool
            and r['P3_BEN_07_completed']==(mode=='physical') and type(r['P3_BEN_07_completed']) is bool
            and r['workload_accepted'] is r['performance_accepted'] is r['SDL_native_handle_verified'] is False and type(r['measurement_samples']) is int and r['measurement_samples']==0,'Diagnostic probe promoted to hardware/workload/performance acceptance')
    require(sha(folder/'runtime-receipt.json')==r['runtime_receipt_sha256'],'Runtime receipt changed')
    runtime=read(folder/'runtime-receipt.json');require(runtime['profile']==read(ROOT/'tools/linux-vulkan-validation.json') and runtime['components']==read(ROOT/'tools/vulkan-validation.lock.json')['components'],'Runtime source/profile changed')
    require(any(x['path']==r['validation_library'] and x['sha256']==runtime['layer_library']['sha256'] for x in r['frozen_inputs']),'Runtime library binding changed')
    inventory=read(folder/'inventory.stdout');require(sha(folder/'inventory.stdout')==r['inventory_sha256'] and inventory['inventory']==r['inventory'] and inventory['logical_device_created'] is False and type(inventory['sample_count']) is int and inventory['sample_count']==0,'Inventory changed or ran a workload')
    require(inventory['environment']==r['environment'] and inventory['loader_path']==r['loader_path'] and inventory['selected'] is None
            and all(inventory[k]==r[k] for k in ('SDK_source_fingerprint','SDL_commit')) and inventory['source_fingerprint']==r['probe_fingerprint'],'Inventory/build/environment provenance changed')
    require(sha(folder/'selection.stdout')==r['selection_sha256'] and read(folder/'selection.stdout')==dict(assertions=9,rejections=7,native_GPU_executed=False),'Native CPU selection controls changed')
    require((folder/'settings/vk_layer_settings.txt').read_text()=='khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\n','Required sync settings changed')
    selected=select(r['inventory'],r['device_uuid'],mode,r['pci']);require(selected==r['selected'],'Selection changed')
    expected=jobs(mode);require(len(r['runs'])==len(expected),'Probe/control count changed');last_end=0
    for row,job in zip(r['runs'],expected):
        require(row['job']==job and type(row['pid']) is int and row['pid']>0 and last_end<=row['started']<=row['finished'],'Order/process/serialization changed');last_end=row['finished']
        env=overrides(pathlib.Path(r['output_directory']),r['icd']['path'],r['validation_library'],job['validation'])
        if job['kind']=='missing-layer':env['VK_LAYER_PATH']=str(pathlib.Path(r['output_directory'])/'empty')
        uuid='0'*32 if job['kind']=='wrong-uuid' else r['device_uuid']
        require(row['argv']==[r['probe']['path'],'probe',job['sdk'],uuid,str(int(job['validation'])),job['mode']]
                and row['overrides'].keys()==env.keys() and all(row['overrides'][k].replace('\\','/')==value.replace('\\','/') for k,value in env.items()),'Command/environment changed')
        stdout=(folder/row['stdout']).read_bytes();stderr=(folder/row['stderr']).read_bytes();require(hashlib.sha256(stdout).hexdigest()==row['stdout_sha256'] and hashlib.sha256(stderr).hexdigest()==row['stderr_sha256'],'Raw process output changed')
        if job['kind']=='probe':
            require(row['returncode']==0 and not re.search(rb'Validation Error|SYNC-HAZARD|VUID-.*(?:ERROR|Error)',stdout+b'\n'+stderr),'Native probe/validation failed')
            result=validate_result(json.loads(stdout),job,r);require(result==row['result'],'Raw/parsed result differs')
        else:control(job,row['returncode'],stdout,stderr)
    if live:require(unchanged(r['frozen_inputs']) and sdk_fingerprint(ROOT)==r['SDK_source_fingerprint'] and fingerprint()==r['probe_fingerprint'],'Source/build/runtime inputs changed')
    return dict(task='P3-BEN-07',mode=mode,probe_processes=8,control_processes=6,measurement_samples=0,physical_probe_qualified=r['physical_probe_qualified'],P3_BEN_07_completed=r['P3_BEN_07_completed'],workload_accepted=False,performance_accepted=False)

def run(a):
    require(platform.system()=='Linux','Actual Linux host required');out=a.output.resolve();build=out.with_name(out.name+'-build')
    require(out.is_relative_to(ROOT/'out') and not out.exists() and not build.exists(),'Fresh output/build roots under out/ required')
    icd=a.icd.resolve();require(icd.is_file() and icd.suffix=='.json','Explicit existing ICD manifest required')
    require(a.mode=='software-diagnostic' or a.device_uuid is not None,'Physical mode requires an explicit device UUID')
    runtime_path=a.runtime_receipt.resolve();runtime=read(runtime_path);require(runtime['profile']==read(ROOT/'tools/linux-vulkan-validation.json') and runtime['components']==read(ROOT/'tools/vulkan-validation.lock.json')['components'],'Pinned Linux validation runtime required')
    layer=(ROOT/runtime['layer_library']['path']).resolve();manifest=(ROOT/runtime['layer_manifest']['path']).resolve()
    require(sha(layer)==runtime['layer_library']['sha256'] and sha(manifest)==runtime['layer_manifest']['sha256'],'Runtime artifacts changed')
    declared=read(manifest)['layer'];require(declared['name']=='VK_LAYER_KHRONOS_validation' and (manifest.parent/declared['library_path']).resolve()==layer,'Runtime manifest target differs')
    require(not git(ROOT,'status','--porcelain','--untracked-files=no'),'Tracked sources changed');commit=git(ROOT,'rev-parse','HEAD')
    require(not os.environ.get('GITHUB_SHA') or os.environ['GITHUB_SHA']==commit,'CI revision differs')
    sdl_pin=read(ROOT/'benchmarks/competitors/sources.lock.json')['sdl']['commit'];pin(ROOT/'.deps/SDL',sdl_pin);pin(ROOT/'.deps/Vulkan-Headers',HEADERS)
    sources=sorted(set([ROOT/n for n in OWN]+[pathlib.Path(__file__),ROOT/'tools/audit_licenses.py',ROOT/'tools/linux-vulkan-validation.json',ROOT/'tools/vulkan-validation.lock.json',ROOT/'CMakeLists.txt',*(ROOT/'include').rglob('*.hpp'),*(ROOT/'src').rglob('*.hpp'),*(ROOT/'src').rglob('*.cpp'),*(ROOT/'cmake').glob('*.in')]))
    before=[record(p) for p in sources];out.mkdir(parents=True);build.mkdir();save(out/'runtime-receipt.json',runtime)
    for n in ('empty','settings','xdg'):(out/n).mkdir(mode=0o700)
    (out/'settings/vk_layer_settings.txt').write_text('khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\n')
    base={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_','LD_')) and k not in ('DISPLAY','WAYLAND_DISPLAY')}
    commands=[]
    def call(name,argv,extra=None,timeout=600):
        env=extra or {};start=time.monotonic();child=subprocess.Popen(list(map(str,argv)),cwd=ROOT,env={**base,**env},stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:stdout,stderr=child.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:child.kill();stdout,stderr=child.communicate();(out/(name+'.stdout')).write_bytes(stdout);(out/(name+'.stderr')).write_bytes(stderr);raise
        row=dict(argv=list(map(str,argv)),pid=child.pid,started=start,finished=time.monotonic(),returncode=child.returncode,overrides=env)
        for key,data in (('stdout',stdout),('stderr',stderr)):
            p=out/(name+'.'+key);p.write_bytes(data);row[key]=p.name;row[key+'_sha256']=sha(p)
        commands.append(dict(name=name,**row));save(out/'commands.json',commands);return row,stdout,stderr
    def build_call(name,argv):
        row,stdout,stderr=call(name,argv)
        if row['returncode']:print((stdout+stderr).decode(errors='replace')[-12000:],flush=True)
        require(row['returncode']==0,name+' failed; raw logs retained');print(name+' PASS',flush=True)
    sdk=build/'sdk';install=build/'install';sdl=build/'sdl';probe_build=build/'probe'
    build_call('configure-sdk',['cmake','-S',ROOT,'-B',sdk,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_INSTALL_PREFIX='+str(install),'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DGFX_ENABLE_D3D12=OFF','-DGFX_ENABLE_VULKAN=ON','-DGFX_BUILD_TESTS=OFF','-DGFX_BUILD_EXAMPLES=OFF','-DGFX_VULKAN_XCB=OFF','-DGFX_VULKAN_HEADERS='+str(ROOT/'.deps/Vulkan-Headers')])
    build_call('build-sdk',['cmake','--build',sdk,'--parallel','2']);build_call('install-sdk',['cmake','--install',sdk])
    build_call('configure-sdl',['cmake','-S',ROOT/'.deps/SDL','-B',sdl,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER=gcc-14','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DSDL_STATIC=ON','-DSDL_SHARED=OFF','-DSDL_GPU=ON','-DSDL_VULKAN=ON','-DSDL_TESTS=OFF','-DSDL_TEST_LIBRARY=OFF','-DSDL_X11=OFF','-DSDL_WAYLAND=OFF','-DSDL_KMSDRM=OFF','-DSDL_OPENGL=OFF','-DSDL_OPENGLES=OFF','-DSDL_OFFSCREEN=ON','-DSDL_UNIX_CONSOLE_BUILD=ON','-DSDL_AUDIO=OFF','-DSDL_CAMERA=OFF'])
    build_call('build-sdl',['cmake','--build',sdl,'--parallel','2'])
    build_call('configure-probe',['cmake','-S',ROOT/'benchmarks/linux_comparison','-B',probe_build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=g++-14','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DCMAKE_PREFIX_PATH='+str(install)+';'+str(sdl)])
    packages(cache(probe_build/'CMakeCache.txt'),install,sdl)
    require(cache(sdl/'CMakeCache.txt')['SDL_STATIC']=='ON' and cache(sdl/'CMakeCache.txt')['SDL_SHARED']=='OFF','Static SDL build required')
    build_call('build-probe',['cmake','--build',probe_build,'--parallel','2']);exe=probe_build/'linux_comparison'
    build_call('selection',[probe_build/'linux_comparison_selection'])
    plain=overrides(out,str(icd),str(layer),False);_,stdout,stderr=call('inventory',[exe,'inventory'],plain,90);inventory=json.loads(stdout)
    require(inventory['sdk']=='native' and inventory['logical_device_created'] is False and inventory['sample_count']==0,'Inventory must not create/timestamp workloads')
    uuid=a.device_uuid
    if uuid is None:
        require(len(inventory['inventory'])==1 and inventory['inventory'][0]['device_type']==4,'Software auto-discovery requires one native CPU device');uuid=inventory['inventory'][0]['device_uuid']
    pci=[]
    for p in sorted(pathlib.Path('/sys/bus/pci/devices').glob('*')):
        if (p/'class').read_text().strip() in ('0x030000','0x030200','0x038000'):pci.append(dict(address=p.name,vendor_id=int((p/'vendor').read_text(),16),device_id=int((p/'device').read_text(),16),driver=(p/'driver').resolve().name if (p/'driver').exists() else None))
    selected=select(inventory['inventory'],uuid,a.mode,pci);libraries=driver_libraries(icd,stderr.decode(errors='replace'),inventory['loader_path'])
    installed_library=install/'lib/libGraphicsSDKCore.a';require(sha(installed_library)==sha(sdk/'libGraphicsSDKCore.a'),'Installed SDK library differs')
    for p in (ROOT/'include').rglob('*.hpp'):require(sha(p)==sha(install/'include'/p.relative_to(ROOT/'include')),'Installed SDK header differs')
    frozen=[record(p) for p in sorted(set(sources+[runtime_path,layer,manifest,icd,exe,probe_build/'linux_comparison_selection',sdk/'libGraphicsSDKCore.a',installed_library,sdl/'libSDL3.a',out/'settings/vk_layer_settings.txt']+[pathlib.Path(x['path']) for x in libraries]))]
    for project in (sdk,sdl,probe_build):frozen += [record(project/n) for n in ('CMakeCache.txt','compile_commands.json','build.ninja')]
    r=dict(task='P3-BEN-07',status='running',source_commit=commit,mode=a.mode,device_uuid=uuid,inventory=inventory['inventory'],environment=inventory['environment'],selected=selected,pci=pci,
           SDK_source_fingerprint=sdk_fingerprint(ROOT),probe_fingerprint=fingerprint(),SDL_commit=sdl_pin,Vulkan_Headers_commit=HEADERS,probe=record(exe),icd=record(icd),loader_path=inventory['loader_path'],runtime_libraries=libraries,
           runtime_receipt_sha256=sha(out/'runtime-receipt.json'),validation_library=str(layer),inventory_sha256=sha(out/'inventory.stdout'),selection_sha256=sha(out/'selection.stdout'),output_directory=str(out),frozen_inputs=frozen,source_inputs=before,
           physical_probe_qualified=False,P3_BEN_07_completed=False,workload_accepted=False,performance_accepted=False,SDL_native_handle_verified=False,measurement_samples=0,runs=[])
    save(out/'receipt.json',r)
    try:
        for job in jobs(a.mode):
            require(unchanged(frozen) and unchanged(before),'Frozen inputs changed');env=overrides(out,str(icd),str(layer),job['validation'])
            if job['kind']=='missing-layer':env['VK_LAYER_PATH']=str(out/'empty')
            target='0'*32 if job['kind']=='wrong-uuid' else uuid
            row,stdout,stderr=call(job['name'],[exe,'probe',job['sdk'],target,str(int(job['validation'])),job['mode']],env,90);row['job']=job;r['runs'].append(row);save(out/'receipt.json',r)
            if job['kind']=='probe':
                require(row['returncode']==0 and not re.search(rb'Validation Error|SYNC-HAZARD|VUID-.*(?:ERROR|Error)',stdout+b'\n'+stderr),'Native probe failed; raw output retained');row['result']=validate_result(json.loads(stdout),job,r)
                actual=driver_libraries(icd,stderr.decode(errors='replace'),r['loader_path']);require(actual==libraries,'Actual loader/ICD changed')
            else:control(job,row['returncode'],stdout,stderr)
            save(out/'receipt.json',r);print(job['name']+' PASS',flush=True)
        pin(ROOT/'.deps/SDL',sdl_pin);pin(ROOT/'.deps/Vulkan-Headers',HEADERS);require(unchanged(frozen) and unchanged(before),'Final frozen input changed')
        r.update(status='completed',inputs_unchanged=True,physical_probe_qualified=a.mode=='physical',P3_BEN_07_completed=a.mode=='physical');save(out/'receipt.json',r);print(json.dumps(check(out,live=True)),flush=True)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:r.update(status='failed',error=str(e));save(out/'receipt.json',r);raise

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',choices=('run','check'));p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--mode',choices=('software-diagnostic','physical'),default='physical');p.add_argument('--icd',type=pathlib.Path);p.add_argument('--device-uuid');p.add_argument('--runtime-receipt',type=pathlib.Path);p.add_argument('--live',action='store_true');a=p.parse_args()
    if a.stage=='check':print(json.dumps(check(a.output,a.live),indent=2));return
    require(a.icd is not None and a.runtime_receipt is not None,'Explicit ICD and pinned Linux runtime receipt required');run(a)
if __name__=='__main__':main()
