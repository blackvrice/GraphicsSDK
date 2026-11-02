"""Fresh Linux three-SDK draw/instancing CPU campaign; software statistics excluded."""
import argparse
import itertools
import math
import pathlib
import random
import sys

import linux_upload_campaign as shared
import linux_llgl_draw as correctness
import port_linux_draw_campaign as port
from linux_comparison import ROOT,require,read,sha
p=shared.p
PROFILE='draw'
TASK='P3-LBEN-02-draw-campaign'
COMPLETION_FIELD='P3_LBEN_02_completed'
CORRECTNESS=correctness
PLAN='benchmarks/phase3_comparison/linux-draw-campaign.json'
WINDOWS_PLAN='benchmarks/phase3_comparison/competitors/llgl-draw-comparison.json'
REFERENCE_FILE='frame-reference.json'
REFERENCE_SOURCE=None
SOURCE_FILE='tools/linux_draw_campaign.py'
CMAKE_DIRECTORY='benchmarks/linux_draw_campaign'
TARGET='linux_draw_compare'
KIND_VARIABLE='GFX_LLGL_DRAW_COMPARE_KIND'
OWN_HEADERS=('benchmarks/common/LinuxEnvironment.hpp','benchmarks/common/Adapter.hpp',
    'benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/vulkan_draw/Frame.hpp','tests/fixtures/SpirvFixtures.hpp')
SDKS=shared.SDKS
WORK=correctness.SHAPES
SHAPES={n:[65,37,4] for n in WORK}
COLUMNS=['record_ms','submit_ms','wait_ms','total_ms']
SUCCESS=shared.SUCCESS|{'audit'}
FULL_TOTALS=(1623,15613260)
SMOKE_TOTALS=(84,808080)
MEASUREMENT_SAMPLES=1350
DIAGNOSTIC_ROWS=(63,36)


def reference():return correctness.references()
def initial_fields():return dict(shader_header_sha256=sha(ROOT/'tests/fixtures/SpirvFixtures.hpp'))
def self():return sys.modules[__name__]
def validate_inputs(ready,llgl,mode):return shared.validate_inputs(ready,llgl,mode,PROFILE,CORRECTNESS)
def arguments(job,r):return shared.arguments(job,r,self())
def environment(job,r):return shared.environment(job,r,self())
def validate_success(row,stdout,stderr,job,r):return shared.validate_success(row,stdout,stderr,job,r,self())
def audit(build,call,stdout):return shared.audit(build,call,stdout,self())
def check(folder,live=False):return shared.check(folder,live,self())


def validate_plan(value):
    geometry=dict(shape=[65,37,4],scissor=[5,3,55,31],vertices_per_instance=3,first_vertex=0,first_instance=0,
        reference='full_changing_red_green_triangle_blue_cyan_clear')
    expected=dict(schema_version=1,task='P3-LBEN-02',scope='linux-x86_64-qualified-three-sdk-draw',sdks=list(SDKS),scenarios=list(SHAPES),
        validation=dict(warmup=1,samples=3,required_core_and_sync=True),
        performance=dict(warmup=5,samples=30,repetitions=5,seed=20261027,order='seeded_scenario_blocks_five_distinct_three_SDK_permutations'),
        smoke=dict(warmup=1,samples=3,repetitions=1,statistics_exported=False),serialized_processes=True,
        observer_free_measurements=True,loader_trace_in_measurements=False,validation_timings_excluded=True,
        historical_timings_reused=False,overall_SDK_ranking=False,GPU_timestamp_measurement=False,
        SDL_native_handle_verified=False,physical_qualification_required_for_measurements=True,
        columns=COLUMNS,work=WORK,geometry=geometry)
    require(p.same(value,expected),'Linux draw contract changed')
    prior=read(ROOT/WINDOWS_PLAN)
    require(all(p.same(value[k],prior[k]) for k in ('performance','validation','work','geometry'))
            and p.same(value['columns'],prior['timing']['columns']),'Frozen draw interval/work/repetition contract changed')
    return value


