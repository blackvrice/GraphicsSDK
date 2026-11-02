"""Physical policies and generated bodies; synthetic metadata is never GPU evidence."""
import copy
import hashlib
import importlib
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_llgl_workload_mode as m


class WorkloadModes(unittest.TestCase):
    def probe(self, physical=False, guard=False):
        r=m.p.read(m.p.ROOT/'benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/native/receipt.json')
        if physical:
            r.update(mode='physical',physical_guard_only=guard,physical_probe_qualified=not guard,
                     P3_BEN_08_completed=False,minimal_buffer_reference_accepted=not guard)
            if not guard:
                a=r['qualification'];d=a['selected'];d['device_type']=2
                a.update(mode='physical',physical_probe_qualified=True,P3_BEN_07_completed=True,
                         inventory=[copy.deepcopy(d)],pci=[dict(vendor_id=d['vendor_id'],device_id=d['device_id'],driver='amdgpu')])
        return r

    def receipt(self,profile,physical=False,guard=False):
        r=dict(probe=self.probe(physical,guard),adaptation={},verified_images=0,verified_bytes=0,verified_buffers=0,
               **{f'software_{profile}_correctness_verified':False})
        if physical:r['adaptation']['mode']='physical'
        m.initialize(r,'physical' if physical else 'software-diagnostic',guard,profile)
        m.finish(r,profile);return r

    def test_all_four_generated_physical_adapters_keep_workload_and_observer_bytes(self):
        for profile in m.PROFILES:
            port=importlib.import_module('port_linux_llgl_'+profile)
            with self.subTest(profile=profile),tempfile.TemporaryDirectory(dir=m.p.ROOT/'out') as td:
                root=pathlib.Path(td);soft=port.generate(root/'soft');physical=port.generate(root/'physical','physical')
                self.assertNotEqual(soft['source_fingerprint'],physical['source_fingerprint'])
                self.assertEqual(physical['mode'],'physical');self.assertFalse(physical['workload_bodies_changed'])
                for name in soft['generated']:
                    if name!='Platform.hpp':self.assertEqual((root/'soft'/name).read_bytes(),(root/'physical'/name).read_bytes())
                platform=(root/'physical/Platform.hpp').read_text(encoding='utf-8')
                self.assertIn('require(kind&&std::string_view(kind)=="physical","Physical qualification only")',platform)
                self.assertIn('expected,"physical")',platform)
                self.assertIn('\\"mode\\":\\"physical\\"',platform)
                self.assertLess(platform.index('selected=linuxbench::select'),platform.index('RenderSystem::Load'))
                self.assertIn(m.SOURCE,port.OWN)

    def test_explicit_software_generation_matches_legacy_default_and_invalid_mode_fails(self):
        for profile in m.PROFILES:
            port=importlib.import_module('port_linux_llgl_'+profile)
            with self.subTest(profile=profile),tempfile.TemporaryDirectory(dir=m.p.ROOT/'out') as td:
                out=pathlib.Path(td)
                self.assertEqual(port.generate(out),port.generate(out,'software-diagnostic'))
                with self.assertRaises(ValueError):port.generate(out/'invalid','automatic')

    def test_generated_identity_binds_mode_and_shared_policy_source(self):
        for profile in m.PROFILES:
            port=importlib.import_module('port_linux_llgl_'+profile)
            with self.subTest(profile=profile),tempfile.TemporaryDirectory(dir=m.p.ROOT/'out') as td:
                v=port.generate(pathlib.Path(td),'physical')
                identity=m.prefix(profile,'physical')+'|'+self.probe()['LLGL_commit']+'|'+m.p.PROFILE+'\n'
                identity+=''.join(x['path']+'|'+x['sha256']+'\n' for x in v['inputs'])
                self.assertEqual(hashlib.sha256(identity.encode()).hexdigest(),v['source_fingerprint'])
                item=next(x for x in v['inputs'] if x['path']==m.SOURCE)
                self.assertEqual(item['sha256'],m.p.sha(m.p.ROOT/m.SOURCE))

    def test_qualified_physical_and_guard_probes_are_distinct(self):
        for physical,guard in ((False,False),(True,False),(True,True)):
            r=self.probe(physical,guard);mode='physical' if physical else 'software-diagnostic'
            m.validate_probe(r,mode,guard)
            with self.subTest(physical=physical,guard=guard),self.assertRaises(ValueError):m.validate_probe(r,mode,not guard)
        with self.assertRaises(ValueError):m.validate_probe(self.probe(),'physical')
        with self.assertRaises(ValueError):m.validate_probe(self.probe(True),'software-diagnostic')

    def test_physical_probe_requires_pci_native_type_and_completed_minimal_reference(self):
        for path,value in [('pci',[]),('device_type',4),('physical_probe_qualified',False)]:
            r=self.probe(True)
            if path=='pci':r['qualification']['pci']=value
            elif path=='device_type':
                r['qualification']['selected'][path]=value;r['qualification']['inventory'][0][path]=value
            else:r[path]=value
            with self.subTest(path=path),self.assertRaises(ValueError):m.validate_probe(r,'physical')
        for key,value in [('status','failed'),('inputs_unchanged',False),('minimal_buffer_reference_accepted',False),
                          ('performance_samples',True),('workload_correctness_accepted',True)]:
            r=self.probe(True);r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):m.validate_probe(r,'physical')

    def test_receipts_never_promote_guard_to_workload_correctness(self):
        for profile in m.PROFILES:
            for physical,guard in ((False,False),(True,False),(True,True)):
                r=self.receipt(profile,physical,guard);m.validate_receipt(r,profile)
                key=('physical_' if physical else 'software_')+profile+'_correctness_verified'
                r[key]=not r[key]
                with self.subTest(profile=profile,physical=physical,guard=guard),self.assertRaises(ValueError):m.validate_receipt(r,profile)
            r=self.receipt(profile,True,True);r['physical_guard_only']=1
            with self.assertRaises(ValueError):m.validate_receipt(r,profile)

    def test_receipt_mode_cannot_be_transplanted_or_claim_ben08_completion(self):
        for profile in m.PROFILES:
            for key,value in [('mode','software-diagnostic'),('P3_BEN_08_completed',True),
                              ('physical_guard_only',True),(f'physical_{profile}_correctness_verified',1)]:
                r=self.receipt(profile,True);r[key]=value
                with self.subTest(profile=profile,key=key),self.assertRaises(ValueError):m.validate_receipt(r,profile)
            r=self.receipt(profile);r[f'physical_{profile}_correctness_verified']=True
            with self.assertRaises(ValueError):m.validate_receipt(r,profile)
            r=self.receipt(profile,True);r['adaptation']['mode']='software-diagnostic'
            with self.assertRaises(ValueError):m.validate_receipt(r,profile)

    def test_guard_schedule_has_four_intended_rejections_in_each_consumer(self):
        for profile in m.PROFILES:
            runner=importlib.import_module('linux_llgl_'+profile);r=self.receipt(profile,True,True)
            jobs=m.jobs(r,runner.jobs());self.assertEqual(len(jobs),4)
            self.assertEqual(len({j['name'] for j in jobs}),4)
            self.assertEqual({j['kind'] for j in jobs},{'native-software','wrong-uuid','wrong-kind','missing-layer'})
            self.assertTrue(all(j['scenario']==runner.jobs()[0]['scenario'] for j in jobs))
            self.assertEqual(m.jobs(self.receipt(profile,True),runner.jobs()),runner.jobs())

    def test_guard_argv_environment_and_error_reasons_never_fallback(self):
        fixture_names=dict(upload='UploadContracts',draw='DrawContracts',pso='PSOContracts',allocation='ResourceContracts')
        tokens=dict(missing_layer=b'Required comparison validation layer absent',wrong_uuid=b'Exact UUID absent or ambiguous',
                    wrong_kind=b'Physical qualification only',native_software=b'Requested probe kind differs from native device type')
        for profile in m.PROFILES:
            runner=importlib.import_module('linux_llgl_'+profile)
            cls=getattr(importlib.import_module('test_linux_llgl_'+profile),fixture_names[profile]);cls.setUpClass()
            fixture=cls();fixture.setUp();r=fixture.r;r.update(mode='physical',physical_guard_only=True,probe=self.probe(True,True))
            for job in m.jobs(r,runner.jobs()):
                args=runner.arguments(job,r);env=runner.environment(job,r)
                self.assertEqual(args[1],'0'*32 if job['kind']=='wrong-uuid' else r['probe']['qualification']['device_uuid'])
                self.assertEqual(env['GFX_LLGL_'+profile.upper()+'_KIND'],'software-diagnostic' if job['kind']=='wrong-kind' else 'physical')
                token=tokens[job['kind'].replace('-','_')];runner.rejected(job,1,b'',token,'physical')
                for code,out,err in [(0,b'',token),(True,b'',token),(1,b'{}',token),(1,b'',b'unrelated')]:
                    with self.subTest(profile=profile,kind=job['kind']),self.assertRaises(ValueError):runner.rejected(job,code,out,err,'physical')

    def test_physical_result_replay_keeps_full_reference_native_identity_and_no_performance(self):
        fixture_names=dict(upload='UploadContracts',draw='DrawContracts',pso='PSOContracts',allocation='ResourceContracts')
        for profile in m.PROFILES:
            runner=importlib.import_module('linux_llgl_'+profile)
            cls=getattr(importlib.import_module('test_linux_llgl_'+profile),fixture_names[profile]);cls.setUpClass()
            fixture=cls();fixture.setUp();fixture.r.update(mode='physical',probe=self.probe(True))
            for job in runner.jobs():
                if job['kind']!='normal' and job['kind'] not in runner.TOKENS:continue
                value=fixture.result(job);value['mode']='physical';runner.validate_result(value,job,fixture.r)
                for key,new in [('mode','software-diagnostic'),('physical_GPU_accepted',True),('performance_accepted',True),
                                ('verified_bytes',0 if value['verified_bytes'] else 1),('performance_samples',1)]:
                    bad=copy.deepcopy(value);bad[key]=new
                    with self.subTest(profile=profile,key=key),self.assertRaises(ValueError):runner.validate_result(bad,job,fixture.r)

    def test_guard_counters_and_scope_do_not_export_expected_full_work_as_executed(self):
        for profile in m.PROFILES:
            r=self.receipt(profile,True,True)
            self.assertEqual(m.totals(r,(32,862694560)),(0,0))
            result=m.summary(r,dict(processes=72,normal_processes=26,core_sync_controls=8,rejection_controls=38,verified_bytes=0),profile)
            self.assertEqual((result['processes'],result['normal_processes'],result['core_sync_controls'],result['rejection_controls']),(4,0,0,4))
            self.assertFalse(result[f'physical_{profile}_correctness_verified']);self.assertFalse(result['P3_BEN_08_completed'])
            self.assertEqual(m.totals(self.receipt(profile,True),(32,862694560)),(32,862694560))

    def test_unknown_modes_and_profiles_fail(self):
        for mode in ('automatic','',None,False):
            with self.subTest(mode=mode),self.assertRaises(ValueError):m.prefix('upload',mode)
        with self.assertRaises(ValueError):m.prefix('unknown')

    def test_zero_guard_counters_reject_booleans_and_negative_values(self):
        for profile in m.PROFILES:
            keys=['verified_images','verified_bytes']+(['verified_buffers'] if profile=='allocation' else [])
            for key in keys:
                for value in (False,True,-1,0.0):
                    r=self.receipt(profile,True,True);r[key]=value
                    with self.subTest(profile=profile,key=key,value=value),self.assertRaises(ValueError):m.validate_receipt(r,profile)


if __name__=='__main__':unittest.main()
