"""Audit and run P3-CMP-02 identity/minimal reference probes; no timed workloads."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import time

from prepare_vulkan_comparison import ROOT, pin, read, record, require, save, sha, unchanged, validate_compiler_flags
from audit_licenses import parse_tlog, source_notices
from audit_vulkan import system_input

IDENTITY = ('device_name','device_uuid','driver_uuid','device_luid','native_vendor_id',
            'native_device_id','native_driver_version','native_api_version','queue_family','queue_index','queue_flags')


def source_fingerprint(root=ROOT):
    pin=read(root/'benchmarks/competitors/sources.lock.json')['llgl']['commit']
    files=sorted(list((root/'benchmarks/llgl_vulkan_probe').glob('*.cpp'))+
                 list((root/'benchmarks/llgl_vulkan_probe').glob('*.hpp'))+
                 [root/'benchmarks/llgl_vulkan_probe/CMakeLists.txt',root/'benchmarks/competitors/sources.lock.json'])
    identity='llgl|'+pin+'\n'+''.join(p.relative_to(root).as_posix()+'|'+sha(p)+'\n' for p in files)
    return hashlib.sha256(identity.encode()).hexdigest()


def validate_probe(row, gpu, fingerprint, validation, operation, reference=None):
    expected=dict(schema_version=1,task='P3-CMP-02',sdk='llgl',api='vulkan',configuration='Release',
                  LLGL_commit=read(ROOT/'benchmarks/competitors/sources.lock.json')['llgl']['commit'],
                  source_fingerprint=fingerprint,device_name=gpu,native_vendor_id=4318,
                  native_handle_verified=True,queue_index=0,validation=validation,operation=operation,
                  delivered_errors=0,performance_samples=0,gpu_ms=None,
                  workload_correctness_accepted=False,performance_accepted=False,instance_API_request_observed=False)
    require(all(k in row and type(row[k]) is type(v) and row[k]==v for k,v in expected.items()), 'Wrong/stale probe scope, target, or acceptance')
    for key,length in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:
        require(re.fullmatch('[0-9a-f]{'+str(length)+'}',row[key]) and int(row[key],16)!=0,'Missing native identity: '+key)
    for key in ('native_device_id','native_driver_version','native_api_version','queue_family','queue_flags','loader_api_version'):
        require(type(row[key]) is int and row[key]>=0,'Invalid native field: '+key)
    require(row['queue_flags']&1 and row['native_api_version']>=(1<<22|1<<12),'Graphics queue and Vulkan 1.1 required')
    require(type(row['verified_bytes']) is int and type(row['reference_rounds']) is int and
            row['verified_bytes']==(12288 if operation=='smoke' else 0) and row['reference_rounds']==(3 if operation=='smoke' else 0),'Incomplete minimal changing reference')
    require(bool(row['loaded_validation_layer'])==validation and row['runtime_modules'], 'Runtime/layer evidence missing')
    identity={k:row[k] for k in IDENTITY}
    require(reference is None or identity==reference,'Native identity differs between processes')
    return identity


def collect_inputs(build):
    locks=read(ROOT/'benchmarks/competitors/sources.lock.json')
    headers_pin=next(c['commit'] for c in read(ROOT/'tools/vulkan-validation.lock.json')['components'] if c['name']=='Vulkan-Headers')
    roots={ROOT/'.deps/LLGL':('llgl',locks['llgl']['commit']),ROOT/'.deps/Vulkan-Headers':('vulkan_headers',headers_pin)}
    tracked={r:pin(r,c) for r,(_,c) in roots.items()}
    logs=[p for p in (build/'build').rglob('*.read.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts
          and p.name.casefold() in ('cl.read.1.tlog','link.read.1.tlog','lib.read.1.tlog','rc.read.1.tlog')]
    require(any(p.name=='CL.read.1.tlog' for p in logs) and any(p.name=='link.read.1.tlog' for p in logs),'Missing compiler/linker evidence')
    compiler_commands=[p for p in (build/'build').rglob('CL.command.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts]
    require(len(compiler_commands)==3 and len(logs)==6,'Unexpected/missing compiler or linker target')
    for log in compiler_commands:validate_compiler_flags(log.read_bytes().decode('utf-16'))
    paths=set();link_libraries=[]
    for log in logs:
        entries=parse_tlog(log.read_bytes());paths.update(entries)
        if log.name=='link.read.1.tlog':link_libraries += [pathlib.Path(p).resolve() for p in entries if p.lower().endswith('.lib')]
    files=[];notices=set();unknown=[]
    for raw in sorted(paths,key=str.casefold):
        p=pathlib.Path(raw).resolve()
        if p.suffix.lower() in ('.obj','.pch','.pdb','.tlog'):continue
        component=None;selected=None
        for r,(sdk,commit) in roots.items():
            if p.is_relative_to(r):
                relative=p.relative_to(r).as_posix()
                require(relative.casefold() in tracked[r],'Untracked dependency input: '+str(p))
                text=p.read_text(encoding='utf-8-sig');blocks=source_notices(text)
                if sdk=='llgl':
                    require(relative.startswith(('include/','sources/')) and 'BSD' in '\n'.join(blocks),'Unreviewed LLGL license branch: '+relative)
                    selected='BSD-3-Clause'
                else:
                    licenses=re.findall(r'SPDX-License-Identifier:\s*([^\r\n*]+)',text)
                    require(licenses and all(x.strip() in ('Apache-2.0','MIT','Apache-2.0 OR MIT','MIT OR Apache-2.0') for x in licenses),'Unreviewed Vulkan header license: '+relative)
                    selected='MIT' if any('MIT' in x for x in licenses) else 'Apache-2.0'
                component=sdk;notices.update(blocks)
                files.append(dict(**record(p),component=component,selected_license=selected,dependency_commit=commit))
                break
        if component:continue
        if p.is_relative_to(ROOT/'benchmarks/llgl_vulkan_probe'):
            files.append(dict(**record(p),component='own_probe',selected_license='MIT'));continue
        if system_input(p):
            files.append(dict(**record(p),component='system_prerequisite',redistributed=False));continue
        if p.is_relative_to(build):
            require(p in (build/'vulkan-1.lib',build/'build/llgl/build/Release/LLGL.lib'), 'Unreviewed generated input: '+str(p))
            files.append(dict(**record(p),component='generated_build_input',redistributed=False));continue
        unknown.append(str(p))
    require(not unknown,'Unreviewed compiler inputs: '+str(unknown))
    for p in link_libraries:
        require(system_input(p) or p in (build/'vulkan-1.lib',build/'build/llgl/build/Release/LLGL.lib'),'Unreviewed linked library: '+str(p))
    cache=(build/'build/CMakeCache.txt').read_text(encoding='utf-8')
    for name,value in dict(LLGL_BUILD_RENDERER_VULKAN='ON',LLGL_BUILD_RENDERER_DIRECT3D12='OFF',LLGL_BUILD_RENDERER_DIRECT3D11='OFF',
            LLGL_BUILD_RENDERER_OPENGL='OFF',LLGL_BUILD_RENDERER_NULL='OFF',LLGL_ENABLE_DEBUG_LAYER='OFF',
            LLGL_VK_ENABLE_GPU_ASSISTED_VALIDATION='OFF',LLGL_VK_ENABLE_SPIRV_REFLECT='OFF').items():
        require(name+':BOOL='+value in cache.splitlines(),'Wrong build option: '+name)
    require(re.search(r'^CMAKE_CONFIGURATION_TYPES:[^=]+=Release$',cache,re.MULTILINE),'Wrong configuration')
    return dict(files=files,read_logs=[record(p) for p in sorted(logs)],compiler_commands=[record(p) for p in sorted(compiler_commands)],linked_libraries=[record(p) for p in sorted(set(link_libraries))],
                notice_blocks=sorted(notices),scope='Conservative actual compiler/linker reads; symbol reachability not inferred; no external binaries redistributed')


def run(build, output, gpu):
    require(os.name=='nt','Native probe requires Windows')
    build=build.resolve();output=output.resolve()
    require(build.is_relative_to(ROOT/'out') and output.is_relative_to(ROOT/'out') and not output.exists(),'Fresh out/ output required')
    output.mkdir(parents=True)
    inputs=collect_inputs(build);save(output/'inputs.json',inputs)
    exe=build/'build/Release/llgl_vulkan_probe.exe';fingerprint=source_fingerprint()
    require(fingerprint.encode() in exe.read_bytes(),'Probe fingerprint stale; reconfigure/rebuild current source')
    layer=ROOT/'out/vulkan-validation-runtime/build/Vulkan-ValidationLayers/layers/Release/VkLayer_khronos_validation.dll'
    require(layer.is_file(),'Pinned validation prerequisite missing')
    prerequisite=ROOT/'out/vulkan-validation-runtime/receipt.json'
    prior=read(prerequisite)
    require(prior['lock_sha256']==sha(ROOT/'tools/vulkan-validation.lock.json'),'Validation prerequisite lock differs')
    require(all(sha(ROOT/a['path'])==a['sha256'] for a in prior['artifacts']),'Validation prerequisite artifact differs')
    settings=output/'settings';settings.mkdir();empty=output/'empty';empty.mkdir()
    (settings/'vk_layer_settings.txt').write_text('khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n',encoding='utf-8',newline='\n')
    base={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','GFX_','SDL_'))}
    plain=dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(empty),VK_LAYER_SETTINGS_PATH=str(empty))
    validated={**plain,'VK_LAYER_PATH':str(layer.parent),'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','VK_LAYER_SETTINGS_PATH':str(settings),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true'}
    save(output/'environment.json',dict(removed_inherited_keys=sorted(set(os.environ)-set(base)),plain=plain,validation=validated))
    policy=ROOT/'benchmarks/llgl_vulkan_probe/runtime-policy.json'
    allowed_external={pathlib.Path(x['path']).resolve():x for x in read(policy)['external_modules']}
    paths=[exe,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',layer,settings/'vk_layer_settings.txt',
           ROOT/'benchmarks/llgl_vulkan_probe/main.cpp',ROOT/'benchmarks/llgl_vulkan_probe/CMakeLists.txt',policy,prerequisite,
           pathlib.Path(__file__),ROOT/'tools/prepare_llgl_vulkan.py',ROOT/'tools/prepare_vulkan_comparison.py',ROOT/'tools/audit_licenses.py',ROOT/'tools/audit_vulkan.py',
           ROOT/'benchmarks/competitors/sources.lock.json',ROOT/'tools/vulkan-validation.lock.json',build/'build/CMakeCache.txt']
    paths += [pathlib.Path(row['path']) for group in ('files','read_logs','compiler_commands','linked_libraries') for row in inputs[group]]
    frozen=[record(p) for p in sorted(set(paths))]
    jobs=[('plain-identity',False,'identity',gpu,0,{}),('plain-smoke',False,'smoke',gpu,0,{}),('validation-smoke',True,'smoke',gpu,0,{}),
          ('wrong-target',False,'smoke',gpu+' invalid target',1,{}),('missing-layer',True,'smoke',gpu,1,{'VK_LAYER_PATH':str(empty)})]
    commands=[];reference=None;runtime={}
    for name,validation,operation,target,expected,overrides in jobs:
        require(unchanged(frozen),'Inputs changed during probe')
        env={**base,**(validated if validation else plain),**overrides}
        start=time.monotonic();args=[str(exe),target,'validation' if validation else 'plain',operation]
        p=subprocess.run(args,cwd=exe.parent,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
        (output/(name+'.stdout')).write_bytes(p.stdout);(output/(name+'.stderr')).write_bytes(p.stderr)
        commands.append(dict(name=name,argv=args,returncode=p.returncode,expected=expected,seconds=time.monotonic()-start,overrides=overrides))
        save(output/'commands.json',commands)
        require(p.returncode==expected,'Probe/control failed: '+name)
        if expected:
            token=b'Wrong target GPU' if name=='wrong-target' else b'Required validation layer absent'
            require(not p.stdout and token in p.stderr,'Control failed for an unrelated reason: '+name)
        else:
            row=json.loads(p.stdout);reference=validate_probe(row,gpu,fingerprint,validation,operation,reference)
            require(pathlib.Path(row['loader_path']).resolve()==pathlib.Path(os.environ['WINDIR'])/'System32/vulkan-1.dll','Wrong system loader')
            for key,dll in [('LLGL_path','LLGL.dll'),('backend_path','LLGL_Vulkan.dll')]:
                require(pathlib.Path(row[key]).resolve()==exe.parent/dll,'Wrong loaded LLGL artifact')
            require(not validation or pathlib.Path(row['loaded_validation_layer']).resolve()==layer.resolve(),'Wrong loaded validation layer')
            require(not re.search(rb'Validation Error|SYNC-HAZARD',p.stderr),'Native validation error')
            for name in row['runtime_modules']:
                path=pathlib.Path(name).resolve()
                external=allowed_external.get(path)
                require(path.is_relative_to(pathlib.Path(os.environ['WINDIR'])) or path in (exe,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',layer.resolve()) or
                        external is not None and sha(path)==external['sha256'], 'Unreviewed runtime module: '+str(path))
                runtime[str(path)]=dict(**record(path),component=external['component'] if external else 'system_driver_or_audited_probe',redistributed=False)
    require(unchanged(frozen),'Inputs changed during probe')
    save(output/'runtime-inputs.json',dict(files=list(runtime.values())))
    save(output/'receipt.json',dict(schema_version=1,task='P3-CMP-02',status='native_probe_passed',identity=reference,source_fingerprint=fingerprint,
        commands=commands,source_inputs=frozen,inputs_unchanged=True,performance_samples=0,workload_correctness_accepted=False,performance_accepted=False,
        validation_scope='Pinned layer actually loaded; core+sync settings requested; delivered errors zero. Full workload validation and feature/error controls remain CMP-03..06.',
        instance_API_request_observed=False,minimum_reference_bytes=24576,next_task='P3-CMP-03'))
    print('P3-CMP-02 native probe passed; 3 processes + 2 rejection controls; no performance samples')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root',type=pathlib.Path,required=True)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--gpu-name',required=True)
    args=parser.parse_args();run(args.build_root,args.output,args.gpu_name)


if __name__=='__main__':main()
