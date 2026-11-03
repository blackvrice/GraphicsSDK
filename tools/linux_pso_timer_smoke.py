"""Short LLGL Linux PSO timer diagnostics; never a three-SDK performance campaign."""
import argparse
import json
import math
import os
import pathlib
import platform
import re
import subprocess

import linux_llgl_pso as correctness
import linux_llgl_probe as p
from linux_comparison import require, save, record, unchanged

NEGATIVE = {
    'missing-pipeline': b'Public pipeline creation count differs',
    'skip-release': b'Public pipeline release count differs',
    'wrong-pipeline': b'Full changing PSO stripe reference mismatch',
    'skip-region': b'Native PSO draw count/arguments differ',
    'wrong-scissor': b'Full changing PSO stripe reference mismatch',
    'corrupt-readback': b'Full changing PSO stripe reference mismatch',
}


def validate(value, scenario, observed, uuid):
    probe = scenario == 'probe'
    count = 0 if probe else correctness.SHAPES[scenario]
    images = 0 if probe else 4
    p.exact(value, dict(schema_version=1, task='P3-LBEN-03-pso-timer', sdk='llgl',
        mode='software-diagnostic', configuration='Release', category='cpu_api',
        contract='vulkan_pipeline_create_public_release_use_checked_v1', device_uuid=uuid,
        device_luid=None, native_handle_verified=True, validation=False, probe=probe,
        control=False, delivered_errors=0, submitted_control_work=0,
        width=65, height=37, texel_bytes=4, scissor=[5,3,55,31],
        vertices_per_pipeline=3, first_vertex=0, first_instance=0,
        warmup=0 if probe else 1, sample_count=0 if probe else 3,
        pipelines_per_iteration=count, created_pipelines=count*images,
        released_pipeline_handles=count*images, used_pipeline_regions=count*images,
        created_shader_handles=2*count*images, released_shader_handles=2*count*images,
        includes_shader_object_create_release=True, GPU_use_in_timing=False,
        GPU_memory_reclamation_verified=False, native_pipeline_count_verified=False,
        driver_cache_state_known=False, cache_state='uncontrolled_driver_cache_repeated_public_creation',
        verified_images=images, verified_bytes=images*9620, readback_verified=not probe,
        gpu_ms=None, columns=['create_ms','public_release_ms'], observer_enabled=observed,
        observer_live_instances_after_close=0, observer_live_devices_after_close=0,
        requested_instance_api_version=None, loaded_validation_layer='', native_workload_shortcuts=False,
        frame_sha256=correctness.references()[scenario] if observed and not probe else []))
    rows=value['samples_ms']
    require(type(rows) is list and len(rows)==(0 if probe else 3), 'PSO diagnostic rows differ')
    require(all(type(row) is list and len(row)==2 and all(type(x) in (int,float) and math.isfinite(x) and x>=0 for x in row) for row in rows), 'Invalid PSO CPU intervals')
    for key, expected in dict(native_draw_calls=count*images, native_forwarded_draw_calls=count*images,
            native_instances=count*images, native_vertices=3*count*images, native_bad_arguments=0,
            native_min_instances=1 if images else 0, native_max_instances=1 if images else 0).items():
        p.exact(value, {key:expected if observed else None})
    modules=value['runtime_modules']
    require(type(modules) is list and modules==sorted(set(modules)) and all(type(x) is str and pathlib.PurePosixPath(x).is_absolute() for x in modules), 'Invalid module snapshot')
    require(not any('VkLayer_khronos_validation' in x for x in modules), 'Validation contaminated plain diagnostics')
    require(any(pathlib.PurePosixPath(x).name=='libVkLayer_gfx_llgl_draw.so' for x in modules)==observed, 'Observer module differs')
    require(value['environment']['platform']=='Linux' and value['environment']['pointer_bits']==64, 'Native Linux required')
    require(value['instance_API_request_observed'] is observed, 'Instance observation differs')
    require(type(value['observed_instance_api_version']) is int and (value['observed_instance_api_version']>=4194304 if observed else value['observed_instance_api_version']==0), 'Observed instance API differs')
    return value


def run(a):
    require(platform.system()=='Linux' and platform.machine() in ('x86_64','AMD64'), 'Native Linux x86_64 required')
    require(re.fullmatch('[0-9a-f]{32}', a.uuid) and int(a.uuid,16), 'Explicit native UUID required')
    out=a.output.resolve(); require(not out.exists(), 'Fresh diagnostic output required')
    binary=a.binary.resolve(); observer=a.observer_directory.resolve(); lib=a.llgl_library_directory.resolve(); icd=a.icd.resolve()
    frozen=[record(x) for x in (binary, observer/'libVkLayer_gfx_llgl_draw.so', observer/'VkLayer_gfx_llgl_draw.json', lib/'libLLGL.so', lib/'libLLGL_Vulkan.so', icd)]
    out.mkdir(parents=True)
    env={k:v for k,v in os.environ.items() if not k.startswith(('VK_','GFX_','LD_'))}
    env.update(VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd), VK_LOADER_LAYERS_DISABLE='~implicit~',
        LD_LIBRARY_PATH=str(lib), GFX_LLGL_PSO_COMPARE_KIND='software-diagnostic', VK_LAYER_PATH=str(observer))
    jobs=[(s,o,None) for s in ('probe','pso1','pso8') for o in (False,True)]
    jobs += [(s,True,t) for s,t in NEGATIVE.items()]
    jobs += [('probe',True,b'Required runtime module absent or ambiguous')]
    runs=[]
    for i,(scenario,observed,token) in enumerate(jobs):
        current=dict(env)
        if observed and i!=len(jobs)-1: current['VK_INSTANCE_LAYERS']='VK_LAYER_GFX_llgl_draw'
        command=[str(binary),a.uuid,'observe' if observed else 'plain',scenario, '0' if scenario=='probe' else '1','0' if scenario=='probe' else '3']
        proc=subprocess.run(command,env=current,capture_output=True,timeout=120)
        name=f'{i:02}-{scenario}-{int(observed)}'
        (out/(name+'.stdout')).write_bytes(proc.stdout);(out/(name+'.stderr')).write_bytes(proc.stderr)
        if token:
            require(proc.returncode==1 and not proc.stdout and token in proc.stderr, 'Unrelated or missing PSO rejection: '+name)
            value=None
        else:
            require(proc.returncode==0 and not proc.stderr, 'PSO native diagnostic failed: '+name)
            value=validate(json.loads(proc.stdout),scenario,observed,a.uuid)
        runs.append(dict(command=command,environment={k:v for k,v in current.items() if k.startswith(("VK_","GFX_","LD_"))},returncode=proc.returncode,result=value))
    require(unchanged(frozen), 'Native inputs changed during diagnostics')
    result=dict(schema_version=1, scope='LLGL-only-timer-pilot', mode='software-diagnostic',
        processes=len(runs), rejection_controls=7, verified_images=16, verified_bytes=153920,
        diagnostic_rows=12, measurement_samples=0, statistics_exported=False, performance_accepted=False,
        physical_GPU_accepted=False, P3_LBEN_03_completed=False, frozen_inputs=frozen, runs=runs)
    save(out/'receipt.json',result);print('PASS LLGL PSO timer: 13 software processes, 16 full images; no performance acceptance')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','observer-directory','icd','llgl-library-directory','output'):
        parser.add_argument('--'+name,type=pathlib.Path,required=True)
    parser.add_argument('--uuid',required=True);run(parser.parse_args())
