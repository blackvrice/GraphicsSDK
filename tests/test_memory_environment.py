"""Environment controls use synthetic cases; actual GPU observations are separate."""
import argparse
import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import memory_environment as env
from test_memory_campaign import current,with_budgets,FP


def snapshot():
    files={role:dict(path=str(path.resolve()),sha256='a'*64) for role,path in env.ROLES.items()}
    controls=dict(SDK_source_fingerprint=FP,files=files,runtime={name:dict(path='C:/Windows/System32/'+name,sha256='b'*64,version='1') for name in env.RUNTIMES},
        os=dict(version='10.0.test',build='test'),cpu=[dict(name='CPU',manufacturer='Vendor',cores=4,threads=8)],
        bios=[dict(manufacturer='Vendor',version='1',release='2026-01-01')],microcode=[1,2,3,4],
        adapter=dict(name='Test GPU',vendor_id=0x8086,device_id=0xa780,pnp_sha256='c'*64),other_gpu_drivers=[])
    return dict(schema=1,controls=controls,selected_driver=dict(version='1.0',date='2026-01-01'),
        observations=dict(utc='2026-10-23T00:00:00+00:00',boot_utc='2026-10-22T00:00:00+00:00',adapter_id=1))


def write_workload(base,snap,s,code=0):
    folder=base/'workload';folder.mkdir()
    h=copy.deepcopy(snap);files=h['controls']['files']
    command=[files['stress']['path'],'--mode','validation','--seconds',str(s['seconds']),'--warmup',str(s['warmup']),
        '--interval',str(s['interval']),'--max-private-growth-mib',str(s['max_private_growth_mib']),
        '--output',str((folder/'validation.jsonl').resolve()),'--adapter',str(h['observations']['adapter_id'])]
    frozen={files[role]['path']:files[role]['sha256'] for role in ('stress','policy','runner','native_source','analyzer','core_library')}
    env.save(folder/'campaign.json',dict(inputs_unchanged=True,SDK_source_fingerprint=FP,frozen_inputs=frozen,
        processes=[dict(status='exited',returncode=code,command=command)]))
    acceptance=dict(accepted=False,diagnostic_only=s['short_diagnostic']);env.save(folder/'acceptance.json',acceptance)
    reference=None
    if code==0:
        rows=with_budgets(current('validation',h['observations']['adapter_id']))
        rows[0].update({k:s[k] for k in ('mode','seconds','warmup','interval')})
        rows[0]['private_growth_limit_bytes']=s['max_private_growth_mib']*2**20
        (folder/'validation.jsonl').write_text('\n'.join(json.dumps(r) for r in rows)+'\n')
        reference=env.validate_native(rows,FP,'validation',h['observations']['adapter_id'],0)
    return acceptance,reference


def case(base,snap=None,seconds=None,code=0):
    base.mkdir();snap=snapshot() if snap is None else snap;s=env.settings(seconds,512)
    env.save(base/'environment-before.json',snap);env.save(base/'environment-after.json',snap)
    acceptance,reference=write_workload(base,snap,s,code)
    result=dict(schema=1,status='recorded',diagnostic_only=True,memory_acceptance=False,settings=s,runner_exit_code=0 if code==0 else 1,
        native_exit_code=code,outcome='completed' if code==0 else 'native_failed',native_acceptance_receipt=acceptance)
    if reference:result['references']=reference
    result['artifacts']=[dict(path=p.relative_to(base).as_posix(),sha256=env.sha(p.read_bytes())) for p in sorted(base.rglob('*')) if p.is_file()]
    env.save(base/'case.json',result);return base/'case.json'


