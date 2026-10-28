"""P3-CMP-03 correctness-only LLGL Vulkan upload runner and input audit."""
import argparse
import array
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import time

from prepare_vulkan_comparison import ROOT, pin, read, record, require, save, sha, unchanged, validate_compiler_flags
from audit_licenses import parse_tlog, source_notices
from audit_vulkan import system_input
from llgl_vulkan_probe import IDENTITY

SHAPES=dict(odd=(65,37,4),upload1080=(1920,1080,4),upload4k=(3840,2160,4),upload4k16f=(3840,2160,8))
CONTRACT='vulkan_upload_cpu_payload_to_texture_fence_complete_each_sample_readback_v1'
PIN='649c0cc8fa608312c5e4b94dae409a5859fc7553'


def source_files(root=ROOT):
    return sorted(list((root/'benchmarks/llgl_vulkan_upload').glob('*.cpp'))+list((root/'benchmarks/llgl_vulkan_upload').glob('*.hpp'))+
                  [root/'benchmarks/llgl_vulkan_upload/CMakeLists.txt',root/'benchmarks/competitors/sources.lock.json',root/'benchmarks/vulkan_upload/main.cpp',root/'benchmarks/phase3_comparison/upload.json'],key=lambda p:p.as_posix())


def fingerprint(root=ROOT,profile='upstream'):
    require(profile in ('upstream','transfer-src-candidate'),'Unknown dependency profile')
    return hashlib.sha256(('llgl|'+PIN+'\ndependency_profile|'+profile+'\n'+''.join(p.relative_to(root).as_posix()+'|'+sha(p)+'\n' for p in source_files(root))).encode()).hexdigest()


def body(text):
    start=text.index('{',text.index('void payload('));depth=0
    for i in range(start,len(text)):
        depth+=int(text[i]=='{')-int(text[i]=='}')
        if not depth:return text[start:i+1].replace('\r\n','\n')
    raise ValueError('Payload function is incomplete')


def verify_payload_source(root=ROOT):
    require(body((root/'benchmarks/vulkan_upload/main.cpp').read_text())==body((root/'benchmarks/llgl_vulkan_upload/Payload.hpp').read_text()),'Payload differs from SDK/SDL baseline')
    plan=read(root/'benchmarks/phase3_comparison/contract.json')
    require(plan['profiles']['upload']['shapes']=={n:list(s) for n,s in SHAPES.items()},'Upload shape contract drift')


def payload_reference(shape,seed):
    w,h,bpp=shape
    halves=(0,0x3400,0x3800,0x3c00,0xb400,0x4000,0x4200,0x0400)
    table=[sum(halves[(v>>(k*3))&7]<<(k*16) for k in range(4)) for v in range(4096)] if bpp==8 else None
    def pixels():
        for i in range(w*h):
            v=(i*1315423911+seed*2654435761)&0xffffffff;v^=v>>16
            yield v if table is None else table[v&4095]
    words=array.array('I' if bpp==4 else 'Q',pixels())
    require(words.itemsize==bpp,'Unexpected reference word size')
    if sys.byteorder!='little':words.byteswap()
    return hashlib.sha256(words.tobytes()).hexdigest()


