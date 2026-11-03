"""Fresh Linux three-SDK PSO create/public-release CPU campaign; software statistics excluded."""
import argparse
import itertools
import math
import pathlib
import random
import sys

import linux_upload_campaign as shared
import linux_llgl_pso as correctness
import port_linux_pso_campaign as port
from linux_comparison import ROOT,require,read,sha
p=shared.p
PROFILE='pso'
TASK='P3-LBEN-03-pso-campaign'
COMPLETION_FIELD='P3_LBEN_03_completed'
CORRECTNESS=correctness
PLAN='benchmarks/phase3_comparison/linux-pso-campaign.json'
WINDOWS_PLAN='benchmarks/phase3_comparison/competitors/llgl-pso-comparison.json'
REFERENCE_FILE='frame-reference.json'
REFERENCE_SOURCE=None
SOURCE_FILE='tools/linux_pso_campaign.py'
EXTRA_SOURCES=('tools/linux_llgl_pso.py','tools/linux_build_policy.py')
CMAKE_DIRECTORY='benchmarks/linux_pso_campaign'
TARGET='linux_pso_compare'
KIND_VARIABLE='GFX_LLGL_PSO_COMPARE_KIND'
OWN_HEADERS=('benchmarks/common/LinuxEnvironment.hpp','benchmarks/common/Adapter.hpp',
    'benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/vulkan_pso/Frame.hpp','tests/fixtures/SpirvFixtures.hpp')
SDKS=shared.SDKS
WORK=correctness.SHAPES
SHAPES={n:[65,37,4] for n in WORK}
COLUMNS=['create_ms','public_release_ms']
SUCCESS=shared.SUCCESS|{'audit'}
FULL_TOTALS=(1082,10408840)
SMOKE_TOTALS=(56,538720)
MEASUREMENT_SAMPLES=900
DIAGNOSTIC_ROWS=(42,24)


def reference():return correctness.references()
def initial_fields():return dict(shader_header_sha256=sha(ROOT/'tests/fixtures/SpirvFixtures.hpp'))
def self():return sys.modules[__name__]
def validate_inputs(ready,llgl,mode):return shared.validate_inputs(ready,llgl,mode,PROFILE,CORRECTNESS)
def arguments(job,r):return shared.arguments(job,r,self())
def environment(job,r):return shared.environment(job,r,self())
def validate_success(row,stdout,stderr,job,r):return shared.validate_success(row,stdout,stderr,job,r,self())
def audit(build,call,stdout):return shared.audit(build,call,stdout,self())
def check(folder,live=False):return shared.check(folder,live,self())


def validate_intervals(values):
    for row in values:
        require(type(row) is list and len(row)==2 and shared.positive_number(row[0])
            and type(row[1]) in (int,float) and math.isfinite(row[1]) and row[1]>=0,'Invalid create/public-release intervals')


def elapsed_samples_ms(value):return sum(sum(row) for row in value['samples_ms'])


def validate_plan(value):
    geometry=dict(shape=[65,37,4],scissor=[5,3,55,31],vertices_per_pipeline=3,
        reference='full_changing_red_green_pipeline_regions_blue_cyan_clear')
    expected=dict(schema_version=1,task='P3-LBEN-03',scope='linux-x86_64-qualified-three-sdk-pso',sdks=list(SDKS),scenarios=list(SHAPES),
        validation=dict(warmup=1,samples=3,required_core_and_sync=True),
        performance=dict(warmup=5,samples=30,repetitions=5,seed=20261027,order='seeded_scenario_blocks_five_distinct_three_SDK_permutations'),
        smoke=dict(warmup=1,samples=3,repetitions=1,statistics_exported=False),serialized_processes=True,
        observer_free_measurements=True,loader_trace_in_measurements=False,validation_timings_excluded=True,
        historical_timings_reused=False,overall_SDK_ranking=False,GPU_timestamp_measurement=False,
        SDL_native_handle_verified=False,physical_qualification_required_for_measurements=True,
        columns=COLUMNS,work=WORK,geometry=geometry,cache_state='uncontrolled_driver_cache_repeated_public_creation',
        native_pipeline_count_verified=False,driver_cache_state_known=False,GPU_memory_reclamation_verified=False)
    require(p.same(value,expected),'Linux PSO contract changed')
    prior=read(ROOT/WINDOWS_PLAN)
    require(all(p.same(value[k],prior[k]) for k in ('performance','validation','work','geometry','cache_state','native_pipeline_count_verified','driver_cache_state_known','GPU_memory_reclamation_verified'))
            and p.same(value['columns'],prior['timing']['columns']),'Frozen PSO interval/work/repetition contract changed')
    return value