class MemoryEnvironmentContracts(unittest.TestCase):
    def test_same_environment_ignores_observation_time_but_never_accepts_memory(self):
        a=snapshot();b=copy.deepcopy(a);b['observations']['utc']='later'
        result=env.compare_snapshots(a,b)
        self.assertTrue(result['recorded_controls_match']);self.assertFalse(result['memory_acceptance'])
        self.assertFalse(result['crash_cause_resolved'])

    def test_driver_change_requires_real_change_and_all_other_controls_equal(self):
        a=snapshot();b=copy.deepcopy(a)
        self.assertFalse(env.compare_snapshots(a,b,'selected-driver')['recorded_controls_match'])
        b['selected_driver']['version']='2.0'
        self.assertTrue(env.compare_snapshots(a,b,'selected-driver')['recorded_controls_match'])
        self.assertFalse(env.compare_snapshots(a,b,'none')['recorded_controls_match'])
        b['controls']['runtime']['dxgi.dll']['sha256']='d'*64
        result=env.compare_snapshots(a,b,'selected-driver')
        self.assertFalse(result['recorded_controls_match']);self.assertIn('controls.runtime.dxgi.dll.sha256',result['differences'])

    def test_cpu_bios_microcode_policy_sdk_binary_gpu_and_os_changes_reject(self):
        a=snapshot()
        for key in ('cpu','bios','microcode','policy','sdk','binary','adapter','other_gpu','os'):
            b=copy.deepcopy(a);b['selected_driver']['version']='2'
            if key=='cpu':b['controls']['cpu'][0]['name']='different'
            if key=='bios':b['controls']['bios'][0]['version']='2'
            if key=='microcode':b['controls']['microcode'][0]=99
            if key=='policy':b['controls']['files']['policy']['sha256']='d'*64
            if key=='sdk':b['controls']['SDK_source_fingerprint']='d'*64
            if key=='binary':b['controls']['files']['stress']['sha256']='d'*64
            if key=='adapter':b['controls']['adapter']['pnp_sha256']='d'*64
            if key=='other_gpu':b['controls']['other_gpu_drivers']=[dict(name='Other GPU',pnp_sha256='d'*64,version='1',date='today')]
            if key=='os':b['controls']['os']['build']='different'
            with self.subTest(key=key):self.assertFalse(env.compare_snapshots(a,b,'selected-driver')['recorded_controls_match'])

    def test_reboot_allows_fresh_id_only_between_runs(self):
        a=snapshot();b=copy.deepcopy(a);b['observations']['adapter_id']=9
        self.assertFalse(env.compare_snapshots(a,b)['recorded_controls_match'])
        b['observations']['boot_utc']='new-boot'
        self.assertTrue(env.compare_snapshots(a,b)['recorded_controls_match'])
        self.assertFalse(env.compare_snapshots(a,b,within_run=True)['recorded_controls_match'])

    def test_missing_or_malformed_control_is_rejected(self):
        for key in ('cpu','bios','microcode','files','runtime','adapter'):
            a=snapshot();a['controls'].pop(key)
            with self.subTest(key=key),self.assertRaises(ValueError):env.validate_snapshot(a)
        for mutate in ('bool_core','empty_bios','bad_microcode','no_runtime_version','bad_pnp','extra_control','bool_adapter_id'):
            a=snapshot()
            if mutate=='bool_core':a['controls']['cpu'][0]['cores']=True
            if mutate=='empty_bios':a['controls']['bios'][0]['version']=''
            if mutate=='bad_microcode':a['controls']['microcode']=[True]
            if mutate=='no_runtime_version':a['controls']['runtime']['dxgi.dll'].pop('version')
            if mutate=='bad_pnp':a['controls']['adapter']['pnp_sha256']='bad'
            if mutate=='extra_control':a['controls']['unexpected']=1
            if mutate=='bool_adapter_id':a['observations']['adapter_id']=True
            with self.subTest(mutate=mutate),self.assertRaises(ValueError):env.validate_snapshot(a)

    def test_cases_bind_workload_settings_and_functional_references(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);a=case(root/'a');b=case(root/'b')
            self.assertTrue(env.compare_cases(a,b,'none')['recorded_controls_match'])
            c=case(root/'c',seconds=3)
            result=env.compare_cases(a,c,'none');self.assertFalse(result['recorded_controls_match'])
            self.assertIn('settings.seconds',result['differences'])

    def test_partial_crash_is_recorded_as_native_failed_without_invented_references(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=case(pathlib.Path(tmp)/'crash',code=0xc0000005)
            value,_,_=env.validated_case(path)
            self.assertEqual(value['outcome'],'native_failed');self.assertNotIn('references',value)
            self.assertFalse(env.compare_cases(path,path,'none')['memory_acceptance'])

    def test_tampered_artifact_and_outside_path_are_rejected(self):
        for mutation in ('artifact','outside','duplicate','missing'):
            with tempfile.TemporaryDirectory() as tmp:
                root=pathlib.Path(tmp);p=case(root/'a');data=env.load(p)
                if mutation=='artifact':(p.parent/'environment-before.json').write_text('{}')
                if mutation=='outside':data['artifacts'][0]['path']='../outside.json'
                if mutation=='duplicate':data['artifacts'].append(data['artifacts'][0])
                if mutation=='missing':data['artifacts']=[]
                env.save(p,data)
                with self.subTest(mutation=mutation),self.assertRaises((ValueError,OSError)):env.validated_case(p)

    def test_case_metadata_cannot_override_native_command_or_results(self):
        for mutation in ('settings','outcome','exit','references','recorded','policy_binding','command'):
            with tempfile.TemporaryDirectory() as tmp:
                p=case(pathlib.Path(tmp)/'a');data=env.load(p)
                if mutation=='settings':data['settings']=env.settings(None,1024)
                if mutation=='outcome':data['outcome']='native_failed'
                if mutation=='exit':data['native_exit_code']=True
                if mutation=='references':data['references']['frames']+=8
                if mutation=='recorded':data['status']='starting'
                if mutation in ('policy_binding','command'):
                    target=p.parent/'workload/campaign.json';native=env.load(target)
                    if mutation=='policy_binding':native['frozen_inputs']={}
                    else:native['processes'][0]['command'][2]='gpu'
                    env.save(target,native)
                    for r in data['artifacts']:
                        if r['path']=='workload/campaign.json':r['sha256']=env.sha(target.read_bytes())
                env.save(p,data)
                with self.subTest(mutation=mutation),self.assertRaises(ValueError):env.validated_case(p)

    def test_record_wrapper_records_normal_run_and_rejects_environment_drift(self):
        for drift in (False,True):
            with tempfile.TemporaryDirectory() as tmp:
                base=pathlib.Path(tmp)/'run';before=snapshot();after=copy.deepcopy(before)
                if drift:after['selected_driver']['version']='2'
                args=argparse.Namespace(output=base,adapter=1,diagnostic_seconds=3,max_private_growth_mib=512)
                def run_fake(a):write_workload(base,before,env.settings(3,512));return 0
                with patch.object(env,'collect_snapshot',side_effect=[before,after]),patch.object(env,'run_stress',side_effect=run_fake):
                    code=env.record(args)
                self.assertEqual(code,1 if drift else 0)
                data=env.load(base/'case.json');self.assertEqual(data['status'],'rejected' if drift else 'recorded')
                if not drift:env.validated_case(base/'case.json')

    def test_runner_exception_is_preserved_and_existing_output_not_probed(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=pathlib.Path(tmp)/'run';args=argparse.Namespace(output=base,adapter=1,diagnostic_seconds=3,max_private_growth_mib=512)
            with patch.object(env,'collect_snapshot',return_value=snapshot()),patch.object(env,'run_stress',side_effect=OSError('test failure')):
                self.assertEqual(env.record(args),1)
            self.assertIn('test failure',env.load(base/'case.json')['error'])
            original=(base/'case.json').read_bytes()
            with patch.object(env,'collect_snapshot') as collect,self.assertRaisesRegex(ValueError,'exists'):env.record(args)
            collect.assert_not_called();self.assertEqual((base/'case.json').read_bytes(),original)

    def test_record_rejects_command_binding_error_before_reporting_recorded(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=pathlib.Path(tmp)/'run';snap=snapshot()
            args=argparse.Namespace(output=base,adapter=1,diagnostic_seconds=3,max_private_growth_mib=512)
            def run_fake(a):
                write_workload(base,snap,env.settings(3,512));p=base/'workload/campaign.json';data=env.load(p)
                data['processes'][0]['command'][2]='gpu';env.save(p,data);return 0
            with patch.object(env,'collect_snapshot',return_value=snap),patch.object(env,'run_stress',side_effect=run_fake):
                self.assertEqual(env.record(args),1)
            self.assertIn('command/settings mismatch',env.load(base/'case.json')['error'])

    def test_compare_cli_never_overwrites_existing_result(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);a=case(root/'a');b=case(root/'b');output=root/'comparison.json';output.write_bytes(b'keep')
            result=subprocess.run([sys.executable,str(ROOT/'tools/memory_environment.py'),'compare','--before',str(a),
                '--after',str(b),'--change','none','--output',str(output)],capture_output=True)
            self.assertEqual(result.returncode,1);self.assertEqual(output.read_bytes(),b'keep')

    def test_collect_snapshot_requires_unique_hardware_and_exact_pci_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);binary=root/'stress.exe';binary.write_bytes(FP.encode())
            roles={name:binary for name in env.ROLES}
            gpu=dict(event='adapter',adapter_id=1,name='Test GPU',software=False,vendor_id=0x8086,device_id=0xa780)
            tail=dict(event='adapter_list',count=1,sdk_fingerprint=FP)
            snap=snapshot();native=dict(os=snap['controls']['os'],boot='boot',cpu=snap['controls']['cpu'],bios=snap['controls']['bios'],microcode=[1,2,3,4],
                runtime=snap['controls']['runtime'],gpu=[dict(name='Test GPU',pnp='PCI\\VEN_8086&DEV_A780&SUBSYS_12345678',version='1',date='date')])
            for mutation in ('good','duplicate','software','wrong_pci','wrong_name'):
                rows=[copy.deepcopy(gpu),copy.deepcopy(tail)];data=copy.deepcopy(native)
                if mutation=='duplicate':rows.insert(0,copy.deepcopy(gpu));rows[-1]['count']=2
                if mutation=='software':rows[0]['software']=True
                if mutation=='wrong_pci':data['gpu'][0]['pnp']='PCI\\VEN_8086&DEV_0001'
                if mutation=='wrong_name':data['gpu'][0]['name']='Other GPU'
                replies=[subprocess.CompletedProcess([],0,('\n'.join(json.dumps(r) for r in rows)+'\n').encode(),b''),
                         subprocess.CompletedProcess([],0,json.dumps(data).encode(),b'')]
                with patch.object(env,'ROLES',roles),patch.object(env,'STRESS',binary),patch.object(env,'sdk_fingerprint',return_value=FP),patch.object(env.subprocess,'run',side_effect=replies):
                    if mutation=='good':self.assertEqual(env.collect_snapshot(1)['selected_driver']['version'],'1')
                    else:
                        with self.subTest(mutation=mutation),self.assertRaises(ValueError):env.collect_snapshot(1)


if __name__=='__main__':unittest.main()
