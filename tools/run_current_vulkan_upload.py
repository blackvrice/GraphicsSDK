"""P3-BEN-03: full current-product upload campaign after BEN-02 qualification."""
import argparse
import json
import os
import pathlib
import platform
import random
import shutil
import subprocess
import time

import run_vulkan_comparison as protocol
from prepare_vulkan_comparison import ROOT, read, save, sha, record, require

PLAN=ROOT/'benchmarks/phase3_comparison/upload.json'
SDKS=protocol.SDKS
SHAPES=protocol.MODULES['upload'].SHAPES


def validate_plan(p):
    require(type(p['schema_version']) is int and p['schema_version']==1 and p['task']=='P3-BEN-03'
            and p['prerequisite']=='P3-BEN-02' and p['scope']=='windows-nvidia-vulkan-current-upload'
            and p['profile']=='upload' and p['scenarios']==list(SHAPES), 'Wrong upload campaign scope/coverage')
    require(p['validation']==dict(warmup=1,samples=3,required_core_and_sync=True)
            and type(p['validation']['warmup']) is type(p['validation']['samples']) is int
            and p['validation']['required_core_and_sync'] is True, 'Required validation contract drift')
    require(p['performance']==dict(warmup=5,samples=30,repetitions=5,seed=20261027,
            order='seeded_scenario_blocks_alternating_adjacent_SDK_pairs_each_repetition')
            and all(type(p['performance'][k]) is int for k in ('warmup','samples','repetitions','seed')), 'Repetition/warmup/sample/order drift')
    require(p['controls']==['missing-layer','wrong-target'] and p['statistics']==dict(quantile='linear_interpolation_at_(N-1)*q',
            retain_all_raw_samples=True,samples_removed=0,process_medians=True), 'Controls/statistics drift')
    require(p['statistics']['retain_all_raw_samples'] is p['statistics']['process_medians'] is True
            and type(p['statistics']['samples_removed']) is int, 'Statistics policy types changed')
    require(p['timing']==dict(category='end_to_end',column='total_ms',
            includes=['staging_map_and_CPU_payload_copy','record','submit','completed_upload_fence'],
            excludes=['payload_generation','full_changing_readback','resource_setup','teardown','warmup','validation'],
            GPU_timestamp_measurement=False,layout_difference='SDK padded staging versus SDL tight transfer layout',
            odd_case='65x37 alignment edge case; small transfer overhead is part of its result'), 'Timing boundary drift')
    require(p['timing']['GPU_timestamp_measurement'] is False, 'CPU wall time cannot become GPU timestamps')
    require(p['historical_timings_reused'] is p['overall_SDK_ranking'] is p['SDL_native_handle_verified'] is False
            and p['completed_workload_tasks']==['P3-BEN-03'] and p['next_gate']=='P3-BEN-04', 'Campaign promoted to another workload/ranking')
    return p


def schedule(plan):
    jobs=[]
    for scenario in plan['scenarios']:
        for sdk in SDKS:jobs.append(dict(profile='upload',sdk=sdk,scenario=scenario,kind='validation',repetition=0))
    for sdk in SDKS:
        for kind in plan['controls']:jobs.append(dict(profile='upload',sdk=sdk,scenario='odd',kind=kind,repetition=0))
    rng=random.Random(plan['performance']['seed']);first={s:rng.randrange(2) for s in plan['scenarios']}
    for rep in range(plan['performance']['repetitions']):
        scenarios=list(plan['scenarios']);rng.shuffle(scenarios)
        for scenario in scenarios:
            for sdk in (SDKS if (first[scenario]+rep)%2==0 else SDKS[::-1]):
                jobs.append(dict(profile='upload',sdk=sdk,scenario=scenario,kind='measurement',repetition=rep))
    for i,j in enumerate(jobs):j['name']=f"{i:03}-{j['kind']}-{j['sdk']}-{j['scenario']}-{j['repetition']}"
    return jobs


def summarize(rows,plan):
    groups=[]
    for scenario in plan['scenarios']:
        for sdk in SDKS:
            selected=[r for r in rows if r['job']['kind']=='measurement' and r['job']['scenario']==scenario and r['job']['sdk']==sdk]
            require(len(selected)==plan['performance']['repetitions'] and {r['job']['repetition'] for r in selected}==set(range(plan['performance']['repetitions'])), 'Missing/duplicate independent repetition')
            require(all(len(r['result']['samples_ms'])==plan['performance']['samples'] for r in selected), 'Wrong raw sample count')
            require(all(type(v) in (int,float) and v>0 for r in selected for v in r['result']['samples_ms']), 'Invalid completed upload wall time')
            groups.append(dict(scenario=scenario,sdk=sdk,shape=list(SHAPES[scenario]),category='end_to_end',column='total_ms',
                               statistics=protocol.scalar_stats([r['result']['samples_ms'] for r in selected])))
    return groups


