"""Independently replay PSO evidence and complete stripe references; never execute ZIP code."""
import hashlib
import json
import pathlib
import re
import zipfile

HERE=pathlib.Path(__file__).resolve().parent
WORK=dict(pso1=1,pso8=8)
EXTRA=['benchmarks/llgl_vulkan_upload/Context.hpp','benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/llgl_vulkan_draw/Layer.cpp','benchmarks/vulkan_pso/Frame.hpp','benchmarks/phase3_comparison/pso.json','benchmarks/competitors/sources.lock.json',
       'tests/fixtures/SpirvFixtures.hpp','tests/fixtures/spirv/fullscreen.spvasm','tests/fixtures/spirv/red.spvasm','tests/fixtures/spirv/green.spvasm']
REJECT={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','missing-pipeline':b'Public pipeline creation count differs','skip-release':b'Public pipeline release count differs','wrong-pipeline':b'Full changing PSO stripe reference mismatch','skip-region':b'Native PSO draw count/arguments differ','wrong-scissor':b'Full changing PSO stripe reference mismatch','corrupt-readback':b'Full changing PSO stripe reference mismatch','observer-disabled':b'Native draw observer not active','sync-disabled':b'Expected validation control diagnostic absent'}

def require(ok,message):
    if not ok:raise ValueError(message)

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))

def sha(data):return hashlib.sha256(data).hexdigest()

