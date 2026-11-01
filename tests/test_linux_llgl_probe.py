"""Linux LLGL attribution, approved patch and diagnostic-only acceptance contracts."""
import copy
import hashlib
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_llgl_probe as p


class LinuxLLGLContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.qualification=p.read(p.ROOT/'benchmarks/reports/validation/2026-10-30-phase3-linux-workload-matrix/native/readiness/qualification/receipt.json')

    def setUp(self):
        self.r=dict(qualification=copy.deepcopy(self.qualification),probe_fingerprint='a'*64,
            LLGL_commit='649c0cc8fa608312c5e4b94dae409a5859fc7553',output_directory='/output',
            binaries={n:dict(path=path) for n,path in [('probe','/build/linux_llgl_probe'),('core','/build/lib/libLLGL.so'),('backend','/build/lib/libLLGL_Vulkan.so')]})

    def result(self, job):
        a=self.r['qualification']; control=job['kind']=='callback-control'; smoke=job['operation']=='smoke'
        modules={x['path'] for x in a['runtime_libraries']}|{self.r['binaries'][n]['path'] for n in ('core','backend')}
        if job['validation']:modules.add(a['validation_library'])
        return dict(schema_version=1,sdk='llgl',task='P3-LBEN-LLGL-readiness',profile_id=p.PROFILE,unmodified_upstream=False,
            source_fingerprint='a'*64,LLGL_commit=self.r['LLGL_commit'],selected=copy.deepcopy(a['selected']),environment=copy.deepcopy(a['environment']),
            mode='software-diagnostic',operation=job['operation'],native_handle_verified=True,queue_index=0,queue_family=0,queue_flags=15,
            validation=job['validation'],control_only=control,delivered_errors=1 if control else 0,diagnostic_ids=[p.VUID] if control else [],
            verified_bytes=12288 if smoke else 0,reference_rounds=3 if smoke else 0,submitted_control_work=0,
            loader_path=a['loader_path'],loader_api_version=4206867,LLGL_path=self.r['binaries']['core']['path'],backend_path=self.r['binaries']['backend']['path'],
            loaded_validation_layer=a['validation_library'] if job['validation'] else '',runtime_modules=sorted(modules),LLGL_log_errors=1 if control else 0,
            instance_API_request_observed=False,performance_samples=0,samples_ms=[],gpu_ms=None,physical_GPU_accepted=False,
            workload_correctness_accepted=False,performance_accepted=False)

    def test_all_probe_and_callback_results(self):
        for job in p.jobs():
            if job['kind'] in ('normal','callback-control'):
                p.validate_result(self.result(job),job,self.r)

    def test_scope_cannot_be_promoted(self):
        job=p.jobs()[0]
        for key,val in [('unmodified_upstream',True),('physical_GPU_accepted',True),('workload_correctness_accepted',True),
                        ('performance_accepted',True),('performance_samples',False),('samples_ms',[1]),('gpu_ms',1),('instance_API_request_observed',True)]:
            value=self.result(job);value[key]=val
            with self.subTest(key=key),self.assertRaises(ValueError):p.validate_result(value,job,self.r)

    def test_actual_device_and_source_must_match(self):
        job=p.jobs()[0]
        for key,val in [('source_fingerprint','b'*64),('LLGL_commit','b'*40),('native_handle_verified',False),('queue_index',1)]:
            value=self.result(job);value[key]=val
            with self.subTest(key=key),self.assertRaises(ValueError):p.validate_result(value,job,self.r)
        for key in self.r['qualification']['selected']:
            value=self.result(job);value['selected'][key]=None if key!='device_luid' else 'synthetic'
            with self.subTest(key=key),self.assertRaises(ValueError):p.validate_result(value,job,self.r)

    def test_environment_cannot_be_transplanted(self):
        job=p.jobs()[0];value=self.result(job);value['environment']['kernel']='other'
        with self.assertRaises(ValueError):p.validate_result(value,job,self.r)

    def test_native_integer_fields_reject_boolean(self):
        job=p.jobs()[0]
        for key in ('queue_family','queue_index','queue_flags','loader_api_version','LLGL_log_errors'):
            value=self.result(job);value[key]=True
            with self.subTest(key=key),self.assertRaises(ValueError):p.validate_result(value,job,self.r)
        value=self.result(job);value['selected']['device_id']=False
        with self.assertRaises(ValueError):p.validate_result(value,job,self.r)

    def test_full_changing_reference_required(self):
        job=next(j for j in p.jobs() if j['operation']=='smoke' and j['kind']=='normal')
        for key,val in [('reference_rounds',2),('verified_bytes',4096),('delivered_errors',1)]:
            value=self.result(job);value[key]=val
            with self.subTest(key=key),self.assertRaises(ValueError):p.validate_result(value,job,self.r)

    def test_identity_probe_cannot_claim_readback(self):
        job=p.jobs()[0];value=self.result(job);value['verified_bytes']=12288
        with self.assertRaises(ValueError):p.validate_result(value,job,self.r)

    def test_callback_control_requires_real_vuid_and_no_gpu_work(self):
        job=next(j for j in p.jobs() if j['kind']=='callback-control')
        for key,val in [('diagnostic_ids',[]),('delivered_errors',0),('submitted_control_work',1),('verified_bytes',1),('LLGL_log_errors',2)]:
            value=self.result(job);value[key]=val
            with self.subTest(key=key),self.assertRaises(ValueError):p.validate_result(value,job,self.r)

    def test_actual_validation_module_presence_and_absence(self):
        for job in p.jobs()[:4]:
            for key,val in [('loaded_validation_layer','wrong'),('runtime_modules',[])]:
                value=self.result(job);value[key]=val
                with self.subTest(job=job,key=key),self.assertRaises(ValueError):p.validate_result(value,job,self.r)
        job=p.jobs()[0];value=self.result(job);value['runtime_modules']=sorted(value['runtime_modules']+[self.r['qualification']['validation_library']])
        with self.assertRaises(ValueError):p.validate_result(value,job,self.r)

    def test_unknown_or_duplicate_modules_fail(self):
        job=p.jobs()[0]
        for extra in ('/tmp/capture.so',self.r['binaries']['core']['path']):
            value=self.result(job);value['runtime_modules']=sorted(value['runtime_modules']+[extra])
            with self.subTest(extra=extra),self.assertRaises(ValueError):p.validate_result(value,job,self.r)

    def test_control_rejection_must_have_intended_failure(self):
        for job in p.jobs()[-3:]:
            token={'wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':b'Software diagnostics only','missing-layer':b'Required comparison validation layer absent'}[job['kind']]
            p.rejected(job,1,b'',token)
            for code,out,err in [(0,b'',token),(1,b'{}',token),(1,b'',b'unrelated')]:
                with self.subTest(job=job),self.assertRaises(ValueError):p.rejected(job,code,out,err)

    def test_eight_distinct_jobs_and_explicit_targets(self):
        jobs=p.jobs();self.assertEqual(len(jobs),8);self.assertEqual(len({j['name'] for j in jobs}),8)
        self.assertEqual(sum(j['kind']=='normal' for j in jobs),4)
        for job in jobs:
            args=p.arguments(job,self.r)
            self.assertEqual(args[0],self.r['binaries']['probe']['path'])
            self.assertEqual(args[1],'0'*32 if job['kind']=='wrong-uuid' else self.r['qualification']['device_uuid'])
            self.assertEqual(args[-1],'physical' if job['kind']=='wrong-kind' else 'software-diagnostic')

    def test_approved_overlay_is_exactly_the_existing_ten_line_profile(self):
        original=(p.ROOT/p.APPROVED_ORIGINAL).read_bytes();changed=(p.ROOT/p.APPROVED_OVERLAY).read_bytes()
        self.assertEqual(p.patch(original.decode()).encode(),changed)
        self.assertEqual(len(changed.splitlines())-len(original.splitlines()),10)

    def test_fingerprint_binds_every_source_overlay_pin_and_profile(self):
        inputs={n:'a'*64 for n in p.OWN};base=p.fingerprint(inputs,'b'*64,self.r['LLGL_commit'])
        for name in p.OWN:
            self.assertNotEqual(base,p.fingerprint({**inputs,name:'c'*64},'b'*64,self.r['LLGL_commit']))
        self.assertNotEqual(base,p.fingerprint(inputs,'c'*64,self.r['LLGL_commit']))
        self.assertNotEqual(base,p.fingerprint(inputs,'b'*64,'d'*40))

    def physical_qualification(self):
        # Synthetic metadata tests selection only; this is never native evidence.
        a=copy.deepcopy(self.qualification); selected=copy.deepcopy(a['selected'])
        selected['device_type']=2
        a.update(mode='physical',physical_probe_qualified=True,P3_BEN_07_completed=True,
                 selected=selected,inventory=[copy.deepcopy(selected)],
                 pci=[dict(vendor_id=selected['vendor_id'],device_id=selected['device_id'],driver='amdgpu')])
        return a

    def test_mode_is_part_of_compiled_identity(self):
        inputs={n:'a'*64 for n in p.OWN}
        self.assertNotEqual(p.fingerprint(inputs,'b'*64,'c'*40),p.fingerprint(inputs,'b'*64,'c'*40,'physical'))
        self.assertEqual(p.identity_prefix('physical'),'linux-llgl-physical')
        with self.assertRaises(ValueError):p.identity_prefix('automatic')

    def test_physical_run_requires_physical_qualification(self):
        p.validate_qualification(self.physical_qualification(),'physical')
        with self.assertRaises(ValueError):p.validate_qualification(self.qualification,'physical')
        with self.assertRaises(ValueError):p.validate_qualification(self.physical_qualification(),'software-diagnostic')

    def test_physical_qualification_requires_device_type_and_pci_driver(self):
        for kind in (0,3,4,True):
            a=self.physical_qualification();a['selected']['device_type']=kind;a['inventory']=[copy.deepcopy(a['selected'])]
            with self.subTest(kind=kind),self.assertRaises(ValueError):p.validate_qualification(a,'physical')
        for pci in ([],[dict(vendor_id=0,device_id=0,driver='amdgpu')]):
            a=self.physical_qualification();a['pci']=pci
            with self.subTest(pci=pci),self.assertRaises(ValueError):p.validate_qualification(a,'physical')
        a=self.physical_qualification();a['pci'][0]['driver']='unbound'
        with self.assertRaises(ValueError):p.validate_qualification(a,'physical')

    def test_physical_qualification_does_not_accept_metadata_flags_as_integers(self):
        for key in ('physical_probe_qualified','P3_BEN_07_completed'):
            a=self.physical_qualification();a[key]=1
            with self.subTest(key=key),self.assertRaises(ValueError):p.validate_qualification(a,'physical')

    def test_guard_requires_software_and_explicit_physical_profile(self):
        p.validate_qualification(self.qualification,'physical',True)
        for a,mode,guard in [(self.physical_qualification(),'physical',True),
                             (self.qualification,'software-diagnostic',True),
                             (self.qualification,'physical',1)]:
            with self.subTest(mode=mode,guard=guard),self.assertRaises(ValueError):p.validate_qualification(a,mode,guard)

    def test_physical_arguments_and_guards_never_fallback(self):
        self.r.update(mode='physical',qualification=self.physical_qualification())
        for job in p.jobs():
            argv=p.arguments(job,self.r)
            self.assertEqual(argv[-1],'software-diagnostic' if job['kind']=='wrong-kind' else 'physical')
            self.assertEqual(argv[1],'0'*32 if job['kind']=='wrong-uuid' else self.r['qualification']['device_uuid'])
        self.assertEqual({j['kind'] for j in p.guard_jobs()},{'native-software','wrong-uuid','wrong-kind','missing-layer'})
        self.assertEqual(len({j['name'] for j in p.guard_jobs()}),4)

    def test_physical_control_requires_specific_errors(self):
        messages={'native-software':b'Requested probe kind differs from native device type',
                  'wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':b'Physical qualification only',
                  'missing-layer':b'Required comparison validation layer absent'}
        for job in p.guard_jobs():
            token=messages[job['kind']];p.rejected(job,1,b'',token,'physical')
            for code,stdout,stderr in [(0,b'',token),(True,b'',token),(1,b'{}',token),(1,b'',b'Software diagnostics only')]:
                with self.subTest(job=job,code=code),self.assertRaises(ValueError):p.rejected(job,code,stdout,stderr,'physical')

    def test_physical_results_retain_narrow_scope_and_actual_identity(self):
        self.r.update(mode='physical',qualification=self.physical_qualification())
        for job in p.jobs()[:5]:
            value=self.result(job);value['mode']='physical';p.validate_result(value,job,self.r)
            for key,new in [('physical_GPU_accepted',True),('workload_correctness_accepted',True),
                            ('performance_accepted',True),('performance_samples',1),('mode','software-diagnostic')]:
                bad=copy.deepcopy(value);bad[key]=new
                with self.subTest(key=key),self.assertRaises(ValueError):p.validate_result(bad,job,self.r)
            bad=copy.deepcopy(value);bad['selected']['device_uuid']='b'*32
            with self.assertRaises(ValueError):p.validate_result(bad,job,self.r)

    def test_compiler_flags_are_frozen_and_no_lto_or_tuning(self):
        args=['/usr/bin/g++-14','-O2','-DNDEBUG','-MD','-MF','file.d'];p.compiler_flags(args)
        for flag in ('-flto','-flto=auto','-march=native','-mtune=native','-O0','-O1','-O3','-Os','-Oz'):
            with self.subTest(flag=flag),self.assertRaises(ValueError):p.compiler_flags(args+[flag])
        for missing in ('-O2','-DNDEBUG','-MD','-MF'):
            with self.subTest(missing=missing),self.assertRaises(ValueError):p.compiler_flags([x for x in args if x!=missing])

    def audit_fixture(self):
        categories=['own','approved_overlay','llgl','vulkan_headers','system']; files=[]
        for i,category in enumerate(categories):
            row=dict(path='/fixture/'+str(i),bytes=1,sha256='a'*64,component=category)
            if category=='system':row['redistributed']=False
            else:row['selected_license']='MIT' if category in ('own','vulkan_headers') else 'BSD-3-Clause'
            files.append(row)
        raw='file.o: '+' '.join(x['path'] for x in files)+'\n'
        return dict(files=files,compile_commands=[dict(command='g++ -O2 -DNDEBUG -c /fixture/0 -o file.o',
            executed_command='g++ -O2 -DNDEBUG -MD -MT file.o -MF file.d -c /fixture/0 -o file.o') for _ in range(100)],
            depfiles=[dict(raw=raw,bytes=len(raw.encode()),sha256=hashlib.sha256(raw.encode()).hexdigest(),
                dependencies=[x['path'] for x in files]) for _ in range(100)])

    def test_raw_gnu_dependencies_and_executed_command_binding(self):
        value=self.audit_fixture();p.validate_audit(value)
        for key,change in [('depfile',lambda r:r['depfiles'][0].update(raw='tampered')),
                           ('ninja',lambda r:r['compile_commands'][0].update(executed_command=r['compile_commands'][0]['executed_command']+' -g')),
                           ('missing',lambda r:r['compile_commands'].pop()),
                           ('coverage',lambda r:[x.update(dependencies=[]) for x in r['depfiles']])]:
            bad=copy.deepcopy(value);change(bad)
            with self.subTest(key=key),self.assertRaises(ValueError):p.validate_audit(bad)

    def test_audit_license_and_redistribution_boundaries(self):
        for index,key,val in [(1,'selected_license','MIT'),(2,'selected_license','unknown'),(4,'redistributed',True),(0,'bytes',True)]:
            value=self.audit_fixture();value['files'][index][key]=val
            with self.subTest(index=index,key=key),self.assertRaises(ValueError):p.validate_audit(value)


if __name__=='__main__':unittest.main()