def schedule(plan,mode):
    validate_plan(plan);p.identity_prefix(mode)
    rows=[dict(kind='probe',sdk=s,scenario='probe',repetition=0) for s in SDKS]
    rows += [dict(kind='observe',sdk='llgl',scenario='probe',repetition=0)]
    rows += [dict(kind='audit',sdk='llgl',scenario=n,repetition=0) for n in SHAPES]
    rows += [dict(kind='validation',sdk=s,scenario=n,repetition=0) for n in SHAPES for s in SDKS]
    rows += [dict(kind=k,sdk=s,scenario='pso1',repetition=0) for s in SDKS for k in ('missing-layer','wrong-uuid','wrong-kind')]
    rows += [dict(kind=k,sdk='llgl',scenario=n,repetition=0) for k,n in (
        ('core-control','core-control'),('sync-control','sync-control'),('sync-disabled','sync-control'),
        ('missing-pipeline','missing-pipeline'),('skip-release','skip-release'),('wrong-pipeline','wrong-pipeline'),('skip-region','skip-region'),('wrong-scissor','wrong-scissor'),
        ('corrupt-readback','corrupt-readback'),('observer-disabled','probe'))]
    rng=random.Random(plan['performance']['seed']);orders={}
    for n in SHAPES:orders[n]=list(itertools.permutations(SDKS));rng.shuffle(orders[n])
    for rep in range(5 if mode=='physical' else 1):
        names=list(SHAPES);rng.shuffle(names)
        for n in names:rows += [dict(kind='measurement' if mode=='physical' else 'smoke',sdk=s,scenario=n,repetition=rep) for s in orders[n][rep]]
    return [dict(j,name=f"{i:03}-{j['kind']}-{j['sdk']}-{j['scenario']}-{j['repetition']}") for i,j in enumerate(rows)]


def options(job):
    validation,_,warmup,samples=shared.options(job)
    observed=job['sdk']=='llgl' and job['kind'] in ('observe','audit','validation','core-control','sync-control','sync-disabled',
        'missing-pipeline','skip-release','wrong-pipeline','skip-region','wrong-scissor','corrupt-readback','observer-disabled')
    return validation,observed,warmup,samples


def validate_output(value,job,r):
    validation,observed,warmup,samples=options(job);a=shared.qualification(r);d=a['selected'];kind=job['kind']
    probe=kind in ('probe','observe');control=kind in correctness.TOKENS;special=probe or control
    count=0 if special else WORK[job['scenario']];images=0 if special else warmup+samples
    p.exact(value,dict(schema_version=1,sdk=job['sdk'],api='vulkan',configuration='Release',category='cpu_api',
        contract='vulkan_pipeline_create_public_release_use_checked_v1',scenario=job['scenario'],
        validation=validation,probe=probe,warmup=warmup,sample_count=samples,gpu_ms=None,
        device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
        native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
        width=65,height=37,texel_bytes=4,pipelines_per_iteration=count,created_pipelines=images*count,
        released_pipeline_handles=images*count,used_pipeline_regions=images*count,
        includes_shader_object_create_release=True,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,
        cache_state='uncontrolled_driver_cache_repeated_public_creation',
        verified_images=images,verified_bytes=9620*images,readback_verified=not special,shader_header_sha256=r['shader_header_sha256'],columns=COLUMNS,
        loaded_validation_layer=a['validation_library'] if validation else ''))
    p.exact(value['environment'],a['environment'])
    values=value['samples_ms'];require(type(values) is list and len(values)==samples,'Invalid PSO interval count')
    validate_intervals(values)
    if job['sdk']!='llgl':
        p.exact(value,dict(preference='high',source_fingerprint=r['readiness']['profile_fingerprints']['pso'],
            SDK_source_fingerprint=a['SDK_source_fingerprint'],SDL_commit=a['SDL_commit'],identity_attribution=shared.w.ATTRIBUTION,
            requested_instance_api_version=4198400))
        p.exact(value['environment'],dict(gpu=d['name'],software_renderer=r['mode']=='software-diagnostic',device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None))
    else:
        shared.validate_llgl_identity(value,job,r,'P3-LBEN-03-pso-timer',observed)
        p.exact(value,dict(scissor=[5,3,55,31],vertices_per_pipeline=3,first_vertex=0,first_instance=0,
            created_shader_handles=2*images*count,released_shader_handles=2*images*count,native_pipeline_count_verified=False,driver_cache_state_known=False,
            requested_instance_api_version=None,clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',
            texture_bind_policy='color_attachment_sampled_copy_src',frame_sha256=r['reference'][job['scenario']] if observed and not special else []))
        native=dict(native_draw_calls=images*count,native_forwarded_draw_calls=images*count,native_instances=images*count,
            native_vertices=3*images*count,native_bad_arguments=0,native_min_instances=1 if images else 0,native_max_instances=1 if images else 0)
        p.exact(value,{k:v if observed else None for k,v in native.items()})
    return value