def frame(iteration,count):
    image=bytearray()
    for y in range(37):
        for x in range(65):
            pixel=(0,255 if iteration%2 else 0,255,255)
            if 5<=x<60 and 3<=y<34:
                stripe=next(i for i in range(count) if 5+55*i//count<=x<5+55*(i+1)//count)
                pixel=(0,255,0,255) if (iteration+stripe)%2 else (255,0,0,255)
            image.extend(pixel)
    return bytes(image)

def exact(row,expected):
    require(all(k in row and type(row[k]) is type(v) and row[k]==v for k,v in expected.items()),'PSO scope/lifecycle/reference field differs')

def verify(root=HERE):
    manifest=read(root/'manifest.json');exact(manifest,dict(task='P3-CMP-05',performance_samples=0))
    require(len(manifest['files'])==len(set(i['path'] for i in manifest['files'])),'Duplicate evidence path')
    for item in manifest['files']:
        p=pathlib.PurePosixPath(item['path']);require(not p.is_absolute() and '..' not in p.parts and '__pycache__' not in p.parts and p.suffix!='.pyc' and ':' not in str(p) and '\\' not in str(p),'Unsafe/cache evidence path')
        data=(root/p).read_bytes();require(sha(data)==item['sha256'] and len(data)==item['bytes'],'Evidence hash differs: '+str(p))
    receipt=read(root/'native/receipt.json')
    totals_expected=dict(verified_images=16,verified_bytes=153920,created_pipelines=72,released_pipeline_handles=72,used_pipeline_regions=72,created_shader_handles=144,released_shader_handles=144,native_draw_calls=72,native_instances=72,native_vertices=216)
    exact(receipt,dict(task='P3-CMP-05',status='pso_correctness_passed',profile_id='llgl-fixed-pin-transfer-src-v1',inputs_unchanged=True,performance_samples=0,performance_accepted=False,core_and_sync_controls_passed=True,GPU_memory_reclamation_verified=False,native_pipeline_count_verified=False,driver_cache_state_known=False,**totals_expected))
    with zipfile.ZipFile(root/'source.zip') as z:
        names=z.namelist();require(len(names)==len(set(names)) and all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts and ':' not in n and '\\' not in n for n in names),'Unsafe source ZIP')
        folder='benchmarks/llgl_vulkan_pso/'
        selected=sorted([n for n in names if n.startswith(folder) and '/' not in n[len(folder):] and n.endswith(('.cpp','.hpp','CMakeLists.txt'))]+EXTRA)
        require([n for n in selected if n.startswith(folder)]==[folder+n for n in ('CMakeLists.txt','Support.hpp','main.cpp')],'Native source coverage differs')
        fp=sha(('profile|llgl-fixed-pin-transfer-src-v1\nworkload|pso\n'+''.join(n+'|'+sha(z.read(n))+'\n' for n in selected)).encode());shader=sha(z.read('tests/fixtures/SpirvFixtures.hpp'))
        suffix=folder+'main.cpp';original=next(r['path'][:-len(suffix)] for r in receipt['source_inputs'] if r['path'].endswith(suffix));frozen={r['path'][len(original):]:r for r in receipt['source_inputs'] if r['path'].startswith(original)}
        for n in selected:require(n in frozen and sha(z.read(n))==frozen[n]['sha256'],'Source ZIP differs from executed source')
        plan=json.loads(z.read('benchmarks/phase3_comparison/pso.json'));require(plan['work']==WORK and plan['timing']['includes']==['public_pipeline_create_including_shader_object_create_release','public_pipeline_handle_release'],'Baseline PSO work differs')
    require(fp==receipt['source_fingerprint'] and shader==receipt['shader_header_sha256'],'Native source/shader identity differs')
    refs={name:[sha(frame(i,count)) for i in range(4)] for name,count in WORK.items()}
    require(read(root/'native/frame-reference.json')==dict(width=65,height=37,scissor=[5,3,55,31],sha256=refs),'CPU full-stripe reference differs')
    layer=read(root/'build/VkLayer_gfx_llgl_draw.json')['layer'];require(layer['name']=='VK_LAYER_GFX_llgl_draw' and layer['implementation_version']=='1' and layer['functions']['vkNegotiateLoaderLayerInterfaceVersion']=='gfxDrawNegotiate','Observer manifest invalid')
    jobs=read(root/'native/commands.json');require(jobs==receipt['commands'] and [j['name'] for j in jobs]==[n+'-'+mode for n in WORK for mode in ('plain','validation')]+['core-control','sync-control']+list(REJECT),'Native matrix incomplete')
    sums={k:0 for k in totals_expected}
    for job in jobs:
        name=job['name'];out=(root/'native'/(name+'.stdout')).read_bytes();err=(root/'native'/(name+'.stderr')).read_bytes()
        require(type(job['returncode']) is int and job['returncode']==job['expected']==(1 if name in REJECT else 0),'Unexpected native exit')
        if name in REJECT:require(not out and REJECT[name] in err,'Unrelated control rejection');continue
        row=json.loads(out);control=name in ('core-control','sync-control');scenario=name if control else name.rsplit('-',1)[0];mode=True if control else name.endswith('-validation');count=0 if control else WORK[scenario]
        require(job['scenario']==scenario and job['validation'] is mode,'Wrong mode/scenario association')
        expected=dict(schema_version=1,task='P3-CMP-05',sdk='llgl',api='vulkan',configuration='Release',profile_id=receipt['profile_id'],source_fingerprint=fp,shader_header_sha256=shader,
            contract='vulkan_pipeline_create_public_release_use_checked_v1',category='cpu_api',scenario=scenario,validation=mode,control=control,native_handle_verified=True,instance_API_request_observed=True,
            clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',texture_bind_policy='color_attachment_sampled_copy_src',width=65,height=37,texel_bytes=4,scissor=[5,3,55,31],vertices_per_pipeline=3,first_vertex=0,first_instance=0,
            pipelines_per_iteration=count,created_pipelines=count*4,released_pipeline_handles=count*4,used_pipeline_regions=count*4,created_shader_handles=count*8,released_shader_handles=count*8,
            includes_shader_object_create_release=True,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,native_pipeline_count_verified=False,driver_cache_state_known=False,cache_state='uncontrolled_driver_cache_repeated_public_creation',
            warmup=0 if control else 1,sample_count=0 if control else 3,verified_images=0 if control else 4,verified_bytes=0 if control else 38480,readback_verified=not control,frame_sha256=[] if control else refs[scenario],
            native_draw_calls=count*4,native_forwarded_draw_calls=count*4,native_instances=count*4,native_vertices=count*12,native_bad_arguments=0,submitted_control_work=0,
            observer_live_instances_after_close=0,observer_live_devices_after_close=0,performance_samples=0,samples_ms=[],gpu_ms=None,performance_accepted=False,native_workload_shortcuts=False)
        exact(row,expected);exact(row,receipt['identity'])
        require(row['queue_flags']&1 and bool(row['loaded_validation_layer'])==mode and row['observer_path'] and row['runtime_modules'],'Actual queue/runtime layer absent')
        require(row['observed_instance_api_version']==row['loader_api_version'] and row['observed_instance_api_version'] in receipt['observed_instance_api_versions'],'Instance API not observed')
        if control:
            token='VUID-VkPhysicalDeviceProperties2-sType-sType' if name=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE'
            require(row['control_diagnostic']==token and type(row['delivered_errors']) is int and row['delivered_errors']>0,'Validation control absent')
        else:
            exact(row,dict(control_diagnostic='',delivered_errors=0));require(not re.search(rb'Validation Error|SYNC-HAZARD',err),'Native workload validation error')
            for key in sums:sums[key]+=row[key]
    require(sums==totals_expected,'Native lifecycle totals differ')
    inputs=read(root/'native/inputs.json');require(inputs['files'] and len(inputs['read_logs'])==2 and len(inputs['compiler_commands'])==1,'Consumer compiler/linker input audit absent')
    extension=read(root/'profile/llgl-transfer-src-pso.json')
    require(extension['status']=='pso_correctness_verified' and extension['verified_correctness']==['pso'] and extension['performance_accepted'] is False and extension['GPU_memory_reclamation_verified'] is extension['native_pipeline_count_verified'] is extension['driver_cache_state_known'] is False and
            extension['base_contract']['sha256']==sha((root/'profile/llgl-transfer-src-profile.json').read_bytes()) and extension['prior_draw_contract']['sha256']==sha((root/'profile/llgl-transfer-src-draw.json').read_bytes()) and extension['evidence']['sha256']==sha((root/'native/receipt.json').read_bytes()) and extension['source_fingerprint']==fp and extension['shader_header_sha256']==shader,'PSO extension binding differs')
    print('PASS saved CMP-05: 16 full images, 72 PSO create/use/public releases, 144 shader handles; no performance/physical reclamation acceptance')
    return receipt

if __name__=='__main__':verify()
