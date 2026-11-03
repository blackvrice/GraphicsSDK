"""Fresh Linux three-SDK resource create/public-release CPU campaign; software statistics excluded."""
import argparse
import itertools
import math
import pathlib
import random
import sys

import linux_upload_campaign as shared
import linux_llgl_allocation as correctness
import port_linux_allocation_campaign as port
from linux_comparison import ROOT,require,read,sha
p=shared.p
PROFILE='allocation'
TASK='P3-LBEN-04-allocation-campaign'
COMPLETION_FIELD='P3_LBEN_04_completed'
CORRECTNESS=correctness
PLAN='benchmarks/phase3_comparison/linux-allocation-campaign.json'
WINDOWS_PLAN='benchmarks/phase3_comparison/competitors/llgl-allocation-comparison.json'
REFERENCE_FILE='payload-reference.json'
REFERENCE_SOURCE=correctness.REFERENCE
SOURCE_FILE='tools/linux_allocation_campaign.py'
EXTRA_SOURCES=('tools/linux_llgl_allocation.py','tools/linux_build_policy.py')
CMAKE_DIRECTORY='benchmarks/linux_allocation_campaign'
TARGET='linux_allocation_compare'
KIND_VARIABLE='GFX_LLGL_ALLOCATION_COMPARE_KIND'
OWN_HEADERS=('benchmarks/common/LinuxEnvironment.hpp','benchmarks/common/Adapter.hpp',
    'benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/vulkan_allocation/Payload.hpp')
SDKS=shared.SDKS
WORK=correctness.SHAPES
SHAPES={n:[65,37,4] if n.startswith('texture') else [4096] for n in WORK}
COLUMNS=['create_ms','public_release_ms']
SUCCESS=shared.SUCCESS|{'audit'}
FULL_TOTALS=(541541,7427776356)
SMOKE_TOTALS=(28028,384432048)
MEASUREMENT_SAMPLES=1800
DIAGNOSTIC_ROWS=(84,48)
EXTRA_COUNTERS=('verified_buffers','verified_resources')


def reference():return correctness.references()
def initial_fields():return dict(verified_buffers=0,verified_resources=0)
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
    expected=dict(schema_version=1,task='P3-LBEN-04',scope='linux-x86_64-qualified-three-sdk-allocation',sdks=list(SDKS),scenarios=list(SHAPES),
        validation=dict(warmup=1,samples=3,required_core_and_sync=True),
        performance=dict(warmup=5,samples=30,repetitions=5,seed=20261027,order='seeded_scenario_blocks_five_distinct_three_SDK_permutations'),
        smoke=dict(warmup=1,samples=3,repetitions=1,statistics_exported=False),serialized_processes=True,
        observer_free_measurements=True,loader_trace_in_measurements=False,validation_timings_excluded=True,
        historical_timings_reused=False,overall_SDK_ranking=False,GPU_timestamp_measurement=False,
        SDL_native_handle_verified=False,physical_qualification_required_for_measurements=True,
        columns=COLUMNS,work=WORK,resources=dict(buffer_bytes=4096,texture_shape=[65,37,4],reference='full_payload_changes_with_iteration_resource_and_byte',
            usage_policy=dict(buffer=dict(graphicssdk='sdk_general_device_buffer',sdl='sdl_vertex_buffer'),texture='sampled_rgba8_2d')),allocation_policy='public_device_resource_default_policy',
        native_allocation_count_verified=False,allocator_cache_state_known=False,GPU_memory_reclamation_verified=False)
    require(p.same(value,expected),'Linux allocation contract changed')
    prior=read(ROOT/WINDOWS_PLAN)
    require(all(p.same(value[k],prior[k]) for k in ('performance','validation','work','resources','allocation_policy','native_allocation_count_verified','allocator_cache_state_known','GPU_memory_reclamation_verified'))
            and p.same(value['columns'],prior['timing']['columns']),'Frozen allocation interval/work/repetition contract changed')
    return value