def rejected(job,code,stdout,stderr,r):
    require(type(code) is int and code==1 and not stdout,'Rejection succeeded or emitted samples')
    if job['sdk']!='llgl':p.q.control(job,code,stdout,stderr);return
    token={'wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':correctness.policy.wrong_kind_message(r['mode']),
        'missing-layer':b'Required comparison validation layer absent','sync-disabled':b'Expected validation control diagnostic absent',
        'missing-pipeline':b'Public pipeline creation count differs','skip-release':b'Public pipeline release count differs',
        'wrong-pipeline':b'Full changing PSO stripe reference mismatch','skip-region':b'Native PSO draw count/arguments differ',
        'wrong-scissor':b'Full changing PSO stripe reference mismatch','corrupt-readback':b'Full changing PSO stripe reference mismatch',
        'observer-disabled':b'Required runtime module absent or ambiguous'}[job['kind']]
    require(token in stderr,'Unrelated LLGL PSO rejection')


def summarize(rows,mode):
    if mode=='software-diagnostic':
        require(not any(x['job']['kind']=='measurement' for x in rows),'Software PSO intervals promoted to measurement');return []
    groups=[]
    for scenario in SHAPES:
        for sdk in SDKS:
            selected=[x for x in rows if x['job']['kind']=='measurement' and x['job']['scenario']==scenario and x['job']['sdk']==sdk]
            require([x['job']['repetition'] for x in selected]==list(range(5)),'Missing/duplicate fresh PSO repetitions')
            for entry in selected:
                require(len(entry['result']['samples_ms'])==30,'Missing retained PSO samples')
                validate_intervals(entry['result']['samples_ms'])
            for i,column in enumerate(COLUMNS):
                groups.append(dict(scenario=scenario,sdk=sdk,column=column,category='cpu_api',
                    statistics=shared.scalar_stats([[row[i] for row in entry['result']['samples_ms']] for entry in selected])))
    return groups


def validate_scope(r):
    validate_inputs(r['readiness'],r['llgl_correctness'],r['mode']);physical=r['mode']=='physical'
    p.exact(r,dict(schema_version=1,task=TASK,status='completed',inputs_unchanged=True,
        campaign_kind='physical-campaign' if physical else 'software-smoke',performance_accepted=physical,
        measurement_samples=900 if physical else 0,diagnostic_rows=24 if physical else 42,
        physical_GPU_accepted=False,P3_LBEN_03_completed=False,historical_timings_reused=False,overall_SDK_ranking=False,
        GPU_timestamp_measurement=False,SDL_native_handle_verified=False,SDK_SDL_measurement_module_snapshot_observed=False,
        shader_header_sha256=r['readiness']['shader_header_sha256']))


if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('stage',choices=('run','smoke','check'))
    a.add_argument('--readiness',type=pathlib.Path);a.add_argument('--llgl-correctness',type=pathlib.Path)
    a.add_argument('--output',type=pathlib.Path,required=True);a.add_argument('--live',action='store_true');v=a.parse_args()
    if v.stage=='check':print(shared.json.dumps(check(v.output,v.live),indent=2))
    else:
        require(v.readiness is not None and v.llgl_correctness is not None,'Explicit same-revision prerequisite evidence required');shared.run(v,self())
