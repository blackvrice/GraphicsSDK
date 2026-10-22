"""Fixed-round full-reference diagnostics, separate from timed memory acceptance."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

import memory_environment as environment
from audit_licenses import ROOT,sdk_fingerprint
from analyze_memory_stress import read_records,require,number,OWNED,POLICY,validate_identity,validate_budget_samples
from run_memory_stress import adapter_argument

BINARY=ROOT/'out/memory-fixed-work/Release/gfx_memory_fixed_work.exe'


def validate(rows,fingerprint,code,policy=None):
    policy=json.loads(POLICY.read_text()) if policy is None else policy
    require(len(rows)>=4 and rows[0].get('type')=='header' and rows[-1].get('type')=='footer','Incomplete fixed-work evidence')
    h,f=rows[0],rows[-1]
    require(h.get('schema')=='fixed_work_v1' and type(h.get('reference_schema')) is int and h['reference_schema']==3
            and h.get('diagnostic_only') is True and h.get('memory_acceptance') is False and f.get('memory_acceptance') is False,'Wrong fixed-work schema/scope')
    require(isinstance(fingerprint,str) and re.fullmatch('[a-f0-9]{64}',fingerprint) and h.get('sdk_sha256')==fingerprint and h.get('mode') in ('gpu','validation','warp'),'Stale/wrong fixed workload')
    for name,low,high in (('warmup_rounds',0,10000),('measured_round_limit',1,100000),('checkpoint_rounds',1,h.get('measured_round_limit',0)),('max_seconds',1,3600)):
        require(type(h.get(name)) is int and low<=h[name]<=high,'Invalid '+name)
    require(all(type(h.get(k)) is int and h[k]==v for k,v in (('ring',8),('side',64),('copy_bytes',262144))),'Wrong shared workload dimensions')
    # A read-only projection reuses ONLY schema-3 identity/budget field contracts.
    # Raw fixed-work rows are never rewritten or passed to timed acceptance.
    projection=[dict(h,schema=3),*rows[1:]]
    identity=validate_identity(projection[0],f,h.get('requested_adapter_id'))
    require(type(h.get('requested_adapter_id')) is int and h['private_growth_limit_bytes']%2**20==0,'Missing exact adapter/guard selection')
    budget=validate_budget_samples(projection,policy)
    require(type(code) is int and code in (0,2,3),'Native fixed-work failure')
    reason={0:'completed',2:'private_growth_limit',3:'wall_time_limit'}[code]
    require(f.get('status')==('passed' if code==0 else 'aborted') and f.get('stop_reason')==reason,'Exit/footer mismatch')
    require(f.get('statistics_contracts') is True and type(f.get('validation_errors')) is int and f['validation_errors']==0,'Native reference/statistics failure')
    samples=rows[1:-1];previous=None
    for s in samples:
        require(s.get('type')=='sample' and number(s.get('elapsed')) and s['elapsed']>=0,'Invalid checkpoint time')
        require(all(type(s.get(k)) is int and s[k]>=0 for k in ('frames','rounds','private_bytes',*OWNED)) and s['frames']==s['rounds']*8,'Invalid checkpoint counters')
        if s.get('phase')=='gpu_held':
            require(s['objects']==s['resources']==1 and s['allocation_bytes']>=64*2**20 and s['pending']==s['pending_refs']==0,'GPU ownership control failed')
        else:require(all(s[k]==0 for k in OWNED),'Fixed-work resources did not drain')
        if previous:require(s['elapsed']>=previous['elapsed'] and s['rounds']>=previous['rounds'],'Reversed checkpoint')
        previous=s
    last=samples[-1];rounds=last['rounds'];warm=h['warmup_rounds'];limit=h['measured_round_limit'];measured=max(0,rounds-warm)
    require(rounds<=warm+limit and (code!=0 or rounds==warm+limit),'Incomplete/oversized fixed work')
    require(all(type(f.get(k)) is int for k in ('frames','full_images','verified_buffer_bytes')) and f['frames']==last['frames'] and f['full_images']==f['frames']*3 and f['verified_buffer_bytes']==f['frames']*262144,'Missing full reference coverage')
    require(number(f.get('elapsed')) and f['elapsed']>=last['elapsed'],'Invalid footer time')
    require(type(f.get('warmup_rounds_verified')) is int and f['warmup_rounds_verified']==min(rounds,warm)
            and type(f.get('measured_rounds_verified')) is int and f['measured_rounds_verified']==measured,'Warmup/measured count mismatch')
    starts=[s for s in samples if s.get('phase')=='measurement_start']
    require(len(starts)<=1 and (len(starts)==1 if rounds>warm else not starts if rounds<warm else True),'Missing/unexpected measurement start')
    if starts:require(starts[0]['rounds']==warm,'Wrong warmup work')
    drained=[s for s in samples if s.get('phase')=='drained']
    expected=list(range(h['checkpoint_rounds'],measured+1,h['checkpoint_rounds']))
    if measured==limit and (not expected or expected[-1]!=limit):expected.append(limit)
    require([s['rounds']-warm for s in drained]==expected,'Missing/duplicate/misaligned work checkpoints')
    abort='guard_aborted' if code==2 else 'time_aborted'
    phases=['baseline','cpu_held','cpu_released','gpu_held','gpu_released']+(['measurement_start'] if starts else [])+['drained']*len(drained)+([abort] if code else [])+['final']
    require([s.get('phase') for s in samples]==phases and samples[0]['rounds']==0,'Wrong fixed-work phase order')
    require(all(s['rounds']==0 for s in samples[:5]),'Work before positive controls finished')
    minimum=policy['cpu_control_minimum_delta_bytes']
    require(min(samples[1]['private_bytes']-samples[0]['private_bytes'],samples[1]['private_bytes']-samples[2]['private_bytes'])>=minimum,'CPU observation control failed')
    if code:
        stop=samples[-2];require(stop['frames']==last['frames'],'Work continued after abort')
        require(stop['private_bytes']>h['guard_baseline_private_bytes']+h['private_growth_limit_bytes'] if code==2 else stop['elapsed']>=h['max_seconds'],'Abort threshold not observed')
    return dict(outcome=reason,completed=code==0,adapter_id=identity,mode=h['mode'],frames=f['frames'],full_images=f['full_images'],verified_buffer_bytes=f['verified_buffer_bytes'],
                warmup_rounds_verified=min(rounds,warm),measured_rounds_verified=measured,validation_errors=0,diagnostic_only=True,memory_acceptance=False,
                reference_field_projection_schema=3,public_memory_budget=budget,
                checkpoints=[dict(measured_round=s['rounds']-warm,frames=s['frames'],private_bytes=s['private_bytes'],elapsed=s['elapsed']) for s in drained])


def input_paths():
    return [BINARY,BINARY.with_suffix('.pdb'),ROOT/'tools/run_fixed_memory.py',ROOT/'tools/memory_fixed_work/FixedWork.cpp',ROOT/'tools/memory_fixed_work/CMakeLists.txt',
           ROOT/'tools/MemoryStress.cpp',ROOT/'out/core/Release/GraphicsSDKCore.lib',POLICY]


def inputs():
    return {str(p.resolve()):environment.sha(p.read_bytes()) for p in input_paths()}


def run(args):
    require(not args.output.exists(),'Output already exists')
    before=environment.collect_snapshot(args.adapter);frozen=inputs();fp=sdk_fingerprint(ROOT)
    require(fp.encode() in BINARY.read_bytes(),'Stale fixed-work binary')
    args.output.mkdir(parents=True,exist_ok=False)
    environment.save(args.output/'environment-before.json',before)
    (args.output/'policy.json').write_bytes(POLICY.read_bytes())
    require(environment.sha((args.output/'policy.json').read_bytes())==before['controls']['files']['policy']['sha256'],'Policy changed before fixed work')
    command=[str(BINARY),'--mode',args.mode,'--adapter',str(args.adapter),'--rounds',str(args.rounds),'--warmup-rounds',str(args.warmup_rounds),
             '--checkpoint-rounds',str(args.checkpoint_rounds),'--max-seconds',str(args.max_seconds),'--max-private-growth-mib',str(args.max_private_growth_mib),'--output',str(args.output/'memory.jsonl')]
    result=dict(schema='fixed_case_v1',status='running',memory_acceptance=False,frozen_inputs=frozen,command=command)
    environment.save(args.output/'case.json',result)
    try:
        with (args.output/'native.log').open('xb') as log:
            process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT,cwd=ROOT)
            result['pid']=process.pid
            try:code=process.wait(timeout=args.max_seconds+120)
            except subprocess.TimeoutExpired:
                process.kill();process.wait();raise ValueError('Own fixed-work child exceeded watchdog')
        result['native_exit_code']=code
        result['references']=validate(read_records(args.output/'memory.jsonl'),fp,code,environment.load(args.output/'policy.json'))
        after=environment.collect_snapshot(args.adapter);environment.save(args.output/'environment-after.json',after)
        require(environment.compare_snapshots(before,after,within_run=True)['recorded_controls_match'],'Recorded environment changed')
        require(inputs()==frozen and sdk_fingerprint(ROOT)==fp,'Frozen fixed-work input changed')
        result['status']='completed' if code==0 else 'aborted'
    except (OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError) as error:result.update(status='rejected',error=str(error))
    finally:
        result['artifacts']=[dict(path=p.name,sha256=environment.sha(p.read_bytes())) for p in sorted(args.output.iterdir()) if p.is_file() and p.name!='case.json']
        environment.save(args.output/'case.json',result)
    if result['status']=='completed':
        try:load_case(args.output/'case.json')
        except (OSError,ValueError,KeyError,TypeError) as error:
            result.update(status='rejected',error=str(error));environment.save(args.output/'case.json',result)
    print('Fixed-work case '+result['status']+'; memory_acceptance=false')
    return 0 if result['status']=='completed' else 1


def load_case(path):
    case=environment.load(path);base=path.parent.resolve()
    require(isinstance(case,dict) and case.get('schema')=='fixed_case_v1' and case.get('status')=='completed' and case.get('memory_acceptance') is False,'Need a completed fixed-work case')
    frozen=case.get('frozen_inputs')
    require(isinstance(frozen,dict) and set(frozen)=={str(p.resolve()) for p in input_paths()}
            and all(isinstance(v,str) and re.fullmatch('[a-f0-9]{64}',v) for v in frozen.values()),'Missing fixed input identities')
    names=set()
    require(isinstance(case.get('artifacts'),list),'Missing case artifacts')
    for item in case.get('artifacts',[]):
        require(isinstance(item,dict),'Invalid artifact record')
        name=item.get('path');require(isinstance(name,str) and pathlib.PurePath(name).name==name and name not in names,'Invalid case artifact')
        names.add(name);p=(base/name).resolve();require(p.is_relative_to(base) and environment.sha(p.read_bytes())==item.get('sha256'),'Changed case artifact')
    require({'environment-before.json','environment-after.json','memory.jsonl','policy.json'}<=names,'Missing fixed-work case artifacts')
    a=environment.load(base/'environment-before.json');b=environment.load(base/'environment-after.json')
    require(environment.compare_snapshots(a,b,within_run=True)['recorded_controls_match'],'Unstable recorded environment')
    require(environment.sha((base/'policy.json').read_bytes())==a['controls']['files']['policy']['sha256']==frozen[str(POLICY.resolve())],'Unbound recorded policy')
    rows=read_records(base/'memory.jsonl');reference=validate(rows,a['controls']['SDK_source_fingerprint'],case.get('native_exit_code'),environment.load(base/'policy.json'))
    require(reference==case.get('references') and reference['completed'] and reference['adapter_id']==a['observations']['adapter_id'],'Wrong fixed-work case references')
    h=rows[0]
    command=[str(BINARY),'--mode',h['mode'],'--adapter',str(h['requested_adapter_id']),'--rounds',str(h['measured_round_limit']),
        '--warmup-rounds',str(h['warmup_rounds']),'--checkpoint-rounds',str(h['checkpoint_rounds']),'--max-seconds',str(h['max_seconds']),
        '--max-private-growth-mib',str(h['private_growth_limit_bytes']//2**20),'--output',str(base/'memory.jsonl')]
    require(case.get('command')==command and frozen[str((ROOT/'out/core/Release/GraphicsSDKCore.lib').resolve())]==a['controls']['files']['core_library']['sha256'],'Fixed command/core identity mismatch')
    return case,a,rows


def compare(left,right,change):
    frozen={p:environment.sha(p.read_bytes()) for p in (left,right)}
    a,ea,ra=load_case(left);b,eb,rb=load_case(right)
    result=environment.compare_snapshots(ea,eb,change)
    changed=environment.differences(a['frozen_inputs'],b['frozen_inputs'],'fixed_inputs')
    keys=('mode','warmup_rounds','measured_round_limit','checkpoint_rounds','max_seconds','private_growth_limit_bytes','ring','side','copy_bytes')
    changed+=environment.differences({k:ra[0][k] for k in keys},{k:rb[0][k] for k in keys},'fixed_work')
    result['differences']+=changed;result['recorded_controls_match']=not result['differences']
    result['matched_checkpoints']=[]
    if result['recorded_controls_match']:
        for x,y in zip(a['references']['checkpoints'],b['references']['checkpoints']):
            require(x['measured_round']==y['measured_round'],'Misaligned comparison checkpoint')
            result['matched_checkpoints'].append(dict(measured_round=x['measured_round'],measured_frames=x['measured_round']*8,
                before_private_bytes=x['private_bytes'],after_private_bytes=y['private_bytes'],difference_bytes=y['private_bytes']-x['private_bytes']))
    for path,case in ((left,a),(right,b)):
        require(environment.sha(path.read_bytes())==frozen[path] and all(environment.sha((path.parent/i['path']).read_bytes())==i['sha256'] for i in case['artifacts']),'Input changed during comparison')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='action',required=True)
    p=sub.add_parser('run');p.add_argument('--adapter',type=adapter_argument,required=True);p.add_argument('--mode',choices=('gpu','validation'),default='validation')
    for option,default in (('rounds',512),('warmup-rounds',8),('checkpoint-rounds',8),('max-seconds',120),('max-private-growth-mib',512)):p.add_argument('--'+option,type=int,default=default)
    p.add_argument('--output',type=pathlib.Path,required=True)
    p=sub.add_parser('compare');p.add_argument('--before',type=pathlib.Path,required=True);p.add_argument('--after',type=pathlib.Path,required=True)
    p.add_argument('--change',choices=('none','selected-driver'),required=True);p.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args();args.output=args.output.resolve()
    try:
        require(not args.output.exists(),'Output already exists')
        if args.action=='run':
            require(1<=args.rounds<=100000 and 0<=args.warmup_rounds<=10000 and 1<=args.checkpoint_rounds<=args.rounds
                    and 1<=args.max_seconds<=3600 and 1<=args.max_private_growth_mib<=4096,'Invalid fixed-work arguments')
            return run(args)
        result=compare(args.before.resolve(),args.after.resolve(),args.change)
        with args.output.open('x',encoding='utf-8') as file:json.dump(result,file,indent=2);file.write('\n')
        return 0 if result['recorded_controls_match'] else 1
    except (OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError) as error:print('FAIL: '+str(error),file=sys.stderr);return 1


if __name__=='__main__':raise SystemExit(main())
