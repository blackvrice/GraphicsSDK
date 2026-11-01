"""Synthetic join/packaging contracts only; these fixtures are not native evidence."""
import copy
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_readiness_report as r


def fixture():
    contract=r.read(r.ROOT/'benchmarks/phase3_comparison/contract.json');commit='1'*40
    q=dict(source_commit=commit,mode='software-diagnostic',physical_probe_qualified=False,P3_BEN_07_completed=False,
        selected=dict(device_type=4,device_uuid='a'*32),environment=dict(cpu='fixture',compiler='GCC 14'),
        SDK_source_fingerprint='2'*64,SDL_commit='3'*40)
    probe=dict(source_commit=commit,qualification=q,LLGL_commit=r.read(r.ROOT/'benchmarks/competitors/sources.lock.json')['llgl']['commit'],
        binaries=dict(core=dict(path='/native/libLLGL.so',bytes=1,sha256='a'*64)))
    receipts={};results={};bindings={};time=0
    for name in r.ORDER:
        rows=[]
        cases=[(profile,scenario,sdk,v) for profile,scenarios in r.CASES.items() for scenario in scenarios for sdk in ('graphicssdk','sdl') for v in (False,True)] if name=='matrix' else [(name,scenario,'llgl',v) for scenario in r.CASES[name] for v in (False,True)]
        for profile,scenario,sdk,validation in cases:
            shape=contract['profiles'][profile]['shapes'][scenario];count=contract['profiles'][profile].get('work',{}).get(scenario,1) if profile=='allocation' else 1
            images=4*count if profile!='allocation' or scenario.startswith('texture') else 0
            buffers=4*count if profile=='allocation' and not images else 0
            size=shape[0]*shape[1]*shape[2] if images else 4096
            job=dict(scenario=scenario,validation=validation)
            if name=='matrix':job.update(profile=profile,sdk=sdk)
            else:job['kind']='normal'
            rows.append(dict(job=job,started=float(time),finished=time+.5,result=dict(contract=contract['profiles'][profile]['contract'],
                verified_images=images,verified_buffers=buffers,verified_bytes=(images+buffers)*size)))
            time+=1
        if name!='matrix':
            for kind in ('core-control','sync-control'):
                rows.append(dict(job=dict(kind=kind),started=float(time),finished=time+.5));time+=1
            for i in range(dict(upload=5,draw=9,pso=11,allocation=13)[name]):
                rows.append(dict(job=dict(kind='rejected-'+str(i)),started=float(time),finished=time+.5));time+=1
        common=dict(path='/native/common.so',bytes=1,sha256='a'*64)
        row=dict(source_commit=commit,status='completed',inputs_unchanged=True,performance_samples=0,performance_accepted=False,
            physical_GPU_accepted=False,runs=rows,frozen_inputs=[common],runtime_inputs=[],
            source_inputs=[dict(path='benchmarks/common/LinuxEnvironment.hpp',sha256='b'*64)])
        if name=='matrix':row.update(contract=contract,readiness=dict(qualification=copy.deepcopy(q),qualification_sha256='c'*64))
        else:row['probe']=copy.deepcopy(probe)
        receipts[name]=row;results[name]=dict(processes=len(rows),performance_samples=0,physical_GPU_accepted=False)
        if name=='matrix':results[name]['diagnostic_rows']=156
        bindings[name]=dict(path=name+'/receipt.json',sha256='d'*64)
    return receipts,results,bindings


