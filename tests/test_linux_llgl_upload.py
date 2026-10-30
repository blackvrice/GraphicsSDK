"""Linux upload port and diagnostic scope contracts; fixtures are not native evidence."""
import copy
import hashlib
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_llgl_upload as u
import port_linux_llgl_upload as port


class UploadPort(unittest.TestCase):
    def test_preserve_entire_upload_and_validation_control_bodies(self):
        source=(u.ROOT/port.OWN[0]).read_text(encoding='utf-8');ctx=(u.ROOT/port.OWN[1]).read_text(encoding='utf-8')
        main,platform,changes=port.translate(source,ctx)
        original=source[source.index('std::string validationControl('):source.index('int main(')]
        self.assertEqual(original,main[main.index('std::string validationControl('):main.index('int main(')])
        self.assertNotIn('BCrypt',main);self.assertNotIn('GetModuleHandle',platform)
        self.assertIn('SHA256(',main);self.assertIn('"null"',platform)
        self.assertLess(platform.index('selected=linuxbench::select'),platform.index('RenderSystem::Load'))
        self.assertIn('actual renderer identity',changes)

    def test_unknown_or_duplicate_platform_tokens_fail_closed(self):
        source=(u.ROOT/port.OWN[0]).read_text(encoding='utf-8');ctx=(u.ROOT/port.OWN[1]).read_text(encoding='utf-8')
        for a,b in [(source+source,ctx),(source,ctx.replace('desc.flags=LLGL::RenderSystemFlags::PreferNVIDIA;','other;'))]:
            with self.assertRaises(ValueError):port.translate(a,b)

    def test_generation_is_deterministic_and_source_read_only(self):
        before=[u.sha(u.ROOT/n) for n in port.OWN]
        with tempfile.TemporaryDirectory(dir=u.ROOT/'out') as td:
            a=port.generate(pathlib.Path(td));b=port.generate(pathlib.Path(td));self.assertEqual(a,b)
            self.assertFalse(a['workload_bodies_changed'])
        self.assertEqual(before,[u.sha(u.ROOT/n) for n in port.OWN])
        with self.assertRaises(ValueError):port.generate(u.ROOT/'invalid-output')


class UploadContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.probe=u.read(u.ROOT/'benchmarks/reports/validation/2026-10-30-phase3-linux-llgl-probe/native/receipt.json')

    def setUp(self):
        self.r=dict(probe=copy.deepcopy(self.probe),adaptation=dict(source_fingerprint='a'*64),reference=u.read(u.ROOT/u.REFERENCE),
            binary=dict(path='/build/linux_llgl_upload'),crypto=dict(path='/usr/lib/libcrypto.so.3'),output_directory='/out/matrix')

    def result(self,job):
        a=self.r['probe']['qualification'];d=a['selected'];native=next(x['result'] for x in self.r['probe']['runs'] if x['job']['kind']=='normal')
        control=job['kind'] in u.TOKENS;s=[0,0,0] if control else u.SHAPES[job['scenario']]
        modules={x['path'] for x in a['runtime_libraries']}|{self.r['probe']['binaries'][n]['path'] for n in ('core','backend')}|{self.r['crypto']['path']}
        if job['validation']:modules.add(a['validation_library'])
        return dict(schema_version=1,task='P3-LBEN-01-LLGL-readiness',sdk='llgl',api='vulkan',configuration='Release',
            contract='vulkan_upload_cpu_payload_to_texture_fence_complete_each_sample_readback_v1',source_fingerprint='a'*64,
            LLGL_commit=self.r['probe']['LLGL_commit'],dependency_profile=u.p.PROFILE,unmodified_upstream=False,selected=copy.deepcopy(d),environment=copy.deepcopy(a['environment']),
            mode='software-diagnostic',device_name=d['name'],device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],
            native_handle_verified=True,queue_family=native['queue_family'],queue_index=0,queue_flags=native['queue_flags'],loader_api_version=native['loader_api_version'],instance_API_request_observed=False,
            loader_path=a['loader_path'],LLGL_path=self.r['probe']['binaries']['core']['path'],backend_path=self.r['probe']['binaries']['backend']['path'],loaded_validation_layer=a['validation_library'] if job['validation'] else '',
            validation=job['validation'],scenario=job['scenario'],control=control,control_diagnostic=u.TOKENS[job['kind']] if control else '',submitted_control_work=0,
            delivered_errors=1 if control else 0,width=s[0],height=s[1],texel_bytes=s[2],warmup=0 if control else 1,sample_count=0 if control else 3,
            verified_images=0 if control else 4,verified_bytes=0 if control else 4*s[0]*s[1]*s[2],readback_verified=not control,row_stride=0,layer_stride=0,
            payload_sha256=[] if control else copy.deepcopy(self.r['reference']['hashes'][job['scenario']]),performance_samples=0,samples_ms=[],gpu_ms=None,
            physical_GPU_accepted=False,P3_LBEN_01_completed=False,performance_accepted=False,native_workload_shortcuts=False,runtime_modules=sorted(modules))

    def test_all_four_cases_and_two_error_controls(self):
        for job in u.jobs():
            if job['kind']=='normal' or job['kind'] in u.TOKENS:
                with self.subTest(job=job):u.validate_result(self.result(job),job,self.r)

    def test_complete_case_counts_and_reference_bytes(self):
        jobs=u.jobs();self.assertEqual(len(jobs),15);self.assertEqual(len({j['name'] for j in jobs}),15)
        normal=[self.result(j) for j in jobs if j['kind']=='normal']
        self.assertEqual(sum(x['verified_images'] for x in normal),32)
        self.assertEqual(sum(x['verified_bytes'] for x in normal),862694560)
        self.assertTrue(all(len(x['payload_sha256'])==4 for x in normal))

    def test_full_hash_payload_shape_and_all_images_required(self):
        for job in u.jobs()[:8]:
            for key,val in [('width',0),('verified_images',3),('verified_bytes',1),('payload_sha256',[]),('readback_verified',False),('row_stride',256)]:
                value=self.result(job);value[key]=val
                with self.subTest(job=job,key=key),self.assertRaises(ValueError):u.validate_result(value,job,self.r)

    def test_reference_hash_cannot_be_stale_or_permuted(self):
        job=u.jobs()[0];value=self.result(job);value['payload_sha256'].reverse()
        with self.assertRaises(ValueError):u.validate_result(value,job,self.r)

    def test_scope_cannot_be_promoted_to_hardware_or_performance(self):
        job=u.jobs()[0]
        for key,val in [('unmodified_upstream',True),('physical_GPU_accepted',True),('P3_LBEN_01_completed',True),('performance_accepted',True),('performance_samples',False),('samples_ms',[1]),('gpu_ms',1),('native_workload_shortcuts',True)]:
            value=self.result(job);value[key]=val
            with self.subTest(key=key),self.assertRaises(ValueError):u.validate_result(value,job,self.r)

    def test_target_queue_source_and_native_handle_must_match(self):
        job=u.jobs()[0]
        for key,val in [('device_uuid','0'*32),('driver_uuid','0'*32),('device_luid','fake'),('source_fingerprint','b'*64),('native_handle_verified',False),('queue_index',1),('queue_flags',True)]:
            value=self.result(job);value[key]=val
            with self.subTest(key=key),self.assertRaises(ValueError):u.validate_result(value,job,self.r)
        value=self.result(job);value['selected']['device_id']=False
        with self.assertRaises(ValueError):u.validate_result(value,job,self.r)

    def test_actual_system_crypto_and_layer_load_required(self):
        for job in u.jobs()[:8]:
            value=self.result(job);value['runtime_modules'].remove(self.r['crypto']['path'])
            with self.subTest(job=job),self.assertRaises(ValueError):u.validate_result(value,job,self.r)
        job=u.jobs()[0];value=self.result(job);value['runtime_modules']=sorted(value['runtime_modules']+[self.r['probe']['qualification']['validation_library']])
        with self.assertRaises(ValueError):u.validate_result(value,job,self.r)

    def test_unknown_runtime_and_duplicate_modules_rejected(self):
        job=u.jobs()[0]
        for name in ('/tmp/extra.so',self.r['crypto']['path']):
            value=self.result(job);value['runtime_modules']=sorted(value['runtime_modules']+[name])
            with self.assertRaises(ValueError):u.validate_result(value,job,self.r)

    def test_errors_and_control_must_match_expected_native_diagnostic(self):
        for job in u.jobs()[8:10]:
            for key,val in [('control_diagnostic','other'),('delivered_errors',0),('submitted_control_work',1),('verified_images',1),('payload_sha256',['f'*64])]:
                value=self.result(job);value[key]=val
                with self.subTest(job=job,key=key),self.assertRaises(ValueError):u.validate_result(value,job,self.r)
        job=u.jobs()[0];value=self.result(job);value['delivered_errors']=1
        with self.assertRaises(ValueError):u.validate_result(value,job,self.r)

    def test_all_five_rejections_require_intended_exit_and_message(self):
        messages={'missing-layer':b'Required comparison validation layer absent','wrong-uuid':b'Exact UUID absent or ambiguous','wrong-kind':b'Software diagnostics only','corrupt-readback':b'Full changing upload readback mismatch','sync-disabled':b'Expected validation control diagnostic absent'}
        for job in u.jobs()[-5:]:
            token=messages[job['kind']];u.rejected(job,1,b'',token)
            for code,out,err in [(0,b'',token),(1,b'{}',token),(1,b'',b'unrelated')]:
                with self.subTest(job=job),self.assertRaises(ValueError):u.rejected(job,code,out,err)

    def test_sync_disabled_control_has_separate_settings(self):
        job=u.jobs()[-1];env=u.environment(job,self.r)
        self.assertEqual(env['VK_KHRONOS_VALIDATION_VALIDATE_SYNC'],'false')
        self.assertTrue(env['VK_LAYER_SETTINGS_PATH'].replace('\\','/').endswith('/sync-off'))
        self.assertIn('validate_sync = false',u.OFF)

    def audit_fixture(self):
        categories=['own','generated','llgl','vulkan_headers','system'];files=[]
        for i,category in enumerate(categories):
            value=dict(path='/fixture/'+str(i),bytes=1,sha256='a'*64,component=category)
            if category=='system':value['redistributed']=False
            else:value['selected_license']='BSD-3-Clause' if category=='llgl' else 'MIT'
            files.append(value)
        raw='file.o: '+' '.join(x['path'] for x in files)+'\n'
        return dict(files=files,actual_verbose_commands_verified=True,
            compile_commands=[dict(command='g++ -O2 -DNDEBUG -c /fixture/0 -o file.o',executed_command='g++ -O2 -DNDEBUG -MD -MT file.o -MF file.d -c /fixture/0 -o file.o')],
            depfiles=[dict(raw=raw,bytes=len(raw.encode()),sha256=hashlib.sha256(raw.encode()).hexdigest(),dependencies=[x['path'] for x in files])])

    def test_actual_single_consumer_command_and_gnu_dependencies(self):
        value=self.audit_fixture();u.validate_audit(value)
        for key,change in [('raw',lambda x:x['depfiles'][0].update(raw='tampered')),
                           ('command',lambda x:x['compile_commands'][0].update(executed_command=x['compile_commands'][0]['executed_command']+' -g')),
                           ('unit',lambda x:x['compile_commands'].append(x['compile_commands'][0])),
                           ('coverage',lambda x:x['depfiles'][0].update(dependencies=[])),
                           ('verbose',lambda x:x.update(actual_verbose_commands_verified=False))]:
            bad=copy.deepcopy(value);change(bad)
            with self.subTest(key=key),self.assertRaises(ValueError):u.validate_audit(bad)

    def test_compiler_license_and_system_binary_boundary(self):
        for index,key,val in [(2,'selected_license','MIT'),(4,'redistributed',True),(0,'bytes',True)]:
            value=self.audit_fixture();value['files'][index][key]=val
            with self.subTest(index=index),self.assertRaises(ValueError):u.validate_audit(value)


if __name__=='__main__':unittest.main()