def validate(row,name,validation,fp,reference_hashes,identity=None,profile='upstream'):
    control=name in ('core-control','sync-control')
    expected=dict(schema_version=1,task='P3-CMP-03',sdk='llgl',api='vulkan',configuration='Release',contract=CONTRACT,
        source_fingerprint=fp,LLGL_commit=PIN,dependency_profile=profile,validation=validation,scenario=name,control=control,
        native_handle_verified=True,native_vendor_id=4318,queue_index=0,instance_API_request_observed=False,
        submitted_control_work=0,row_stride=0,layer_stride=0,performance_samples=0,samples_ms=[],gpu_ms=None,performance_accepted=False,native_workload_shortcuts=False)
    require(all(k in row and type(row[k]) is type(v) and row[k]==v for k,v in expected.items()),'Upload scope/identity/mode drift')
    for key,size in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:
        require(re.fullmatch('[0-9a-f]{'+str(size)+'}',row[key]) and int(row[key],16),'Native identity absent')
    for key in ('native_device_id','native_driver_version','native_api_version','queue_family','queue_flags','loader_api_version'):
        require(type(row[key]) is int and row[key]>=0,'Invalid native identity field')
    require(row['queue_flags']&1 and row['native_api_version']>=((1<<22)|(1<<12)),'Graphics queue/Vulkan 1.1 required')
    current={k:row[k] for k in IDENTITY}
    require(identity is None or current==identity,'Native device/driver/queue differs')
    require(bool(row['loaded_validation_layer'])==validation and row['runtime_modules'],'Actual layer/runtime evidence absent')
    if control:
        token='VUID-VkPhysicalDeviceProperties2-sType-sType' if name=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE'
        require(validation and row['control_diagnostic']==token and type(row['delivered_errors']) is int and row['delivered_errors']>0,'Validation control not observed')
        values=dict(width=0,height=0,texel_bytes=0,warmup=0,sample_count=0,verified_images=0,verified_bytes=0,readback_verified=False,payload_sha256=[])
    else:
        w,h,bpp=SHAPES[name]
        values=dict(width=w,height=h,texel_bytes=bpp,warmup=1,sample_count=3,verified_images=4,verified_bytes=w*h*bpp*4,
                    readback_verified=True,payload_sha256=reference_hashes[name],delivered_errors=0,control_diagnostic='')
    require(all(k in row and type(row[k]) is type(v) and row[k]==v for k,v in values.items()),'Incomplete/changing payload, validation error or reference drift')
    return current