def schedule(plan,mode):
    validate_plan(plan);p.identity_prefix(mode)
    rows=[dict(kind='probe',sdk=s,scenario='probe',repetition=0) for s in SDKS]
    rows += [dict(kind='observe',sdk='llgl',scenario='probe',repetition=0)]
    rows += [dict(kind='audit',sdk='llgl',scenario=n,repetition=0) for n in SHAPES]
    rows += [dict(kind='validation',sdk=s,scenario=n,repetition=0) for n in SHAPES for s in SDKS]
    rows += [dict(kind=k,sdk=s,scenario='buffer1',repetition=0) for s in SDKS for k in ('missing-layer','wrong-uuid','wrong-kind')]
    rows += [dict(kind=k,sdk='llgl',scenario=n,repetition=0) for k,n in (
        ('core-control','core-control'),('sync-control','sync-control'),('sync-disabled','sync-control'),
        ('missing-resource','missing-resource'),('alias-resource','alias-resource'),('skip-release','skip-release'),
        ('wrong-resource-payload','wrong-resource-payload'),('stale-iteration','stale-iteration'),('skip-copy','skip-copy'),
        ('corrupt-readback','corrupt-readback'),('short-readback','short-readback'),('observer-disabled','probe'))]
    rng=random.Random(plan['performance']['seed']);orders={}
    for n in SHAPES:orders[n]=list(itertools.permutations(SDKS));rng.shuffle(orders[n])
    for rep in range(5 if mode=='physical' else 1):
        names=list(SHAPES);rng.shuffle(names)
        for n in names:rows += [dict(kind='measurement' if mode=='physical' else 'smoke',sdk=s,scenario=n,repetition=rep) for s in orders[n][rep]]
    return [dict(j,name=f"{i:03}-{j['kind']}-{j['sdk']}-{j['scenario']}-{j['repetition']}") for i,j in enumerate(rows)]


def options(job):
    validation,_,warmup,samples=shared.options(job)
    observed=job['sdk']=='llgl' and job['kind'] in ('observe','audit','validation','core-control','sync-control','sync-disabled',
        'missing-resource','alias-resource','skip-release','wrong-resource-payload','stale-iteration','skip-copy','corrupt-readback','short-readback','observer-disabled')
    return validation,observed,warmup,samples


def validate_output(value,job,r):
    validation,observed,warmup,samples=options(job);a=shared.qualification(r);d=a['selected'];kind=job['kind']
    probe=kind in ('probe','observe');control=kind in correctness.TOKENS;special=probe or control
    count=0 if special else WORK[job['scenario']];texture=job['scenario'].startswith('texture');total=count*(warmup+samples)
    p.exact(value,dict(schema_version=1,sdk=job['sdk'],api='vulkan',configuration='Release',category='cpu_api',
        contract='vulkan_resource_create_public_release_full_readback_v1',scenario=job['scenario'],
        validation=validation,probe=probe,warmup=warmup,sample_count=samples,gpu_ms=None,
        device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
        native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
        resource_kind='none' if control else 'texture' if texture else 'buffer',
        width=65 if texture else 0,height=37 if texture else 0,texel_bytes=4 if texture else 0,
        buffer_bytes=4096 if not control and not texture else 0,resources_per_iteration=count,
        created_resources=total,released_resource_handles=total,used_resources=total,verified_resources=total,
        allocation_policy='public_device_resource_default_policy',GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,
        native_allocation_count_verified=False,verified_images=total if texture else 0,verified_buffers=0 if texture else total,
        verified_bytes=total*(9620 if texture else 4096),readback_verified=not special,columns=COLUMNS,
        loaded_validation_layer=a['validation_library'] if validation else ''))
    p.exact(value['environment'],a['environment'])
    values=value['samples_ms'];require(type(values) is list and len(values)==samples,'Invalid allocation interval count')
    validate_intervals(values)
    if job['sdk']!='llgl':
        p.exact(value,dict(preference='high',source_fingerprint=r['readiness']['profile_fingerprints']['allocation'],
            SDK_source_fingerprint=a['SDK_source_fingerprint'],SDL_commit=a['SDL_commit'],identity_attribution=shared.w.ATTRIBUTION,
            requested_instance_api_version=4198400,usage_policy='sampled_rgba8_2d' if texture else 'sdl_vertex_buffer' if job['sdk']=='sdl' else 'sdk_general_device_buffer'))
        p.exact(value['environment'],dict(gpu=d['name'],software_renderer=r['mode']=='software-diagnostic',device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None))
    else:
        shared.validate_llgl_identity(value,job,r,TASK,observed)
        p.exact(value,dict(unique_native_resources_checked=total,native_resource_handles_verified=not special,
            requested_instance_api_version=None,usage_policy='none' if control else 'sampled_rgba8_2d' if texture else 'llgl_vertex_buffer_copy_src_dst',
            buffer_cpu_access_flags=0,initial_data_supplied=False,copy_submission_policy='separate_upload_download_completed_fence_idle',
            staging_in_create_release_boundary=False,row_stride=0,layer_stride=0,allocator_cache_state_known=False,
            payload_sha256=r['reference']['sha256'][job['scenario']] if observed and not special else [],native_draw_calls=0 if observed else None))
    return value


