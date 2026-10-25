"""Synthetic fixed-work contracts, distinct from actual native observations."""
import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import run_fixed_memory as fixed
from test_memory_campaign import current,with_budgets,FP
from test_memory_environment import snapshot
from analyze_memory_stress import analyze


def evidence(code=0,warm=2,limit=5,checkpoint=2,rounds=None):
    source=with_budgets(current('validation'));h=source[0]
    h.update(schema='fixed_work_v1',reference_schema=3,diagnostic_only=True,memory_acceptance=False,
             warmup_rounds=warm,measured_round_limit=limit,checkpoint_rounds=checkpoint,max_seconds=120)
    for name in ('seconds','warmup','interval'):h.pop(name)
    rounds=warm+limit if rounds is None else rounds
    def sample(phase,n):
        r=copy.deepcopy(source[1]);r.update(phase=phase,elapsed=n+.1,rounds=n,frames=n*8);return r
    samples=copy.deepcopy(source[1:6])
    for i,r in enumerate(samples):r.update(elapsed=i*.01,rounds=0,frames=0)
    if rounds>=warm:samples.append(sample('measurement_start',warm))
    measured=max(0,rounds-warm);points=list(range(checkpoint,measured+1,checkpoint))
    if measured==limit and (not points or points[-1]!=limit):points.append(limit)
    samples += [sample('drained',warm+n) for n in points]
    if code:
        stop=sample('guard_aborted' if code==2 else 'time_aborted',rounds)
        if code==2:stop['private_bytes']=h['guard_baseline_private_bytes']+h['private_growth_limit_bytes']+4096
        else:stop['elapsed']=120.1
        samples.append(stop)
    final=sample('final',rounds);final['elapsed']=samples[-1]['elapsed']+.1;samples.append(final)
    f=dict(type='footer',adapter_id=1,elapsed=final['elapsed']+.1,frames=rounds*8,full_images=rounds*24,verified_buffer_bytes=rounds*8*262144,
           warmup_rounds_verified=min(rounds,warm),measured_rounds_verified=measured,validation_errors=0,statistics_contracts=True,
           status='passed' if code==0 else 'aborted',stop_reason={0:'completed',2:'private_growth_limit',3:'wall_time_limit'}[code],memory_acceptance=False)
    return [h,*samples,f]


