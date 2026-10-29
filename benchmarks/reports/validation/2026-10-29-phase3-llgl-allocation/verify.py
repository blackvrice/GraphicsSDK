"""Replay resource lifecycle and every full-payload reference without GPU or ZIP code execution."""
import functools
import hashlib
import json
import pathlib
import re
import zipfile

HERE=pathlib.Path(__file__).resolve().parent
WORK=dict(buffer1=1,buffer1000=1000,texture1=1,texture1000=1000)
EXTRA=['benchmarks/llgl_vulkan_upload/Context.hpp','benchmarks/llgl_vulkan_pso/Support.hpp','benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/llgl_vulkan_draw/Layer.cpp','benchmarks/vulkan_allocation/Payload.hpp','benchmarks/phase3_comparison/allocation.json','benchmarks/competitors/sources.lock.json']
REJECT={'missing-layer':b'Required validation layer absent','wrong-target':b'Wrong target GPU','missing-resource':b'Public resource creation count differs','alias-resource':b'Duplicate live public resource handle','skip-release':b'Public resource release count differs','wrong-resource-payload':b'Full changing resource readback mismatch','stale-iteration':b'Full changing resource readback mismatch','skip-copy':b'Full changing resource readback mismatch','corrupt-readback':b'Full changing resource readback mismatch','short-readback':b'Incomplete full resource readback','observer-disabled':b'Native draw observer not active','sync-disabled':b'Expected validation control diagnostic absent'}

def require(ok,message):
    if not ok:raise ValueError(message)

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))

def sha(data):return hashlib.sha256(data).hexdigest()

def exact(row,expected):
    require(all(k in row and type(row[k]) is type(v) and row[k]==v for k,v in expected.items()),'Resource scope/lifecycle/reference field differs')

@functools.lru_cache(maxsize=4096)
def payload(iteration,resource):
    # uint32_t wrapping at each multiplication, as in the fixed baseline.
    out=bytearray()
    for offset in range(9620):
        value=((offset*3266489917)^(iteration*2246822519)^(resource*2654435761))&0xffffffff
        value=((value^(value>>16))*2246822519)&0xffffffff
        value=((value^(value>>13))*3266489917)&0xffffffff
        out.append((value^(value>>16))&255)
    return bytes(out)

@functools.lru_cache(maxsize=4)
def references(scenario):
    size=9620 if scenario.startswith('texture') else 4096
    return [[sha(payload(i,j)[:size]) for j in range(WORK[scenario])] for i in range(4)]