def rejected(job,code,stdout,stderr,r):
    require(type(code) is int and code==1 and not stdout,'Rejection succeeded or emitted samples')
    if job['sdk']!='llgl':p.q.control(job,code,stdout,stderr);return
    token={'wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':correctness.policy.wrong_kind_message(r['mode']),
        'missing-layer':b'Required comparison validation layer absent','sync-disabled':b'Expected validation control diagnostic absent',
        'missing-resource':b'Public resource creation count differs','alias-resource':b'Duplicate live public resource handle',
        'skip-release':b'Public resource release count differs','wrong-resource-payload':b'Full changing resource readback mismatch',
        'stale-iteration':b'Full changing resource readback mismatch','skip-copy':b'Full changing resource readback mismatch',
        'corrupt-readback':b'Full changing resource readback mismatch','short-readback':b'Incomplete full resource readback',
        'observer-disabled':b'Required runtime module absent or ambiguous'}[job['kind']]
    require(token in stderr,'Unrelated LLGL allocation rejection')


def summarize(rows,mode):
    if mode=='software-diagnostic':
        require(not any(x['job']['kind']=='measurement' for x in rows),'Software allocation intervals promoted to measurement');return []
    groups=[]
    for scenario in SHAPES:
        for sdk in SDKS:
            selected=[x for x in rows if x['job']['kind']=='measurement' and x['job']['scenario']==scenario and x['job']['sdk']==sdk]
            require([x['job']['repetition'] for x in selected]==list(range(5)),'Missing/duplicate fresh allocation repetitions')
            for entry in selected:
                require(len(entry['result']['samples_ms'])==30,'Missing retained allocation samples')
                validate_intervals(entry['result']['samples_ms'])
            for i,column in enumerate(COLUMNS):
                groups.append(dict(scenario=scenario,sdk=sdk,column=column,category='cpu_api',
                    statistics=shared.scalar_stats([[row[i] for row in entry['result']['samples_ms']] for entry in selected])))
    return groups


def validate_scope(r):
    validate_inputs(r['readiness'],r['llgl_correctness'],r['mode']);physical=r['mode']=='physical'
    p.exact(r,dict(schema_version=1,task=TASK,status='completed',inputs_unchanged=True,
        campaign_kind='physical-campaign' if physical else 'software-smoke',performance_accepted=physical,
        measurement_samples=1800 if physical else 0,diagnostic_rows=48 if physical else 84,
        verified_buffers=541541 if physical else 28028,verified_resources=1083082 if physical else 56056,
        physical_GPU_accepted=False,P3_LBEN_04_completed=False,historical_timings_reused=False,overall_SDK_ranking=False,
        GPU_timestamp_measurement=False,SDL_native_handle_verified=False,SDK_SDL_measurement_module_snapshot_observed=False))


if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('stage',choices=('run','smoke','check'))
    a.add_argument('--readiness',type=pathlib.Path);a.add_argument('--llgl-correctness',type=pathlib.Path)
    a.add_argument('--output',type=pathlib.Path,required=True);a.add_argument('--live',action='store_true');v=a.parse_args()
    if v.stage=='check':print(shared.json.dumps(check(v.output,v.live),indent=2))
    else:
        require(v.readiness is not None and v.llgl_correctness is not None,'Explicit same-revision prerequisite evidence required');shared.run(v,self())