def arguments(job,prepared,plan,gpu):
    validation=job['kind'] in ('validation','missing-layer');mode=plan['validation' if validation else 'performance']
    expected='GraphicsSDK BEN03 absent GPU' if job['kind']=='wrong-target' else gpu
    return [prepared['binaries']['upload']['path'],job['sdk'],job['scenario'],'high',str(mode['warmup']),str(mode['samples']),str(int(validation)),expected]


def check(folder,live=False):
    r=read(folder/'receipt.json');plan=validate_plan(read(folder/'upload.json'))
    require(r['task']=='P3-BEN-03' and r['status']=='completed' and r['inputs_unchanged'] is True
            and r['validation_timings_excluded'] is True and r['historical_timings_reused'] is r['overall_SDK_ranking'] is r['SDL_native_handle_verified'] is False
            and r['completed_workload_tasks']==['P3-BEN-03'], 'Incomplete or promoted upload campaign')
    require(sha(folder/'upload.json')==r['plan_sha256'], 'Upload plan changed')
    protocol.check(folder/'qualification')
    q=read(folder/'qualification/receipt.json');prepared=read(folder/'qualification/prepared/receipt.json')
    require(sha(folder/'qualification/receipt.json')==r['qualification_receipt_sha256']
            and all(r[k]==q[k] for k in ('identity','expected_gpu','SDK_source_fingerprint','profile_fingerprints','SDL_commit','validation_library','validation_library_sha256')),
            'Qualification/measurement provenance changed')
    require(sha(folder/'runtime-receipt.json')==r['runtime_receipt_sha256']==q['runtime_receipt_sha256'], 'Runtime binding changed')
    require((folder/'settings/vk_layer_settings.txt').read_text()==protocol.SETTINGS, 'Required sync validation settings changed')
    require(sha(folder/'environment.json')==r['environment_sha256'], 'Process environment changed')
    environment=read(folder/'environment.json');clean=environment['performance_overrides'];validated=environment['validation_overrides']
    # Reconstruct recorded path values without requiring those Windows paths to
    # exist during offline replay on another OS.
    _,_,expected_env=protocol.environments(pathlib.Path(r['output_directory']),pathlib.Path(r['validation_library']),{})
    require(protocol.same_overrides(clean,expected_env['performance_overrides']) and protocol.same_overrides(validated,expected_env['validation_overrides']), 'Layer isolation changed')
    jobs=schedule(plan);require(len(r['runs'])==len(jobs), 'Missing/extra process')
    last_end=0;successful=[]
    for row,job in zip(r['runs'],jobs):
        require(row['job']==job, 'Workload/order/repetition changed')
        require(type(row['pid']) is int and row['pid']>0 and last_end<=row['started']<=row['finished'], 'Overlapping/non-process execution')
        last_end=row['finished'];validation=job['kind'] in ('validation','missing-layer')
        overrides=dict(validated if validation else clean)
        if job['kind']=='missing-layer':overrides['VK_LAYER_PATH']=str(pathlib.Path(r['output_directory'])/'missing')
        require(protocol.same_overrides(row['overrides'],overrides) and row['argv']==arguments(job,prepared,plan,r['expected_gpu']), 'Command/environment isolation changed')
        require(sha(folder/row['stdout'])==row['stdout_sha256'] and sha(folder/row['stderr'])==row['stderr_sha256'], 'Raw output changed')
        stdout=(folder/row['stdout']).read_bytes();stderr=(folder/row['stderr']).read_bytes()
        if job['kind'] in ('validation','measurement'):
            require(row['returncode']==0, 'Successful workload exit required')
            result=protocol.validate_output(job,stdout,stderr,r,plan)
            require(result==row['result'], 'Raw/parsed upload result changed');successful.append(row)
        else:protocol.validate_control(job,row['returncode'],stdout,stderr)
    require(r['groups']==summarize(successful,plan), 'Statistics do not reproduce from current measurement-only raw samples')
    require(type(r['measurement_samples']) is int and r['measurement_samples']==1200, 'Wrong full-workload sample total')
    require(r['verified_bytes']==sum(x['result']['verified_bytes'] for x in successful)
            and r['verified_images']==sum(x['result']['verified_images'] for x in successful), 'Readback totals changed')
    if live:
        protocol.check(pathlib.Path(r['qualification_evidence']),live=True)
        require(protocol.inputs.unchanged(r['frozen_inputs']), 'Frozen runtime/binary/source changed')
    return dict(task='P3-BEN-03',upload_accepted=True,validation_processes=8,control_processes=4,measurement_processes=40,
                measurement_samples=1200,verified_images=r['verified_images'],verified_bytes=r['verified_bytes'],overall_SDK_ranking=False)


