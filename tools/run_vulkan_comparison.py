"""Qualify current Vulkan comparison correctness, process separation and statistics.

BEN-02 measures representative cases to exercise the protocol. Full workload
performance acceptance belongs to BEN-03..06. No ranking or historical replay.
"""
import argparse
import json
import math
import os
import pathlib
import platform
import random
import re
import shutil
import statistics
import subprocess
import time

import prepare_vulkan_comparison as inputs
from prepare_vulkan_comparison import ROOT, NAMES, MODULES, read, save, sha, record, require

PLAN = ROOT / 'benchmarks/phase3_comparison/protocol.json'
SDKS = ('graphicssdk', 'sdl')
SETTINGS = ('khronos_validation.validate_sync = true\n'
            'khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\n'
            'khronos_validation.syncval_submit_time_validation = true\n'
            'khronos_validation.duplicate_message_limit = 0\n'
            'khronos_validation.message_id_filter =\n')


def validate_plan(p):
    require(p['schema_version'] == 1 and type(p['schema_version']) is int and p['task'] == 'P3-BEN-02'
            and p['scope'] == 'windows-nvidia-vulkan-protocol-qualification', 'Wrong protocol scope')
    require(p['validation'] == dict(all_contract_scenarios=True, warmup=1, samples=3, required_core_and_sync=True), 'Validation gate drift')
    require(type(p['validation']['warmup']) is type(p['validation']['samples']) is int, 'Validation sample counts must be integers')
    expected = dict(scenarios=dict(upload='odd', draw='draw1', pso='pso1', allocation='buffer1'), warmup=3,
                    samples=12, repetitions=3, seed=20261027,
                    order='seeded_profile_blocks_with_alternating_adjacent_SDK_pairs_each_repetition')
    require(p['performance'] == expected and all(type(p['performance'][k]) is int for k in ('warmup','samples','repetitions','seed')), 'Qualification workload/order drift')
    require(p['statistics'] == dict(quantile='linear_interpolation_at_(N-1)*q',
            fields=['p50_ms','p95_ms','p99_ms','min_ms','max_ms','MAD_ms','process_medians_ms'], retain_all_raw_samples=True,
            validation_timings_excluded=True, warmup_excluded=True, samples_removed=0,
            interpretation='descriptive_protocol_qualification_not_workload_acceptance_or_confidence_bounds'), 'Statistics policy drift')
    require(p['controls'] == ['missing_required_layer_for_each_profile_and_SDK','absent_exact_GPU_for_each_profile_and_SDK']
            and p['serialized_processes'] is True and p['historical_timings_reused'] is p['overall_SDK_ranking'] is p['SDL_native_handle_verified'] is False
            and p['full_workload_tasks_completed'] == [] and p['next_gate'] == 'P3-BEN-03', 'Protocol promoted to workload/ranking acceptance')
    return p


def schedule(plan):
    """Every validation and fallback control finishes before any timed process."""
    jobs = []
    for n, m in MODULES.items():
        for scenario in m.SHAPES:
            for sdk in SDKS:
                jobs.append(dict(profile=n, sdk=sdk, scenario=scenario, kind='validation', repetition=0))
    for n in NAMES:
        for sdk in SDKS:
            for kind in ('missing-layer','wrong-target'):
                jobs.append(dict(profile=n, sdk=sdk, scenario=plan['performance']['scenarios'][n], kind=kind, repetition=0))
    rng = random.Random(plan['performance']['seed'])
    first = {n:rng.randrange(2) for n in NAMES}
    for rep in range(plan['performance']['repetitions']):
        profiles = list(NAMES); rng.shuffle(profiles)
        for n in profiles:
            order = SDKS if (first[n]+rep)%2 == 0 else SDKS[::-1]
            for sdk in order:
                jobs.append(dict(profile=n, sdk=sdk, scenario=plan['performance']['scenarios'][n], kind='measurement', repetition=rep))
    for i,j in enumerate(jobs):
        j['name'] = f"{i:03}-{j['kind']}-{j['profile']}-{j['sdk']}-{j['scenario']}-{j['repetition']}"
    return jobs