def verify(root=HERE):
    manifest=read(root/'manifest.json');exact(manifest,dict(task='P3-CMP-06',performance_samples=0));require(len(manifest['files'])==len(set(i['path'] for i in manifest['files'])),'Duplicate evidence path')
    for item in manifest['files']:
        p=pathlib.PurePosixPath(item['path']);require(not p.is_absolute() and '..' not in p.parts and '__pycache__' not in p.parts and p.suffix!='.pyc' and ':' not in str(p) and '\\' not in str(p),'Unsafe/cache evidence path')
        data=(root/p).read_bytes();require(sha(data)==item['sha256'] and len(data)==item['bytes'],'Evidence hash differs: '+str(p))
    receipt=read(root/'native/receipt.json');totals_expected=dict(created_resources=16016,released_resource_handles=16016,used_resources=16016,verified_resources=16016,unique_native_resources_checked=16016,verified_images=8008,verified_buffers=8008,verified_bytes=109837728)
    exact(receipt,dict(task='P3-CMP-06',status='allocation_correctness_passed',profile_id='llgl-fixed-pin-transfer-src-v1',inputs_unchanged=True,core_and_sync_controls_passed=True,GPU_memory_reclamation_verified=False,native_allocation_count_verified=False,allocator_cache_state_known=False,performance_samples=0,performance_accepted=False,**totals_expected))
    with zipfile.ZipFile(root/'source.zip') as z:
        names=z.namelist();require(len(names)==len(set(names)) and all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts and ':' not in n and '\\' not in n for n in names),'Unsafe source ZIP')
        folder='benchmarks/llgl_vulkan_allocation/';selected=sorted([n for n in names if n.startswith(folder) and '/' not in n[len(folder):] and n.endswith(('.cpp','.hpp','CMakeLists.txt'))]+EXTRA)
        require([n for n in selected if n.startswith(folder)]==[folder+n for n in ('CMakeLists.txt','main.cpp')],'Native source coverage differs')
        fp=sha(('profile|llgl-fixed-pin-transfer-src-v1\nworkload|allocation\n'+''.join(n+'|'+sha(z.read(n))+'\n' for n in selected)).encode())
        suffix=folder+'main.cpp';original=next(r['path'][:-len(suffix)] for r in receipt['source_inputs'] if r['path'].endswith(suffix));frozen={r['path'][len(original):]:r for r in receipt['source_inputs'] if r['path'].startswith(original)}
        for n in selected:require(n in frozen and sha(z.read(n))==frozen[n]['sha256'],'Source ZIP differs from executed source')
        plan=json.loads(z.read('benchmarks/phase3_comparison/allocation.json'));require(plan['work']==WORK and plan['resources']['buffer_bytes']==4096 and plan['resources']['texture_shape']==[65,37,4] and plan['timing']['includes']==['public_device_resource_create','public_resource_handle_release'],'Baseline resource work differs')
    require(fp==receipt['source_fingerprint'],'Native source identity differs')
    exact(read(root/'native/payload-reference.json'),dict(buffer_bytes=4096,texture_shape=[65,37,4],sha256={n:references(n) for n in WORK}))
    layer=read(root/'build/VkLayer_gfx_llgl_draw.json')['layer'];require(layer['name']=='VK_LAYER_GFX_llgl_draw' and layer['implementation_version']=='1','Observer manifest invalid')
    jobs=read(root/'native/commands.json');require(jobs==receipt['commands'] and [j['name'] for j in jobs]==[n+'-'+mode for n in WORK for mode in ('plain','validation')]+['core-control','sync-control']+list(REJECT),'Native matrix incomplete')
    sums={k:0 for k in totals_expected}
    for job in jobs:
        name=job['name'];out=(root/'native'/(name+'.stdout')).read_bytes();err=(root/'native'/(name+'.stderr')).read_bytes();require(type(job['returncode']) is int and job['returncode']==job['expected']==(1 if name in REJECT else 0),'Unexpected native exit')
        if name in REJECT:require(not out and REJECT[name] in err,'Unrelated control rejection');continue
        row=json.loads(out);control=name in ('core-control','sync-control');scenario=name if control else name.rsplit('-',1)[0];mode=True if control else name.endswith('-validation');count=0 if control else WORK[scenario];texture=scenario.startswith('texture');total=count*4
        require(job['scenario']==scenario and job['validation'] is mode,'Wrong mode/scenario association')
        exact(row,dict(schema_version=1,task='P3-CMP-06',sdk='llgl',api='vulkan',configuration='Release',profile_id=receipt['profile_id'],source_fingerprint=fp,contract='vulkan_resource_create_public_release_full_readback_v1',category='cpu_api',scenario=scenario,validation=mode,control=control,native_handle_verified=True,instance_API_request_observed=True,
            resource_kind='none' if control else 'texture' if texture else 'buffer',width=65 if texture else 0,height=37 if texture else 0,texel_bytes=4 if texture else 0,buffer_bytes=4096 if not control and not texture else 0,resources_per_iteration=count,
            created_resources=total,released_resource_handles=total,used_resources=total,verified_resources=total,unique_native_resources_checked=total,native_resource_handles_verified=not control,verified_images=total if texture else 0,verified_buffers=total if not texture else 0,verified_bytes=total*(9620 if texture else 4096),readback_verified=not control,
            allocation_policy='public_device_resource_default_policy',usage_policy='none' if control else 'sampled_rgba8_2d' if texture else 'llgl_vertex_buffer_copy_src_dst',buffer_cpu_access_flags=0,initial_data_supplied=False,copy_submission_policy='separate_upload_download_completed_fence_idle',staging_in_create_release_boundary=False,row_stride=0,layer_stride=0,
            GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,native_allocation_count_verified=False,allocator_cache_state_known=False,warmup=0 if control else 1,sample_count=0 if control else 3,payload_sha256=[] if control else references(scenario),native_draw_calls=0,submitted_control_work=0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,performance_samples=0,samples_ms=[],gpu_ms=None,performance_accepted=False,native_workload_shortcuts=False))
        exact(row,receipt['identity']);require(row['queue_flags']&1 and bool(row['loaded_validation_layer'])==mode and row['observer_path'] and row['runtime_modules'],'Actual queue/runtime layer absent');require(row['observed_instance_api_version']==row['loader_api_version'] and row['observed_instance_api_version'] in receipt['observed_instance_api_versions'],'Instance API not observed')
        if control:
            token='VUID-VkPhysicalDeviceProperties2-sType-sType' if name=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE';require(row['control_diagnostic']==token and type(row['delivered_errors']) is int and row['delivered_errors']>0,'Validation control absent')
        else:
            exact(row,dict(control_diagnostic='',delivered_errors=0));require(not re.search(rb'Validation Error|SYNC-HAZARD',err),'Native workload validation error')
            for key in sums:sums[key]+=row[key]
    require(sums==totals_expected,'Native resource totals differ')
    inputs=read(root/'native/inputs.json');require(inputs['files'] and len(inputs['read_logs'])==2 and len(inputs['compiler_commands'])==1,'Consumer compiler/linker audit absent')
    extension=read(root/'profile/llgl-transfer-src-allocation.json');exact(extension,dict(status='allocation_correctness_verified',verified_correctness=['allocation'],performance_accepted=False,GPU_memory_reclamation_verified=False,native_allocation_count_verified=False,allocator_cache_state_known=False,source_fingerprint=fp))
    require(extension['base_contract']['sha256']==sha((root/'profile/llgl-transfer-src-profile.json').read_bytes()) and extension['prior_pso_contract']['sha256']==sha((root/'profile/llgl-transfer-src-pso.json').read_bytes()) and extension['evidence']['sha256']==sha((root/'native/receipt.json').read_bytes()),'Resource extension binding differs')
    print('PASS saved CMP-06: 16016 resources, 109837728 full bytes, all payload hashes; no performance/native memory allocation/reclamation acceptance')
    return receipt

if __name__=='__main__':verify()