def case(folder,rows=None):
    folder.mkdir();rows=evidence() if rows is None else rows;s=snapshot();h=rows[0]
    (folder/'policy.json').write_bytes(fixed.POLICY.read_bytes());policy_hash=fixed.environment.sha((folder/'policy.json').read_bytes())
    s['controls']['files']['policy']['sha256']=policy_hash
    fixed.environment.save(folder/'environment-before.json',s);fixed.environment.save(folder/'environment-after.json',s)
    (folder/'memory.jsonl').write_text('\n'.join(json.dumps(r) for r in rows)+'\n')
    command=[str(fixed.BINARY),'--mode',h['mode'],'--adapter','1','--rounds',str(h['measured_round_limit']),'--warmup-rounds',str(h['warmup_rounds']),
        '--checkpoint-rounds',str(h['checkpoint_rounds']),'--max-seconds',str(h['max_seconds']),'--max-private-growth-mib',str(h['private_growth_limit_bytes']//2**20),
        '--output',str(folder/'memory.jsonl')]
    data=dict(schema='fixed_case_v1',status='completed',native_exit_code=0,memory_acceptance=False,command=command,
        frozen_inputs={str(p.resolve()):'a'*64 for p in fixed.input_paths()},references=fixed.validate(rows,FP,0),
        artifacts=[dict(path=p.name,sha256=fixed.environment.sha(p.read_bytes())) for p in sorted(folder.iterdir())])
    data['frozen_inputs'][str(fixed.POLICY.resolve())]=policy_hash
    fixed.environment.save(folder/'case.json',data);return folder/'case.json'


class FixedMemoryContracts(unittest.TestCase):
    def test_exact_warmup_measurement_and_remainder_checkpoint(self):
        rows=evidence();result=fixed.validate(rows,FP,0)
        self.assertEqual(result['frames'],56);self.assertEqual(result['measured_rounds_verified'],5)
        self.assertEqual([p['measured_round'] for p in result['checkpoints']],[2,4,5]);self.assertFalse(result['memory_acceptance'])
        self.assertTrue(result['public_memory_budget']['recorded']);self.assertEqual(rows[0]['schema'],'fixed_work_v1')

    def test_fixed_work_cannot_enter_timed_acceptance(self):
        with self.assertRaises((ValueError,KeyError)):analyze(evidence(),FP)

    def test_zero_warmup_and_one_round_are_valid(self):
        result=fixed.validate(evidence(warm=0,limit=1,checkpoint=1),FP,0)
        self.assertEqual(result['frames'],8);self.assertEqual(result['warmup_rounds_verified'],0)

    def test_guard_and_wall_deadline_preserve_verified_prefix(self):
        for code,rounds in ((2,1),(2,4),(3,0),(3,3)):
            rows=evidence(code=code,rounds=rounds)
            result=fixed.validate(rows,FP,code)
            self.assertFalse(result['completed']);self.assertEqual(result['frames'],rounds*8)
            self.assertFalse(result['memory_acceptance'])

    def test_incomplete_oversized_and_wrong_split_counts_reject(self):
        for mutate in ('frames','measured','warmup','target','exit','scope','schema','stale'):
            rows=evidence()
            if mutate=='frames':rows[-1]['full_images']-=1
            if mutate=='measured':rows[-1]['measured_rounds_verified']=4
            if mutate=='warmup':rows[-1]['warmup_rounds_verified']=1
            if mutate=='target':rows[0]['measured_round_limit']=6
            if mutate=='exit':rows[-1]['stop_reason']='private_growth_limit'
            if mutate=='scope':rows[0]['memory_acceptance']=True
            if mutate=='schema':rows[0]['schema']=3
            if mutate=='stale':rows[0]['sdk_sha256']='b'*64
            with self.subTest(mutate=mutate),self.assertRaises(ValueError):fixed.validate(rows,FP,0)

    def test_missing_duplicate_and_out_of_order_work_checkpoints_reject(self):
        for mutate in ('missing','duplicate','start','order','unknown_phase'):
            rows=evidence()
            if mutate=='missing':rows.pop(7)
            if mutate=='duplicate':rows.insert(7,copy.deepcopy(rows[7]))
            if mutate=='start':rows[6]['rounds']=1;rows[6]['frames']=8
            if mutate=='order':rows[7],rows[8]=rows[8],rows[7]
            if mutate=='unknown_phase':rows[7]['phase']='live'
            with self.subTest(mutate=mutate),self.assertRaises(ValueError):fixed.validate(rows,FP,0)

    def test_identity_public_budget_and_drain_contracts_reject(self):
        for mutate in ('adapter','budget','ownership','errors','requested'):
            rows=evidence()
            if mutate=='adapter':rows[-1]['adapter_id']=2
            if mutate=='budget':rows[7]['sdk_budget_local']['usage_bytes']+=4096
            if mutate=='ownership':rows[7]['resources']=1
            if mutate=='errors':rows[-1]['validation_errors']=1
            if mutate=='requested':rows[0]['requested_adapter_id']=None
            with self.subTest(mutate=mutate),self.assertRaises(ValueError):fixed.validate(rows,FP,0)

    def test_bad_types_ranges_and_timing_reject(self):
        for field,value in (('warmup_rounds',True),('measured_round_limit',0),('checkpoint_rounds',0),('max_seconds',3601),('ring',True)):
            rows=evidence();rows[0][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):fixed.validate(rows,FP,0)
        for field,value in (('elapsed',False),('frames',True),('private_bytes',-1)):
            rows=evidence();rows[3][field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):fixed.validate(rows,FP,0)

    def test_abort_requires_observed_threshold_and_no_later_work(self):
        for code in (2,3):
            rows=evidence(code=code,rounds=3)
            rows[-3]['private_bytes']=rows[0]['guard_baseline_private_bytes'];rows[-3]['elapsed']=3.1
            with self.subTest(code=code),self.assertRaises(ValueError):fixed.validate(rows,FP,code)

    def test_matched_comparison_uses_work_coordinates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);a=case(root/'a');rows=evidence()
            for row in rows[1:-1]:row['elapsed']*=2;row['private_bytes']+=4096
            rows[-1]['elapsed']*=2;b=case(root/'b',rows)
            result=fixed.compare(a,b,'none');self.assertTrue(result['recorded_controls_match'])
            self.assertEqual([p['measured_frames'] for p in result['matched_checkpoints']],[16,32,40])
            self.assertEqual([p['difference_bytes'] for p in result['matched_checkpoints']],[4096]*3)

    def test_different_work_or_binary_does_not_produce_memory_comparison(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);a=case(root/'a');b=case(root/'b',evidence(limit=6))
            result=fixed.compare(a,b,'none');self.assertFalse(result['recorded_controls_match']);self.assertFalse(result['matched_checkpoints'])
            data=fixed.environment.load(b);data['frozen_inputs'][str(fixed.BINARY.resolve())]='b'*64;fixed.environment.save(b,data)
            self.assertFalse(fixed.compare(a,b,'none')['recorded_controls_match'])

    def test_case_artifact_reference_command_and_frozen_identity_cannot_change(self):
        for mutate in ('artifact','reference','command','frozen','scope','policy'):
            with tempfile.TemporaryDirectory() as tmp:
                path=case(pathlib.Path(tmp)/'a');data=fixed.environment.load(path)
                if mutate=='artifact':(path.parent/'memory.jsonl').write_text('bad\n')
                if mutate=='reference':data['references']['frames']+=8
                if mutate=='command':data['command'][2]='gpu'
                if mutate=='frozen':data['frozen_inputs']={}
                if mutate=='scope':data['status']='aborted'
                if mutate=='policy':
                    policy=path.parent/'policy.json';policy.write_text('{}')
                    for item in data['artifacts']:
                        if item['path']=='policy.json':item['sha256']=fixed.environment.sha(policy.read_bytes())
                fixed.environment.save(path,data)
                with self.subTest(mutate=mutate),self.assertRaises(ValueError):fixed.load_case(path)

    def test_equivalent_output_path_preserves_original_command(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=case(pathlib.Path(tmp)/'a');data=fixed.environment.load(path)
            (path.parent/'alias').mkdir()
            data['command'][-1]=str(path.parent/'alias'/'..'/'memory.jsonl')
            fixed.environment.save(path,data)
            loaded,_,_=fixed.load_case(path)
            self.assertEqual(loaded['command'],data['command'])

    def test_command_paths_and_argument_order_remain_bound(self):
        for mutate in ('binary','output','order','type','extra'):
            with tempfile.TemporaryDirectory() as tmp:
                path=case(pathlib.Path(tmp)/'a');data=fixed.environment.load(path)
                if mutate=='binary':data['command'][0]+='.other'
                if mutate=='output':data['command'][-1]=str(path.parent/'other.jsonl')
                if mutate=='order':data['command'][1],data['command'][3]=data['command'][3],data['command'][1]
                if mutate=='type':data['command'][2]=False
                if mutate=='extra':data['command'].append('--extra')
                fixed.environment.save(path,data)
                with self.subTest(mutate=mutate),self.assertRaises(ValueError):fixed.load_case(path)


if __name__=='__main__':unittest.main()
