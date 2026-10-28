"""Replay saved draw metadata, references and native observations without GPU execution."""
import hashlib
import json
import pathlib
import re
import zipfile

HERE=pathlib.Path(__file__).resolve().parent
WORK=dict(draw1=(1,1),draw1000=(1000,1),instanced1000=(1,1000))
EXTRA=['benchmarks/llgl_vulkan_upload/Context.hpp','benchmarks/vulkan_draw/Frame.hpp','benchmarks/phase3_comparison/draw.json','benchmarks/competitors/sources.lock.json',
       'tests/fixtures/SpirvFixtures.hpp','tests/fixtures/spirv/fullscreen.spvasm','tests/fixtures/spirv/red.spvasm','tests/fixtures/spirv/green.spvasm']

def require(ok,message):
    if not ok:raise ValueError(message)
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(data):return hashlib.sha256(data).hexdigest()
def frame(i):return b''.join(bytes(((0,255,0,255) if i%2 else (255,0,0,255)) if 5<=x<60 and 3<=y<34 else ((0,255,255,255) if i%2 else (0,0,255,255))) for y in range(37) for x in range(65))

def verify(root=HERE):
    manifest=read(root/'manifest.json');require(manifest['task']=='P3-CMP-04' and manifest['performance_samples']==0,'Wrong archive scope')
    for item in manifest['files']:
        p=pathlib.PurePosixPath(item['path']);require(not p.is_absolute() and '..' not in p.parts and '__pycache__' not in p.parts and p.suffix!='.pyc' and ':' not in str(p) and '\\' not in str(p),'Unsafe/cache evidence path')
        data=(root/p).read_bytes();require(sha(data)==item['sha256'] and len(data)==item['bytes'],'Evidence hash differs: '+str(p))
    receipt=read(root/'native/receipt.json');require(receipt['task']=='P3-CMP-04' and receipt['status']=='draw_correctness_passed' and receipt['profile_id']=='llgl-fixed-pin-transfer-src-v1' and receipt['inputs_unchanged'] is True and receipt['performance_samples']==0 and receipt['performance_accepted'] is False,'Receipt scope differs')
    with zipfile.ZipFile(root/'source.zip') as z:
        names=z.namelist();require(len(names)==len(set(names)) and all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts for n in names),'Unsafe source ZIP')
        selected=sorted([n for n in names if n.startswith('benchmarks/llgl_vulkan_draw/') and '/' not in n[len('benchmarks/llgl_vulkan_draw/'):] and n.endswith(('.cpp','.hpp','CMakeLists.txt'))]+EXTRA)
        fp=sha(('profile|llgl-fixed-pin-transfer-src-v1\n'+''.join(n+'|'+sha(z.read(n))+'\n' for n in selected)).encode())
        shader=sha(z.read('tests/fixtures/SpirvFixtures.hpp'))
        suffix='benchmarks/llgl_vulkan_draw/main.cpp';original=next(r['path'][:-len(suffix)] for r in receipt['source_inputs'] if r['path'].endswith(suffix));frozen={r['path'][len(original):]:r for r in receipt['source_inputs'] if r['path'].startswith(original)}
        for n in names:
            if n in frozen:require(sha(z.read(n))==frozen[n]['sha256'],'Source ZIP differs from executed source')
    require(fp==receipt['source_fingerprint'] and shader==receipt['shader_header_sha256'],'Native source/shader identity differs')
    ref=[sha(frame(i)) for i in range(4)];require(read(root/'native/frame-reference.json')['sha256']==ref,'CPU full-frame reference differs')
    observer=read(root/'build/VkLayer_gfx_llgl_draw.json')['layer'];require(observer['name']=='VK_LAYER_GFX_llgl_draw' and observer['implementation_version']=='1' and observer['functions']['vkNegotiateLoaderLayerInterfaceVersion']=='gfxDrawNegotiate','Observer manifest invalid')
    jobs=read(root/'native/commands.json');require(len(jobs)==16 and jobs==receipt['commands'],'Native matrix incomplete')
    require([j['name'] for j in jobs[:6]]==[n+'-'+mode for n in WORK for mode in ('plain','validation')],'Three workload/mode matrix differs')
    rejection={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','wrong-call-count':b'Native draw count/arguments differ','wrong-instance-count':b'Native draw count/arguments differ','wrong-scissor':b'Full changing draw reference mismatch','corrupt-readback':b'Full changing draw reference mismatch','observer-disabled':b'Native draw observer not active','sync-disabled':b'Expected validation control diagnostic absent'}
    totals=dict(images=0,bytes=0,calls=0,instances=0,vertices=0)
    for job in jobs:
        name=job['name'];out=(root/'native'/(name+'.stdout')).read_bytes();err=(root/'native'/(name+'.stderr')).read_bytes()
        require(job['returncode']==job['expected']==(1 if name in rejection else 0),'Unexpected native exit')
        if name in rejection:require(not out and rejection[name] in err,'Unrelated control rejection');continue
        row=json.loads(out);control=job['scenario'] in ('core-control','sync-control');calls,instances=(0,0) if control else WORK[job['scenario']]
        require(row['source_fingerprint']==fp and row['shader_header_sha256']==shader and row['profile_id']==receipt['profile_id'] and row['api']=='vulkan' and row['configuration']=='Release','Wrong/stale source profile')
        require(all(row[k]==v for k,v in receipt['identity'].items()) and row['native_handle_verified'] is True and row['queue_flags']&1,'Actual target/queue differs')
        require(row['validation'] is job['validation'] and bool(row['loaded_validation_layer'])==job['validation'] and row['observer_path'] and row['runtime_modules'],'Runtime observer/validation absent')
        require(row['instance_API_request_observed'] is True and row['observed_instance_api_version']==row['loader_api_version'] and row['observed_instance_api_version'] in receipt['observed_instance_api_versions'],'Instance API not observed')
        require(row['observer_live_instances_after_close']==row['observer_live_devices_after_close']==0,'Native objects not retired')
        require(row['native_draw_calls']==row['native_forwarded_draw_calls']==calls*4 and row['native_instances']==calls*instances*4 and row['native_vertices']==calls*instances*12 and row['native_bad_arguments']==0 and row['native_min_instances']==row['native_max_instances']==instances,'Native draw/instance argument counts differ')
        require(row['performance_samples']==0 and row['samples_ms']==row['CPU_intervals_ms']==[] and row['gpu_ms'] is None and row['performance_accepted'] is row['native_workload_shortcuts'] is False,'Correctness promoted to performance/native shortcut')
        if control:
            token='VUID-VkPhysicalDeviceProperties2-sType-sType' if job['scenario']=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE'
            require(row['control_diagnostic']==token and row['delivered_errors']>0 and row['submitted_control_work']==row['verified_images']==row['verified_bytes']==0,'Validation control absent/unsafe')
        else:
            require(row['width']==65 and row['height']==37 and row['texel_bytes']==4 and row['scissor']==[5,3,55,31] and row['first_vertex']==row['first_instance']==0 and row['vertices_per_instance']==3,'Geometry differs')
            require(row['clear_strategy']=='automatic_offscreen_pass_plus_full_attachment_clear' and row['texture_bind_policy']=='color_attachment_sampled_copy_src','Undeclared public API mapping')
            require(row['warmup']==1 and row['sample_count']==3 and row['verified_images']==4 and row['verified_bytes']==38480 and row['frame_sha256']==ref and row['readback_verified'] is True and row['delivered_errors']==0,'Full changing reference incomplete')
            require(not re.search(rb'Validation Error|SYNC-HAZARD',err),'Native workload validation error')
            totals['images']+=4;totals['bytes']+=38480;totals['calls']+=row['native_draw_calls'];totals['instances']+=row['native_instances'];totals['vertices']+=row['native_vertices']
    require(totals==dict(images=24,bytes=230880,calls=8016,instances=16008,vertices=48024),'Aggregate native observations incomplete')
    require(receipt['verified_images']==24 and receipt['verified_bytes']==230880 and receipt['native_draw_calls']==8016 and receipt['native_instances']==16008 and receipt['native_vertices']==48024 and receipt['core_and_sync_controls_passed'] is True,'Receipt aggregate differs')
    inputs=read(root/'native/inputs.json');require(len(inputs['files'])==386 and len(inputs['read_logs'])==4 and len(inputs['compiler_commands'])==2,'Consumer input audit absent')
    extension=read(root/'profile/llgl-transfer-src-draw.json')
    require(extension['status']=='draw_correctness_verified' and extension['verified_correctness']==['draw'] and extension['performance_accepted'] is False and
            extension['base_contract']['sha256']==sha((root/'profile/llgl-transfer-src-profile.json').read_bytes()) and extension['evidence']['sha256']==sha((root/'native/receipt.json').read_bytes()),'Draw extension/source profile binding differs')
    print('PASS saved CMP-04: 24 full images, 8016 forwarded native draws, 16008 instances; no performance acceptance')
    return receipt

if __name__=='__main__':verify()
