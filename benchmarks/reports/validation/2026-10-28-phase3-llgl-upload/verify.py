"""Offline replay of the upstream defect and separately labeled correction candidate."""
import hashlib
import json
import pathlib
import re
import zipfile

HERE=pathlib.Path(__file__).resolve().parent
PIN='649c0cc8fa608312c5e4b94dae409a5859fc7553'
SHAPES=dict(odd=(65,37,4),upload1080=(1920,1080,4),upload4k=(3840,2160,4),upload4k16f=(3840,2160,8))

def require(value,message):
    if not value:raise ValueError(message)

def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def sha(data):return hashlib.sha256(data).hexdigest()

def source_fingerprint(path,profile=None):
    with zipfile.ZipFile(path) as z:
        names=z.namelist();require(len(set(names))==len(names),'Duplicate source entry')
        require(all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts and ':' not in n and '\\' not in n for n in names),'Unsafe source ZIP')
        selected=sorted(n for n in names if n.startswith('benchmarks/llgl_vulkan_upload/') and '/' not in n[len('benchmarks/llgl_vulkan_upload/'):] and n.endswith(('.cpp','.hpp','CMakeLists.txt')))
        selected=sorted(selected+['benchmarks/competitors/sources.lock.json','benchmarks/vulkan_upload/main.cpp','benchmarks/phase3_comparison/upload.json'])
        identity='llgl|'+PIN+'\n'+('dependency_profile|'+profile+'\n' if profile else '')
        identity+=''.join(n+'|'+sha(z.read(n))+'\n' for n in selected)
        require(json.loads(z.read('benchmarks/competitors/sources.lock.json'))['llgl']['commit']==PIN,'Source pin differs')
        return sha(identity.encode())

def check_row(row,name,validation,fp,hashes,identity,profile=None):
    require(row['task']=='P3-CMP-03' and row['api']=='vulkan' and row['sdk']=='llgl' and row['configuration']=='Release' and row['source_fingerprint']==fp and row['LLGL_commit']==PIN,'Wrong/stale native row')
    require(row['scenario']==name and row['validation'] is validation and row['native_handle_verified'] is True and
            row['native_vendor_id']==4318 and row['queue_index']==0 and row['queue_flags']&1,'Wrong native target/mode')
    require(all(row[k]==v for k,v in identity.items()),'Native identity differs between processes')
    for key,size in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:require(re.fullmatch('[0-9a-f]{'+str(size)+'}',row[key]) and int(row[key],16),'Empty native identity')
    require(bool(row['loaded_validation_layer'])==validation and row['runtime_modules'],'Actual layer/runtime absent')
    if profile:require(row['dependency_profile']==profile,'Modified candidate relabeled as upstream')
    require(row['performance_samples']==0 and row['samples_ms']==[] and row['gpu_ms'] is None and row['performance_accepted'] is row['native_workload_shortcuts'] is row['instance_API_request_observed'] is False,'Correctness promoted to performance/native shortcuts')
    if name in SHAPES:
        w,h,bpp=SHAPES[name]
        require((row['width'],row['height'],row['texel_bytes'])==(w,h,bpp) and row['verified_bytes']==w*h*bpp*4 and row['verified_images']==4 and row['warmup']==1 and row['sample_count']==3,'Incomplete image verification')
        require(row['payload_sha256']==hashes[name] and len(set(hashes[name]))==4 and row['readback_verified'] is True and row['delivered_errors']==0,'Changing whole-image reference or validation failed')
    else:
        token='VUID-VkPhysicalDeviceProperties2-sType-sType' if name=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE'
        require(row['control'] is True and row['control_diagnostic']==token and row['delivered_errors']>0 and row['submitted_control_work']==0 and row['verified_images']==row['verified_bytes']==0,'Native validation control not observed safely')