def schedule(plan,mode):
    validate_plan(plan);p.identity_prefix(mode)
    rows=[dict(kind='probe',sdk=s,scenario='probe',repetition=0) for s in SDKS]
    rows += [dict(kind='observe',sdk='llgl',scenario='probe',repetition=0)]
    rows += [dict(kind='audit',sdk='llgl',scenario=n,repetition=0) for n in SHAPES]
    rows += [dict(kind='validation',sdk=s,scenario=n,repetition=0) for n in SHAPES for s in SDKS]
    rows += [dict(kind=k,sdk=s,scenario='draw1',repetition=0) for s in SDKS for k in ('missing-layer','wrong-uuid','wrong-kind')]
    rows += [dict(kind=k,sdk='llgl',scenario=n,repetition=0) for k,n in (
        ('core-control','core-control'),('sync-control','sync-control'),('sync-disabled','sync-control'),
        ('wrong-call-count','wrong-call-count'),('wrong-instance-count','wrong-instance-count'),('wrong-scissor','wrong-scissor'),
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
        'wrong-call-count','wrong-instance-count','wrong-scissor','corrupt-readback','observer-disabled')
    return validation,observed,warmup,samples


def validate_output(value,job,r):
    validation,observed,warmup,samples=options(job);a=shared.qualification(r);d=a['selected'];kind=job['kind']
    probe=kind in ('probe','observe');control=kind in correctness.TOKENS;special=probe or control
    calls,instances=(0,0) if special else WORK[job['scenario']];images=0 if special else warmup+samples
    p.exact(value,dict(schema_version=1,sdk=job['sdk'],api='vulkan',configuration='Release',category='end_to_end',
        contract='vulkan_scissored_fullscreen_triangle_draw_fence_each_sample_readback_v1',scenario=job['scenario'],
        validation=validation,probe=probe,warmup=warmup,sample_count=samples,gpu_ms=None,
        device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,
        native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
        width=65,height=37,texel_bytes=4,scissor=[5,3,55,31],vertices_per_instance=3,first_vertex=0,first_instance=0,
        draw_calls_per_frame=calls,instances_per_call=instances,vertices_per_frame=3*calls*instances,submitted_draw_calls=images*calls,
        verified_images=images,verified_bytes=9620*images,readback_verified=not special,shader_header_sha256=r['shader_header_sha256'],columns=COLUMNS,
        loaded_validation_layer=a['validation_library'] if validation else ''))
    p.exact(value['environment'],a['environment'])
    values=value['samples_ms'];require(type(values) is list and len(values)==samples and all(shared.positive_number(v) for v in values),'Invalid CPU draw interval rows')
    intervals=value['CPU_intervals_ms'];require(type(intervals) is list and len(intervals)==samples,'CPU partition count differs')
    for row,total in zip(intervals,values):
        require(type(row) is list and len(row)==4 and all(type(v) in (int,float) and math.isfinite(v) and v>=0 for v in row)
            and row[-1]==total and abs(sum(row[:3])-total)<=max(1e-6,total*1e-10),'CPU draw intervals do not partition total')
    if job['sdk']!='llgl':
        p.exact(value,dict(preference='high',source_fingerprint=r['readiness']['profile_fingerprints']['draw'],
            SDK_source_fingerprint=a['SDK_source_fingerprint'],SDL_commit=a['SDL_commit'],identity_attribution=shared.w.ATTRIBUTION,
            requested_instance_api_version=4198400))
        p.exact(value['environment'],dict(gpu=d['name'],software_renderer=r['mode']=='software-diagnostic',device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None))
    else:
        shared.validate_llgl_identity(value,job,r,TASK,observed)
        p.exact(value,dict(requested_instance_api_version=None,clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',
            texture_bind_policy='color_attachment_sampled_copy_src',frame_sha256=r['reference'] if observed and not special else []))
        native=dict(native_draw_calls=images*calls,native_forwarded_draw_calls=images*calls,native_instances=images*calls*instances,
            native_vertices=3*images*calls*instances,native_bad_arguments=0,native_min_instances=instances if images else 0,native_max_instances=instances if images else 0)
        p.exact(value,{k:v if observed else None for k,v in native.items()})
    return value


def rejected(job,code,stdout,stderr,r):
    require(type(code) is int and code==1 and not stdout,'Rejection succeeded or emitted samples')
    if job['sdk']!='llgl':p.q.control(job,code,stdout,stderr);return
    token={'wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':correctness.policy.wrong_kind_message(r['mode']),
        'missing-layer':b'Required comparison validation layer absent','sync-disabled':b'Expected validation control diagnostic absent',
        'wrong-call-count':b'Native draw count/arguments differ from contract','wrong-instance-count':b'Native draw count/arguments differ from contract',
        'wrong-scissor':b'Full changing draw reference mismatch','corrupt-readback':b'Full changing draw reference mismatch',
        'observer-disabled':b'Required runtime module absent or ambiguous'}[job['kind']]
    require(token in stderr,'Unrelated LLGL draw rejection')


def summarize(rows,mode):
    if mode=='software-diagnostic':
        require(not any(x['job']['kind']=='measurement' for x in rows),'Software draw intervals promoted to measurement');return []
    groups=[]
    for scenario in SHAPES:
        for sdk in SDKS:
            selected=[x for x in rows if x['job']['kind']=='measurement' and x['job']['scenario']==scenario and x['job']['sdk']==sdk]
            require([x['job']['repetition'] for x in selected]==list(range(5)),'Missing/duplicate fresh draw repetitions')
            for entry in selected:
                intervals=entry['result']['CPU_intervals_ms'];totals=entry['result']['samples_ms']
                require(len(intervals)==len(totals)==30,'Missing retained draw samples')
                for row,total in zip(intervals,totals):
                    require(type(row) is list and len(row)==4 and all(type(v) in (int,float) and math.isfinite(v) and v>=0 for v in row)
                        and shared.positive_number(total) and row[-1]==total and abs(sum(row[:3])-total)<=max(1e-6,total*1e-10),'Invalid retained draw partitions')
            for i,column in enumerate(COLUMNS):
                groups.append(dict(scenario=scenario,sdk=sdk,column=column,category='end_to_end',
                    statistics=shared.scalar_stats([[row[i] for row in entry['result']['CPU_intervals_ms']] for entry in selected])))
    return groups


def validate_scope(r):
    validate_inputs(r['readiness'],r['llgl_correctness'],r['mode']);physical=r['mode']=='physical'
    p.exact(r,dict(schema_version=1,task=TASK,status='completed',inputs_unchanged=True,
        campaign_kind='physical-campaign' if physical else 'software-smoke',performance_accepted=physical,
        measurement_samples=1350 if physical else 0,diagnostic_rows=36 if physical else 63,
        physical_GPU_accepted=False,P3_LBEN_02_completed=False,historical_timings_reused=False,overall_SDK_ranking=False,
        GPU_timestamp_measurement=False,SDL_native_handle_verified=False,SDK_SDL_measurement_module_snapshot_observed=False,
        shader_header_sha256=r['readiness']['shader_header_sha256']))


if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('stage',choices=('run','smoke','check'))
    a.add_argument('--readiness',type=pathlib.Path);a.add_argument('--llgl-correctness',type=pathlib.Path)
    a.add_argument('--output',type=pathlib.Path,required=True);a.add_argument('--live',action='store_true');v=a.parse_args()
    if v.stage=='check':print(shared.json.dumps(check(v.output,v.live),indent=2))
    else:
        require(v.readiness is not None and v.llgl_correctness is not None,'Explicit same-revision prerequisite evidence required');shared.run(v,self())