def run(args):
    require(platform.system()=='Windows', 'Windows host required')
    qualification=args.qualification.resolve();protocol.check(qualification,live=True)
    q=read(qualification/'receipt.json');prepared=read(qualification/'prepared/receipt.json');plan=validate_plan(read(PLAN))
    runtime,dll=protocol.runtime_inputs(args.runtime_receipt.resolve())
    require(sha(args.runtime_receipt)==q['runtime_receipt_sha256'] and sha(dll)==q['validation_library_sha256'], 'Qualified runtime changed')
    out=args.output.resolve();require(out.is_relative_to(ROOT/'out') and not out.exists(), 'Fresh output under out/ required')
    out.mkdir(parents=True);shutil.copytree(qualification,out/'qualification');shutil.copyfile(PLAN,out/'upload.json');shutil.copyfile(args.runtime_receipt,out/'runtime-receipt.json')
    for n in ('settings','empty','missing','commands'):(out/n).mkdir()
    (out/'settings/vk_layer_settings.txt').write_text(protocol.SETTINGS,encoding='utf-8',newline='\n')
    sources=[pathlib.Path(__file__),PLAN,ROOT/'tests/test_current_vulkan_upload.py',pathlib.Path(protocol.__file__),
             qualification/'receipt.json',args.runtime_receipt.resolve(),out/'settings/vk_layer_settings.txt',
             ROOT/'tools/vulkan-validation.lock.json',*[ROOT/a['path'] for a in runtime['artifacts']],
             pathlib.Path(prepared['binaries']['upload']['path']),*protocol.inputs.source_paths()]
    frozen=[record(p) for p in sorted(set(sources))]
    r=dict(task='P3-BEN-03',status='running',source_commit=protocol.inputs.git(ROOT,'rev-parse','HEAD'),qualification_evidence=qualification.as_posix(),
           qualification_receipt_sha256=sha(qualification/'receipt.json'),output_directory=out.as_posix(),
           **{k:q[k] for k in ('identity','expected_gpu','SDK_source_fingerprint','profile_fingerprints','SDL_commit','validation_library','validation_library_sha256','runtime_receipt_sha256')},
           plan_sha256=sha(PLAN),frozen_inputs=frozen,validation_timings_excluded=True,historical_timings_reused=False,overall_SDK_ranking=False,
           SDL_native_handle_verified=False,completed_workload_tasks=['P3-BEN-03'],runs=[],groups=[],measurement_samples=0,verified_images=0,verified_bytes=0)
    def write():save(out/'receipt.json',r)
    clean,validated,environment=protocol.environments(out,dll,os.environ)
    save(out/'environment.json',environment);r['environment_sha256']=sha(out/'environment.json');write()
    try:
        for job in schedule(plan):
            require(protocol.inputs.unchanged(frozen), 'Frozen runtime/binary/source changed')
            argv=arguments(job,prepared,plan,r['expected_gpu']);validation=job['kind'] in ('validation','missing-layer')
            env=dict(validated if validation else clean)
            if job['kind']=='missing-layer':env['VK_LAYER_PATH']=str(out/'missing')
            start=time.monotonic();child=subprocess.Popen(argv,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:stdout,stderr=child.communicate(timeout=180)
            except subprocess.TimeoutExpired:
                child.kill();stdout,stderr=child.communicate();(out/'commands'/(job['name']+'.stdout')).write_bytes(stdout);(out/'commands'/(job['name']+'.stderr')).write_bytes(stderr);raise
            row=dict(job=job,argv=argv,pid=child.pid,started=start,finished=time.monotonic(),returncode=child.returncode,
                     overrides={k:v for k,v in env.items() if k.upper().startswith(('VK_','GFX_'))})
            for key,data in (('stdout',stdout),('stderr',stderr)):
                p=out/'commands'/(job['name']+'.'+key);p.write_bytes(data);row[key]=p.relative_to(out).as_posix();row[key+'_sha256']=sha(p)
            r['runs'].append(row);write()
            if job['kind'] in ('validation','measurement'):
                require(child.returncode==0,job['name']+' failed; raw logs retained')
                row['result']=protocol.validate_output(job,stdout,stderr,r,plan)
                if job['kind']=='measurement':r['measurement_samples']+=len(row['result']['samples_ms'])
                r['verified_images']+=row['result']['verified_images'];r['verified_bytes']+=row['result']['verified_bytes']
            else:protocol.validate_control(job,child.returncode,stdout,stderr)
            write();print(job['name'],'PASS',flush=True)
        protocol.check(qualification,live=True);require(protocol.inputs.unchanged(frozen), 'Final frozen input changed')
        r['groups']=summarize([x for x in r['runs'] if 'result' in x],plan);r['inputs_unchanged']=True;r['status']='completed';write()
        print(json.dumps(check(out,live=True)),flush=True)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:
        r.update(status='failed',error=str(e));write();print(str(e),flush=True);return 1
    return 0


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',choices=('run','check'));p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--qualification',type=pathlib.Path);p.add_argument('--runtime-receipt',type=pathlib.Path);p.add_argument('--live',action='store_true');a=p.parse_args()
    if a.stage=='check':print(json.dumps(check(a.output,a.live),indent=2));return 0
    require(a.qualification is not None and a.runtime_receipt is not None,'Explicit BEN-02 qualification and runtime receipt required')
    return run(a)


if __name__=='__main__':raise SystemExit(main())