def verify(root=HERE):
    manifest=read(root/'manifest.json');require(manifest['task']=='P3-CMP-03' and manifest['performance_samples']==0,'Wrong archive scope')
    for item in manifest['files']:
        p=pathlib.PurePosixPath(item['path']);require(not p.is_absolute() and '..' not in p.parts and '__pycache__' not in p.parts and p.suffix!='.pyc' and ':' not in str(p) and '\\' not in str(p),'Unsafe/cache archive entry')
        data=(root/p).read_bytes();require(sha(data)==item['sha256'] and len(data)==item['bytes'],'Evidence hash differs: '+str(p))
    upstream=read(root/'upstream/receipt.json');require(upstream['status']=='fixed_pin_validation_defect_reproduced' and upstream['correctness_accepted'] is upstream['performance_accepted'] is False,'Upstream defect was silently accepted')
    original_fp=source_fingerprint(root/'upstream/source-original.zip');require(original_fp==upstream['source_fingerprint'],'Original source identity differs')
    refs=read(root/'payload-reference.json')['hashes'];require(set(refs)==set(SHAPES),'Missing reference scenario')
    for name in SHAPES:
        require(len(refs[name])==4 and all(re.fullmatch('[0-9a-f]{64}',h) for h in refs[name]),'Missing payload digest')
        row=read(root/'upstream'/(name+'-plain.stdout'));check_row(row,name,False,original_fp,refs,upstream['identity'])
        require(not (root/'upstream'/(name+'-validation.stdout')).read_bytes(),'Upstream validation unexpectedly succeeded')
        require(b'SYNC-HAZARD-READ-AFTER-WRITE' in (root/'upstream'/(name+'-validation.stderr')).read_bytes(),'Original synchronization defect missing')
    old_jobs=read(root/'upstream/commands.json');require(len(old_jobs)==11 and old_jobs==upstream['commands'] and all(j['returncode']==j['expected'] for j in old_jobs),'Upstream reproduction jobs incomplete')
    candidate=read(root/'candidate/receipt.json');require(candidate['status']=='candidate_correctness_passed' and candidate['dependency_profile']=='transfer-src-candidate' and candidate['adopted_as_comparison_baseline'] is False and candidate['performance_accepted'] is False and candidate['performance_samples']==0 and candidate['inputs_unchanged'] is True,'Candidate scope drift')
    fp=source_fingerprint(root/'source.zip','transfer-src-candidate');require(fp==candidate['source_fingerprint'],'Candidate source identity differs')
    jobs=read(root/'candidate/commands.json');require(len(jobs)==14 and jobs==candidate['commands'],'Candidate matrix incomplete')
    rejections={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','corrupt-readback':b'Full changing upload readback mismatch','sync-disabled':b'Expected validation control diagnostic absent'}
    require([j['name'] for j in jobs[:8]]==[n+'-'+mode for n in SHAPES for mode in ('plain','validation')],'Candidate workload order/cases differ')
    for job in jobs:
        name=job['name'];data=(root/'candidate'/(name+'.stdout')).read_bytes();err=(root/'candidate'/(name+'.stderr')).read_bytes()
        require(job['returncode']==job['expected']==(1 if name in rejections else 0),'Candidate unexpected exit')
        if name in rejections:require(not data and rejections[name] in err,'Unrelated rejection failure')
        else:
            check_row(json.loads(data),job['scenario'],job['validation'],fp,refs,candidate['identity'],'transfer-src-candidate')
            if job['scenario'] in SHAPES:require(not re.search(rb'Validation Error|SYNC-HAZARD',err),'Candidate native workload validation error')
    require(candidate['identity']==upstream['identity'] and candidate['verified_images']==32 and candidate['verified_bytes']==862694560 and candidate['core_and_sync_controls_passed'] is True,'Different target or incomplete corrected reference')
    audit=read(root/'candidate/candidate-inputs.json');require(sum(r['component']=='explicitly_modified_BSD_LLGL_overlay' for r in audit['files'])==1 and len(audit['read_logs'])==4 and len(audit['compiler_commands'])==2,'Candidate compiler input audit absent')
    original=(root/'patch/VKCommandContext.original.cpp').read_text();changed=(root/'patch/VKCommandContext.cpp').read_text()
    expected=original
    for access,stage in [('srcAccessMask','srcStageMask'),('dstAccessMask','dstStageMask')]:
        anchor='        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:\n            barrier.'+access+' = VK_ACCESS_SHADER_READ_BIT;'
        insertion='        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:\n            barrier.'+access+' = VK_ACCESS_TRANSFER_READ_BIT;\n            '+stage+' = VK_PIPELINE_STAGE_TRANSFER_BIT;\n            break;\n\n'
        require(expected.count(anchor)==1,'Original switch source drift')
        expected=expected.replace(anchor,insertion+anchor)
    require(changed==expected and len(changed.splitlines())-len(original.splitlines())==10,'Unexpected patch contents')
    decision=read(root/'decision/llgl-transfer-src-profile.json')
    require(decision['status']=='approved_for_explicit_comparisons' and decision['human_decision']=='명시적 패치 프로필을 허용해 계속 진행' and
            decision['verified_correctness']==['upload'] and decision['unmodified_upstream'] is decision['performance_accepted'] is False,'Explicit human profile decision missing')
    evidence=dict(scope_decision='decision/PHASE3_LLGL_UPLOAD_SCOPE.md',patch='patch/transfer-src.patch',original_source='patch/VKCommandContext.original.cpp',
                  modified_source='patch/VKCommandContext.cpp',upstream_failure='upstream/receipt.json',candidate_correctness='candidate/receipt.json')
    for key,name in evidence.items():require(decision['evidence'][key]['sha256']==sha((root/name).read_bytes()),'Approved profile evidence hash differs')
    print('PASS saved CMP-03: original defect retained; approved explicit profile 32 full images / 862694560 bytes; no performance acceptance')
    return candidate

if __name__=='__main__':verify()