class Join(unittest.TestCase):
    def setUp(self):self.receipts,self.results,self.bindings=fixture()
    def compose(self):return r.compose(self.receipts,self.results,self.bindings)

    def test_all_thirteen_cases_three_sdks_two_modes_and_controls(self):
        value=self.compose();self.assertEqual(len(value['coverage']),78)
        self.assertEqual(value['totals'],dict(processes=124,normal_processes=78,core_sync_controls=8,rejection_controls=38,
            verified_images=24240,verified_buffers=24024,verified_bytes=2918751264))
        self.assertEqual(len(value['frozen_file_union']),1)

    def test_mixed_native_revision_rejected(self):
        self.receipts['draw']['source_commit']='f'*40
        with self.assertRaises(ValueError):self.compose()

    def test_qualification_and_probe_revision_must_match(self):
        self.receipts['matrix']['readiness']['qualification']['source_commit']='f'*40
        with self.assertRaises(ValueError):self.compose()

    def test_different_environment_or_device_qualification_rejected(self):
        self.receipts['draw']['probe']['qualification']['environment']['cpu']='other'
        with self.assertRaises(ValueError):self.compose()

    def test_different_qualified_llgl_build_rejected(self):
        self.receipts['pso']['probe']['binaries']['core']['sha256']='f'*64
        with self.assertRaises(ValueError):self.compose()

    def test_conflicting_shared_source_hash_rejected(self):
        self.receipts['pso']['source_inputs'][0]['sha256']='f'*64
        with self.assertRaises(ValueError):self.compose()

    def test_conflicting_frozen_runtime_hash_rejected(self):
        self.receipts['allocation']['frozen_inputs'][0]['sha256']='f'*64
        with self.assertRaises(ValueError):self.compose()

    def test_cross_component_process_overlap_rejected(self):
        self.receipts['draw']['runs'][0]['started']=0.0
        with self.assertRaises(ValueError):self.compose()

    def test_nonfinite_process_boundary_rejected(self):
        self.receipts['upload']['runs'][0]['started']=float('nan')
        with self.assertRaises(ValueError):self.compose()

    def test_missing_duplicate_and_foreign_scenario_rejected(self):
        a=self.receipts['draw']['runs'];a[0]['job']=copy.deepcopy(a[1]['job'])
        with self.assertRaises(ValueError):self.compose()
        a[0]['job']['scenario']='foreign'
        with self.assertRaises(ValueError):self.compose()

    def test_workload_contract_must_match_across_sdks(self):
        self.receipts['upload']['runs'][0]['result']['contract']='other'
        with self.assertRaises(ValueError):self.compose()

    def test_scope_or_hardware_promotion_rejected(self):
        for key,value in [('performance_samples',1),('physical_GPU_accepted',True),('performance_accepted',True)]:
            old=self.receipts['matrix'][key];self.receipts['matrix'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.compose()
            self.receipts['matrix'][key]=old
        self.receipts['matrix']['readiness']['qualification']['selected']['device_type']=2
        with self.assertRaises(ValueError):self.compose()

    def test_boolean_readback_counts_rejected(self):
        self.receipts['upload']['runs'][0]['result']['verified_images']=True
        with self.assertRaises(ValueError):self.compose()

    def test_no_diagnostic_timings_statistics_or_rankings_exported(self):
        report=self.compose();text=json.dumps(report)
        for key in ('samples_ms','CPU_intervals_ms','p50_ms','statistics'):self.assertNotIn(key,text)
        self.assertEqual(report['diagnostic_CPU_rows_retained_in_raw_evidence'],156)
        self.assertFalse(report['overall_SDK_ranking']);self.assertFalse(report['Phase3_completed'])
        llgl=[row for row in report['coverage'] if row['sdk']=='llgl'];other=[row for row in report['coverage'] if row['sdk']!='llgl']
        self.assertEqual(sum(row['native_draw_observed'] for row in llgl),10)
        self.assertFalse(any(row['native_draw_observed'] for row in other))

    def test_handoff_is_unexecuted_and_uses_real_supported_workflow_inputs(self):
        handoff=r.handoff();self.assertFalse(handoff['executed'])
        for row in handoff['steps']:
            if 'workflow' in row:
                self.assertTrue((r.ROOT/row['workflow']).is_file())
                for name in row['inputs']:self.assertIn(name+':',(r.ROOT/row['workflow']).read_text(encoding='utf-8'))
        self.assertIn('reject physical mode',handoff['steps'][2]['remaining'])


class Bundle(unittest.TestCase):
    def unpack_fixture(self,entries,inventory=None,symlink=False):
        with tempfile.TemporaryDirectory() as td:
            folder=pathlib.Path(td);archive=folder/'evidence.zip';rows=[]
            with zipfile.ZipFile(archive,'w') as z:
                for name,data in entries:
                    info=zipfile.ZipInfo(name)
                    if symlink:info.create_system=3;info.external_attr=0o120777<<16
                    z.writestr(info,data);rows.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
            r.unpack(archive,inventory if inventory is not None else rows,folder/'target')
            return [(folder/'target'/name).read_bytes() for name,_ in entries]

    def test_empty_raw_stderr_and_binary_source_zip_bytes_preserved(self):
        entries=[('upload/commands/control.stdout',b''),('matrix/source.zip',b'PK\x03\x04\x00\xff')]
        self.assertEqual(self.unpack_fixture(entries),[data for _,data in entries])

    def test_unsafe_paths_case_alias_and_symlink_rejected(self):
        for name in ('../outside','/upload/a','upload/../a','upload\\a','upload/C:a','other/a'):
            with self.subTest(name=name),self.assertRaises(ValueError):self.unpack_fixture([(name,b'a')])
        with self.assertRaises(ValueError):self.unpack_fixture([('upload/A',b'a'),('upload/a',b'b')])
        with self.assertRaises(ValueError):self.unpack_fixture([('upload/a',b'target')],symlink=True)

    def test_hash_inventory_and_size_drift_rejected(self):
        for rows in ([],[dict(path='upload/a',bytes=2,sha256='a'*64)],[dict(path='upload/a',bytes=1,sha256='a'*64)]):
            with self.subTest(rows=rows),self.assertRaises(ValueError):self.unpack_fixture([('upload/a',b'a')],rows)


if __name__=='__main__':unittest.main()