def normalized_path(s):
    return str(s).replace('\\','/').casefold()


def environments(out, dll, inherited):
    base={k:v for k,v in inherited.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_'))}
    clean={**base,'VK_LOADER_LAYERS_DISABLE':'~implicit~','VK_LAYER_PATH':str(out/'empty'),'VK_LAYER_SETTINGS_PATH':str(out/'empty')}
    validated={**clean,'VK_LAYER_PATH':str(dll.parent),'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','VK_LAYER_SETTINGS_PATH':str(out/'settings'),
               'GFX_EXPECT_VULKAN_LAYER':str(dll),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true','VK_LOADER_DEBUG':'layer'}
    evidence=dict(removed_inherited_keys=sorted(set(inherited)-set(base)),performance_overrides={k:v for k,v in clean.items() if k not in base},
                  validation_overrides={k:v for k,v in validated.items() if k not in base})
    return clean,validated,evidence


def same_overrides(actual, expected):
    paths={'VK_LAYER_PATH','VK_LAYER_SETTINGS_PATH','GFX_EXPECT_VULKAN_LAYER'}
    return actual.keys()==expected.keys() and all(normalized_path(actual[k])==normalized_path(v) if k in paths else actual[k]==v for k,v in expected.items())


def validate_output(job, stdout, stderr, context, plan):
    require(not re.search(rb'Validation Error|SYNC-HAZARD|VUID-[^\s]+.*(?:ERROR|Error)', stdout+b'\n'+stderr), 'Native validation diagnostic')
    data = json.loads(stdout)
    validation = job['kind'] == 'validation'
    mode = plan['validation' if validation else 'performance']
    MODULES[job['profile']].validate_result(data, job['sdk'], job['scenario'], 'high', mode['warmup'], mode['samples'], validation,
        context['identity'], context['profile_fingerprints'][job['profile']], context['SDK_source_fingerprint'], context['SDL_commit'])
    require(data['device_name'] == context['expected_gpu'] and data['native_vendor_id'] == 4318, 'Expected NVIDIA GPU changed')
    for k,v in data.items():
        if k.startswith(('verified_','created_','released_resource_','submitted_')):
            require(type(v) is int, 'Non-integer work counter: '+k)
    if validation:
        require(normalized_path(data['loaded_validation_layer']) == normalized_path(context['validation_library']), 'Unpinned validation layer loaded')
        require(b'VK_LAYER_KHRONOS_validation' in stderr and b'VkLayer_khronos_validation.dll' in stderr, 'Missing actual loader layer trace')
    else:
        require(data['loaded_validation_layer'] == '', 'Validation timing contamination')
    return data


def validate_control(job, code, stdout, stderr):
    require(type(code) is int and code == 1 and not stdout, 'Control produced results or did not fail')
    if job['kind'] == 'wrong-target':
        inputs.validate_target_rejection(job['profile'], job['sdk'], code, stdout, stderr)
    else:
        # Both consumers explicitly run the SDK required-validation prerequisite
        # before their own validation workload. This tests that prerequisite gate.
        require(b'VK_LAYER_KHRONOS_validation is not installed' in stderr, 'Unrelated failure is not missing-required-layer rejection')


def scalar_stats(runs):
    require(runs and all(isinstance(row,list) and row for row in runs), 'Empty process samples')
    values = [v for row in runs for v in row]
    require(all(type(v) in (int,float) and math.isfinite(v) and v >= 0 for v in values), 'Non-finite or negative sample')
    ordered = sorted(values)
    def q(p):
        x=(len(ordered)-1)*p;lo=int(x);hi=min(lo+1,len(ordered)-1)
        return ordered[lo]+(ordered[hi]-ordered[lo])*(x-lo)
    median=q(.5)
    return dict(processes=len(runs), samples=len(values), p50_ms=median, p95_ms=q(.95), p99_ms=q(.99),
                min_ms=min(values), max_ms=max(values), MAD_ms=statistics.median(abs(v-median) for v in values),
                process_medians_ms=[statistics.median(row) for row in runs], samples_removed=0)


def summarize(rows, plan):
    groups=[]
    for n in NAMES:
        for sdk in SDKS:
            runs=[row['result'] for row in rows if row['job']['kind']=='measurement' and row['job']['profile']==n and row['job']['sdk']==sdk]
            require(len(runs)==plan['performance']['repetitions'], 'Missing independent process repetition')
            columns=inputs.read(inputs.PLAN)['profiles'][n]['columns']
            samples=[[[x] for x in r['samples_ms']] if n=='upload' else r['CPU_intervals_ms'] if n=='draw' else r['samples_ms'] for r in runs]
            groups.append(dict(profile=n, sdk=sdk, scenario=plan['performance']['scenarios'][n], category=runs[0]['category'],
                               statistics={c:scalar_stats([[row[i] for row in process] for process in samples]) for i,c in enumerate(columns)}))
    return groups


def runtime_inputs(path):
    r=read(path); lock=read(ROOT/'tools/vulkan-validation.lock.json')
    require(r['lock_sha256']==sha(ROOT/'tools/vulkan-validation.lock.json') and r['configuration']==lock['configuration'], 'Validation runtime lock/build changed')
    require({(c['name'],c['url'],c['commit']) for c in r['components']}=={(c['name'],c['url'],c['commit']) for c in lock['components']}, 'Runtime dependency pin changed')
    require(len(r['artifacts'])==2, 'Unexpected validation runtime artifacts')
    for a in r['artifacts']:
        p=ROOT/a['path'];require(p.resolve().is_relative_to(ROOT/'out') and p.is_file() and p.stat().st_size==a['bytes'] and sha(p)==a['sha256'], 'Pinned validation runtime changed')
    dll=next(ROOT/a['path'] for a in r['artifacts'] if a['path'].endswith('.dll'))
    manifest=next(ROOT/a['path'] for a in r['artifacts'] if a['path'].endswith('.json'))
    layer=read(manifest)['layer']
    require(layer['name']=='VK_LAYER_KHRONOS_validation' and (manifest.parent/layer['library_path']).resolve()==dll.resolve(), 'Runtime manifest maps to another library')
    return r,dll


def check(folder, live=False):
    r=read(folder/'receipt.json');plan=validate_plan(read(folder/'protocol.json'))
    require(r['task']=='P3-BEN-02' and r['status']=='qualified' and r['inputs_unchanged'] is True
            and r['validation_timings_excluded'] is True and r['historical_timings_reused'] is r['overall_SDK_ranking'] is r['SDL_native_handle_verified'] is False
            and r['full_workload_tasks_completed']==[], 'Incomplete or promoted qualification')
    require(sha(folder/'protocol.json')==r['protocol_sha256'], 'Protocol bytes changed')
    inputs.check(folder/'prepared')
    prepared=read(folder/'prepared/receipt.json')
    require(all(r[k]==prepared[k] for k in ('identity','SDK_source_fingerprint','profile_fingerprints','SDL_commit','expected_gpu')), 'Preparation/result provenance changed')
    require(sha(folder/'runtime-receipt.json')==r['runtime_receipt_sha256'], 'Runtime receipt changed')
    runtime=read(folder/'runtime-receipt.json')
    require(runtime['lock_sha256']==sha(ROOT/'tools/vulkan-validation.lock.json'), 'Validation runtime lock attribution changed')
    require(any(normalized_path(r['validation_library']).endswith('/'+normalized_path(a['path'])) and a['sha256']==r['validation_library_sha256'] for a in runtime['artifacts']), 'Validation library attribution changed')
    require((folder/'settings/vk_layer_settings.txt').read_text()==SETTINGS, 'Required sync validation settings changed')
    require(sha(folder/'environment.json')==r['environment_sha256'], 'Process environments changed')
    environment=read(folder/'environment.json')
    clean=environment['performance_overrides'];validated=environment['validation_overrides']
    require(set(clean)=={'VK_LOADER_LAYERS_DISABLE','VK_LAYER_PATH','VK_LAYER_SETTINGS_PATH'} and clean['VK_LOADER_LAYERS_DISABLE']=='~implicit~'
            and normalized_path(clean['VK_LAYER_PATH']).endswith('/empty') and clean['VK_LAYER_SETTINGS_PATH']==clean['VK_LAYER_PATH'], 'Performance layer isolation changed')
    require(same_overrides(validated,{**clean,'VK_LAYER_PATH':str(pathlib.Path(r['validation_library']).parent),'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation',
            'VK_LAYER_SETTINGS_PATH':str(pathlib.Path(r['output_directory'])/'settings'),'GFX_EXPECT_VULKAN_LAYER':str(pathlib.Path(r['validation_library'])),
            'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true','VK_LOADER_DEBUG':'layer'}), 'Required validation process settings changed')
    jobs=schedule(plan);require(len(r['runs'])==len(jobs), 'Missing/extra campaign processes')
    measured=[];last_end=0
    for row,job in zip(r['runs'],jobs):
        require(row['job']==job, 'Run order/coverage/repetition changed')
        require(type(row['pid']) is int and row['pid']>0 and last_end<=row['started']<=row['finished'], 'Overlapping/non-process execution')
        last_end=row['finished']
        stdout=(folder/row['stdout']).read_bytes();stderr=(folder/row['stderr']).read_bytes()
        require(sha(folder/row['stdout'])==row['stdout_sha256'] and sha(folder/row['stderr'])==row['stderr_sha256'], 'Raw process output changed')
        expected_mode=job['kind'] in ('validation','missing-layer')
        expected_overrides=dict(validated if expected_mode else clean)
        if job['kind']=='missing-layer':expected_overrides['VK_LAYER_PATH']=str(pathlib.Path(r['output_directory'])/'missing')
        require(same_overrides(row['overrides'],expected_overrides), 'Per-process validation/performance isolation changed')
        mode=plan['validation' if expected_mode else 'performance']
        gpu='GraphicsSDK BEN02 absent GPU' if job['kind']=='wrong-target' else r['expected_gpu']
        require(row['argv']==[prepared['binaries'][job['profile']]['path'], job['sdk'], job['scenario'],'high',str(mode['warmup']),str(mode['samples']),str(int(expected_mode)),gpu], 'Recorded command changed')
        if job['kind'] in ('validation','measurement'):
            require(row['returncode']==0, 'Successful workload exit required')
            result=validate_output(job,stdout,stderr,r,plan)
            require(result==row['result'], 'Raw/parsed result changed');measured.append(row)
        else:validate_control(job,row['returncode'],stdout,stderr)
    require(r['groups']==summarize(measured,plan), 'Statistics do not reproduce from measurement-only raw samples')
    require(r['measurement_samples']==plan['performance']['samples']*plan['performance']['repetitions']*len(NAMES)*len(SDKS), 'Wrong measurement sample total')
    if live:
        inputs.check(pathlib.Path(r['prepared_evidence']),live=True)
        require(inputs.unchanged(r['frozen_inputs']), 'Runtime/binary/protocol input changed')
    return dict(task='P3-BEN-02',protocol_qualified=True,validation_processes=26,control_processes=16,
                measurement_processes=24,measurement_samples=r['measurement_samples'],full_workload_tasks_completed=[],overall_SDK_ranking=False)


def run(args):
    require(platform.system()=='Windows', 'Windows host required')
    prepared=args.prepared_roots.resolve()/'evidence';inputs.check(prepared,live=True)
    p=read(prepared/'receipt.json');plan=validate_plan(read(PLAN));runtime,dll=runtime_inputs(args.runtime_receipt.resolve())
    out=args.output.resolve();require(out.is_relative_to(ROOT/'out') and not out.exists(), 'Fresh output under out/ required')
    out.mkdir(parents=True);shutil.copytree(prepared,out/'prepared');shutil.copyfile(PLAN,out/'protocol.json');shutil.copyfile(args.runtime_receipt,out/'runtime-receipt.json')
    for name in ('settings','empty','missing'): (out/name).mkdir()
    (out/'settings/vk_layer_settings.txt').write_text(SETTINGS,encoding='utf-8',newline='\n')
    sources=[pathlib.Path(__file__),PLAN,ROOT/'tools/vulkan-validation.lock.json',args.runtime_receipt.resolve(),out/'settings/vk_layer_settings.txt',*[ROOT/a['path'] for a in runtime['artifacts']],*[pathlib.Path(a['path']) for a in p['binaries'].values()],*inputs.source_paths()]
    frozen=[record(x) for x in sorted(set(sources))]
    r=dict(task='P3-BEN-02',status='running',source_commit=inputs.git(ROOT,'rev-parse','HEAD'),prepared_evidence=prepared.as_posix(),output_directory=out.as_posix(),
           **{k:p[k] for k in ('identity','SDK_source_fingerprint','profile_fingerprints','SDL_commit','expected_gpu')},
           validation_library=dll.resolve().as_posix(),validation_library_sha256=sha(dll),runtime_receipt_sha256=sha(out/'runtime-receipt.json'),
           protocol_sha256=sha(PLAN),frozen_inputs=frozen,validation_timings_excluded=True,historical_timings_reused=False,overall_SDK_ranking=False,
           SDL_native_handle_verified=False,full_workload_tasks_completed=[],runs=[],groups=[],measurement_samples=0)
    def write():save(out/'receipt.json',r)
    clean,validated,environment=environments(out,dll,os.environ)
    save(out/'environment.json',environment);r['environment_sha256']=sha(out/'environment.json')
    write()
    try:
        (out/'commands').mkdir()
        for job in schedule(plan):
            require(inputs.unchanged(frozen), 'Frozen runtime/binary/source changed')
            validation=job['kind'] in ('validation','missing-layer');mode=plan['validation' if validation else 'performance']
            gpu='GraphicsSDK BEN02 absent GPU' if job['kind']=='wrong-target' else p['expected_gpu']
            argv=[p['binaries'][job['profile']]['path'],job['sdk'],job['scenario'],'high',str(mode['warmup']),str(mode['samples']),str(int(validation)),gpu]
            env=dict(validated if validation else clean)
            if job['kind']=='missing-layer':env['VK_LAYER_PATH']=str(out/'missing')
            start=time.monotonic()
            child=subprocess.Popen(argv,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:stdout,stderr=child.communicate(timeout=180)
            except subprocess.TimeoutExpired:
                child.kill();stdout,stderr=child.communicate();(out/'commands'/(job['name']+'.stdout')).write_bytes(stdout);(out/'commands'/(job['name']+'.stderr')).write_bytes(stderr);raise
            row=dict(job=job,argv=argv,pid=child.pid,started=start,finished=time.monotonic(),returncode=child.returncode,
                     overrides={k:v for k,v in env.items() if k.upper().startswith(('VK_','GFX_'))})
            for key,data in (('stdout',stdout),('stderr',stderr)):
                path=out/'commands'/(job['name']+'.'+key);path.write_bytes(data);row[key]=path.relative_to(out).as_posix();row[key+'_sha256']=sha(path)
            r['runs'].append(row);write()
            if job['kind'] in ('validation','measurement'):
                require(child.returncode==0, job['name']+' failed; raw logs retained')
                row['result']=validate_output(job,stdout,stderr,r,plan)
                if job['kind']=='measurement':r['measurement_samples']+=len(row['result']['samples_ms'])
            else:validate_control(job,child.returncode,stdout,stderr)
            write();print(job['name'],'PASS',flush=True)
        inputs.check(prepared,live=True);require(inputs.unchanged(frozen), 'Final frozen input changed')
        r['groups']=summarize([x for x in r['runs'] if 'result' in x],plan);r['inputs_unchanged']=True;r['status']='qualified';write()
        print(json.dumps(check(out,live=True)),flush=True)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:
        r.update(status='failed',error=str(e));write();print(str(e),flush=True);return 1
    return 0


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',choices=('run','check'));p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--prepared-roots',type=pathlib.Path);p.add_argument('--runtime-receipt',type=pathlib.Path);p.add_argument('--live',action='store_true');a=p.parse_args()
    if a.stage=='check':print(json.dumps(check(a.output,a.live),indent=2));return 0
    require(a.prepared_roots is not None and a.runtime_receipt is not None,'Explicit preparation roots and runtime receipt required')
    return run(a)


if __name__=='__main__':raise SystemExit(main())