def collect(build,dependency):
    prior=read(dependency/'probe/receipt.json')
    require(prior['task']=='P3-CMP-02' and prior['status']=='native_probe_passed' and unchanged(prior['source_inputs']),'Audited CMP-02 source/artifact dependency changed')
    locks=read(ROOT/'benchmarks/competitors/sources.lock.json');require(locks['llgl']['commit']==PIN,'LLGL pin drift')
    dep=pathlib.Path(next(r['path'] for r in prior['source_inputs'] if r['path'].endswith('/build/Release/LLGL.dll'))).parents[2]
    roots={ROOT/'.deps/LLGL':('llgl',PIN),ROOT/'.deps/Vulkan-Headers':('vulkan_headers',next(c['commit'] for c in read(ROOT/'tools/vulkan-validation.lock.json')['components'] if c['name']=='Vulkan-Headers'))}
    tracked={p:pin(p,commit) for p,(_,commit) in roots.items()}
    logs=[p for p in build.rglob('*.read.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts and p.name in ('CL.read.1.tlog','link.read.1.tlog')]
    commands=[p for p in build.rglob('CL.command.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts]
    require(len(logs)==2 and len(commands)==1,'Missing/unexpected consumer compiler/linker evidence')
    validate_compiler_flags(commands[0].read_bytes().decode('utf-16'))
    paths=set(x for p in logs for x in parse_tlog(p.read_bytes()));files=[]
    imports=(dep/'vulkan-1.lib',dep/'build/llgl/build/Release/LLGL.lib')
    for raw in sorted(paths,key=str.casefold):
        path=pathlib.Path(raw).resolve()
        if path.suffix.lower() in ('.obj','.pch','.pdb','.tlog'):continue
        component=None
        for base,(sdk,commit) in roots.items():
            if path.is_relative_to(base):
                rel=path.relative_to(base).as_posix();require(rel.casefold() in tracked[base],'Untracked dependency input')
                text=path.read_text(encoding='utf-8-sig')
                require('BSD' in '\n'.join(source_notices(text)) if sdk=='llgl' else 'SPDX-License-Identifier: Apache-2.0' in text,'Unreviewed dependency license')
                component=sdk;break
        if component:pass
        elif path.is_relative_to(ROOT/'benchmarks/llgl_vulkan_upload'):component='own_upload'
        elif path in imports:component='audited_CMP02_import_library'
        elif system_input(path):component='system_prerequisite'
        else:raise ValueError('Unreviewed consumer input: '+str(path))
        files.append(dict(**record(path),component=component))
    return dict(files=files,read_logs=[record(p) for p in logs],compiler_commands=[record(p) for p in commands],dependency_receipt=record(dependency/'probe/receipt.json'),dependency_build_root=dep.as_posix(),
                scope='Actual new consumer reads plus byte-unchanged audited CMP-02 LLGL/Vulkan inputs, no binary redistribution'),prior,dep


def candidate_inputs(candidate,dep):
    from prepare_llgl_transfer_src_candidate import patch,RELATIVE
    candidate=candidate.resolve();metadata=read(candidate/'profile.json')
    require(metadata['profile']=='transfer-src-candidate' and metadata['upstream_commit']==PIN and metadata['unmodified_upstream'] is metadata['adopted_as_comparison_baseline'] is metadata['performance_accepted'] is False,'Candidate must remain an explicitly modified non-adopted profile')
    original=subprocess.check_output(['git','--no-optional-locks','-C',str(ROOT/'.deps/LLGL'),'show',PIN+':'+RELATIVE])
    require(original==(candidate/'VKCommandContext.original.cpp').read_bytes() and patch(original.decode()).encode()==(candidate/'VKCommandContext.cpp').read_bytes(),'Candidate differs from reviewed two-switch patch')
    for key in ('original','overlay','patch'):require(unchanged([metadata[key]]),'Candidate source/patch hash changed')
    logs=[p for p in (candidate/'build').rglob('*.read.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts and p.name in ('CL.read.1.tlog','link.read.1.tlog')]
    commands=[p for p in (candidate/'build').rglob('CL.command.1.tlog') if 'Release' in p.parts and 'CMakeFiles' not in p.parts]
    require(len(logs)==4 and len(commands)==2,'Candidate target compiler/linker evidence absent')
    for p in commands:validate_compiler_flags(p.read_bytes().decode('utf-16'))
    files=[];paths=set(x for p in logs for x in parse_tlog(p.read_bytes()))
    roots={ROOT/'.deps/LLGL':pin(ROOT/'.deps/LLGL',PIN),ROOT/'.deps/Vulkan-Headers':pin(ROOT/'.deps/Vulkan-Headers',next(c['commit'] for c in read(ROOT/'tools/vulkan-validation.lock.json')['components'] if c['name']=='Vulkan-Headers'))}
    for raw in sorted(paths,key=str.casefold):
        p=pathlib.Path(raw).resolve()
        if p.suffix.lower() in ('.obj','.pch','.pdb','.tlog'):continue
        kind=None
        for base,tracked in roots.items():
            if p.is_relative_to(base):require(p.relative_to(base).as_posix().casefold() in tracked,'Untracked candidate dependency');kind='unchanged_pinned_input';break
        if kind:pass
        elif p==candidate/'VKCommandContext.cpp':kind='explicitly_modified_BSD_LLGL_overlay'
        elif p in (dep/'vulkan-1.lib',candidate/'build/llgl/build/Release/LLGL.lib'):kind='generated_import_library'
        elif system_input(p):kind='system_prerequisite'
        else:raise ValueError('Unreviewed candidate input: '+str(p))
        files.append(dict(**record(p),component=kind))
    return dict(profile=metadata,files=files,read_logs=[record(p) for p in logs],compiler_commands=[record(p) for p in commands])


def run(build,output,gpu,dependency,candidate=None):
    require(os.name=='nt','Windows native profile required')
    build=build.resolve();output=output.resolve();dependency=dependency.resolve()
    require(build.is_relative_to(ROOT/'out') and output.is_relative_to(ROOT/'out') and not output.exists(),'Fresh out/ output required')
    verify_payload_source();inputs,prior,dep=collect(build,dependency)
    output.mkdir(parents=True);save(output/'inputs.json',inputs)
    profile='transfer-src-candidate' if candidate else 'upstream';runtime_root=dep/'build/Release'
    candidate_audit=candidate_inputs(candidate,dep) if candidate else None
    if candidate:runtime_root=candidate.resolve()/'build/llgl/build/Release';save(output/'candidate-inputs.json',candidate_audit)
    fp=fingerprint(profile=profile);exe=build/'Release/llgl_vulkan_upload.exe';require(fp.encode() in exe.read_bytes(),'Stale upload executable')
    for dll in ('LLGL.dll','LLGL_Vulkan.dll'):require(sha(exe.parent/dll)==sha(runtime_root/dll),'Wrong dependency DLL copy')
    layer=ROOT/'out/vulkan-validation-runtime/build/Vulkan-ValidationLayers/layers/Release/VkLayer_khronos_validation.dll'
    reference={}
    for n,s in SHAPES.items():
        reference[n]=[payload_reference(s,i) for i in range(1,5)];print('CPU reference',n,flush=True)
    save(output/'payload-reference.json',dict(algorithm='Independent stdlib little-endian implementation of pinned SDK/SDL uint32 and finite half payload',seeds=[1,2,3,4],hashes=reference))
    settings=output/'settings';settings.mkdir();off=output/'sync-off';off.mkdir();empty=output/'empty';empty.mkdir()
    (settings/'vk_layer_settings.txt').write_text('khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n',encoding='utf-8',newline='\n')
    (off/'vk_layer_settings.txt').write_text('khronos_validation.validate_sync = false\nkhronos_validation.syncval_submit_time_validation = false\nkhronos_validation.duplicate_message_limit = 0\n',encoding='utf-8',newline='\n')
    base={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','GFX_','SDL_'))}
    plain=dict(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(empty),VK_LAYER_SETTINGS_PATH=str(empty))
    validated={**plain,'VK_LAYER_PATH':str(layer.parent),'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','VK_LAYER_SETTINGS_PATH':str(settings),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true'}
    save(output/'environment.json',dict(removed_inherited_keys=sorted(set(os.environ)-set(base)),plain=plain,validation=validated))
    paths=source_files()+[pathlib.Path(__file__),exe,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',build/'CMakeCache.txt',layer,settings/'vk_layer_settings.txt',off/'vk_layer_settings.txt']
    paths += [pathlib.Path(r['path']) for group in ('files','read_logs','compiler_commands') for r in inputs[group]]
    paths += [pathlib.Path(r['path']) for r in prior['source_inputs']]
    if candidate_audit:
        paths += [pathlib.Path(r['path']) for group in ('files','read_logs','compiler_commands') for r in candidate_audit[group]]
        paths += [candidate.resolve()/n for n in ('profile.json','VKCommandContext.original.cpp','VKCommandContext.cpp','transfer-src.patch')]
    frozen=[record(p) for p in sorted(set(paths))]
    jobs=[(n+'-'+mode,n,mode=='validation',gpu,0,{}) for n in SHAPES for mode in ('plain','validation')]
    jobs += [('core-control','core-control',True,gpu,0,{}),('sync-control','sync-control',True,gpu,0,{}),
             ('missing-layer','odd',True,gpu,1,{'VK_LAYER_PATH':str(empty)}),('wrong-target','odd',False,gpu+' invalid target',1,{}),
             ('corrupt-readback','corrupt-readback',False,gpu,1,{}),('sync-disabled','sync-control',True,gpu,1,{'VK_LAYER_SETTINGS_PATH':str(off),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'false'})]
    commands=[];identity=None;runtime={}
    try:
        for name,scenario,validation,target,expected,overrides in jobs:
            require(unchanged(frozen),'Input changed during correctness run')
            argv=[str(exe),target,'validation' if validation else 'plain',scenario]
            start=time.monotonic();p=subprocess.run(argv,cwd=exe.parent,env={**base,**(validated if validation else plain),**overrides},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90)
            (output/(name+'.stdout')).write_bytes(p.stdout);(output/(name+'.stderr')).write_bytes(p.stderr)
            commands.append(dict(name=name,scenario=scenario,validation=validation,argv=argv,returncode=p.returncode,expected=expected,seconds=time.monotonic()-start,overrides=overrides));save(output/'commands.json',commands)
            require(p.returncode==expected,'Unexpected native exit: '+name)
            if expected:
                token={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','corrupt-readback':b'Full changing upload readback mismatch','sync-disabled':b'Expected validation control diagnostic absent'}[name]
                require(not p.stdout and token in p.stderr,'Rejection failed for another reason: '+name)
            else:
                row=json.loads(p.stdout);identity=validate(row,scenario,validation,fp,reference,identity,profile)
                require(row['device_name']==gpu,'Wrong GPU')
                require(pathlib.Path(row['loader_path']).resolve()==pathlib.Path(os.environ['WINDIR'])/'System32/vulkan-1.dll','Wrong loader')
                require(not validation or pathlib.Path(row['loaded_validation_layer']).resolve()==layer.resolve(),'Wrong VVL module')
                for key,dll in [('LLGL_path','LLGL.dll'),('backend_path','LLGL_Vulkan.dll')]:require(pathlib.Path(row[key]).resolve()==exe.parent/dll,'Wrong loaded LLGL module')
                if scenario in SHAPES:require(not re.search(rb'Validation Error|SYNC-HAZARD',p.stderr),'Native workload validation error')
                for entry in row['runtime_modules']:
                    path=pathlib.Path(entry).resolve();require(path.is_relative_to(pathlib.Path(os.environ['WINDIR'])) or path in (exe,exe.parent/'LLGL.dll',exe.parent/'LLGL_Vulkan.dll',layer.resolve()),'Unreviewed runtime module: '+str(path))
                    if str(path) not in runtime:runtime[str(path)]=record(path)
            print(name,'PASS',flush=True)
        require(unchanged(frozen),'Inputs changed during run')
        save(output/'runtime-inputs.json',dict(files=list(runtime.values()),redistributed=False))
        save(output/'receipt.json',dict(schema_version=1,task='P3-CMP-03',status='candidate_correctness_passed' if candidate else 'correctness_passed',dependency_profile=profile,adopted_as_comparison_baseline=False,identity=identity,source_fingerprint=fp,commands=commands,source_inputs=frozen,inputs_unchanged=True,
            verified_images=32,verified_bytes=sum(w*h*bpp*8 for w,h,bpp in SHAPES.values()),performance_samples=0,performance_accepted=False,core_and_sync_controls_passed=True,
            upload_API='MapBuffer/UnmapBuffer -> CopyTextureFromBuffer -> Submit(command/fence)/WaitFence',readback_API='CopyBufferFromTexture -> Submit(command/fence)/WaitFence -> ReadBuffer',
            differences=['LLGL default CPU-map internal staging plus GPU-local buffer copy on Unmap','zero row/layer stride tight layouts vs SDK padded layout','LLGL loader-max API request is source-derived, not intercepted'],next_task='P3-CMP-04'))
    except Exception:
        save(output/'failure.json',dict(task='P3-CMP-03',completed_commands=len(commands),correctness_accepted=False,performance_accepted=False));raise


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-root',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--gpu-name',required=True)
    p.add_argument('--dependency-evidence',type=pathlib.Path,default=ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-vulkan');p.add_argument('--candidate-build',type=pathlib.Path);a=p.parse_args();run(a.build_root,a.output,a.gpu_name,a.dependency_evidence,a.candidate_build)


if __name__=='__main__':main()
