import functools
import hashlib
import importlib.util
import json
import pathlib
import shutil
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import llgl_vulkan_allocation as v
import check_llgl_allocation_profile as profile

@functools.lru_cache(maxsize=1)
def saved():
    path=v.ROOT/'benchmarks/reports/validation/2026-10-29-phase3-llgl-allocation/verify.py';spec=importlib.util.spec_from_file_location('saved_llgl_allocation',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

class LLGLVulkanAllocation(unittest.TestCase):
    def row(self,name='buffer1',validation=False):
        name=name if name in ('core-control','sync-control') else name+('-validation' if validation else '-plain')
        return json.loads((saved().HERE/'native'/(name+'.stdout')).read_bytes())

    def check(self,row,name='buffer1',validation=False,identity=None):return v.validate(row,name,validation,saved().read(saved().HERE/'native/receipt.json')['source_fingerprint'],identity)

    def invalid(self,changes,name='buffer1',validation=False):
        for key,value in changes.items():
            row=self.row(name,validation);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row,name,validation)

    def test_all_resource_sizes_and_validation_modes(self):
        for name in v.WORK:
            for mode in (False,True):
                with self.subTest(name=name,mode=mode):self.check(self.row(name,mode),name,mode)

    def test_independent_reference_bytes_and_native_payload_hashes(self):
        for i,j in ((0,0),(1,1),(3,999)):
            self.assertEqual(v.reference(i,j),saved().payload(i,j));self.assertEqual(len(v.reference(i,j)),9620)
        for name in v.WORK:self.assertEqual(v.expected_hashes(name),self.row(name)['payload_sha256'])

    def test_payload_changes_with_iteration_resource_and_byte_offset(self):
        self.assertNotEqual(v.reference(0,0),v.reference(1,0));self.assertNotEqual(v.reference(0,0),v.reference(0,1));self.assertGreater(len(set(v.reference(3,999))),250)

    def test_last_resource_last_byte_is_part_of_full_reference(self):
        row=self.row('texture1000');data=bytearray(v.reference(3,999));data[-1]^=1;row['payload_sha256'][3][999]=hashlib.sha256(data).hexdigest()
        with self.assertRaises(ValueError):self.check(row,'texture1000')

    def test_all_public_creates_uses_releases_and_verifications_required(self):
        self.invalid(dict(created_resources=3999,released_resource_handles=3999,used_resources=3999,verified_resources=3999,resources_per_iteration=999),'buffer1000')

    def test_distinct_live_native_resource_identity_required(self):
        self.invalid(dict(unique_native_resources_checked=3999,native_resource_handles_verified=False),'texture1000')

    def test_buffer_and_texture_geometry_must_remain_exact(self):
        self.invalid(dict(buffer_bytes=4095,width=65,resource_kind='texture'))
        self.invalid(dict(width=64,height=36,texel_bytes=8,buffer_bytes=4096),'texture1')

    def test_all_full_bytes_and_resource_types_are_counted(self):
        self.invalid(dict(verified_bytes=16383,verified_buffers=3,verified_images=4,readback_verified=False))

    def test_payload_table_cannot_omit_resource_or_iteration(self):
        row=self.row('buffer1000');row['payload_sha256'][3].pop()
        with self.assertRaises(ValueError):self.check(row,'buffer1000')
        row=self.row();row['payload_sha256'].pop()
        with self.assertRaises(ValueError):self.check(row)

    def test_payload_table_cannot_reuse_another_resource_or_iteration(self):
        row=self.row('buffer1000');row['payload_sha256'][2][999]=row['payload_sha256'][2][998]
        with self.assertRaises(ValueError):self.check(row,'buffer1000')
        row=self.row();row['payload_sha256'][1]=row['payload_sha256'][0]
        with self.assertRaises(ValueError):self.check(row)

    def test_usage_policy_and_submission_difference_are_declared(self):
        self.invalid(dict(usage_policy='sdk_general_device_buffer',copy_submission_policy='one_command',initial_data_supplied=True,buffer_cpu_access_flags=1,allocation_policy='custom_pool'))

    def test_staging_and_gpu_use_cannot_enter_creation_release_boundary(self):
        self.invalid(dict(staging_in_create_release_boundary=True,GPU_use_in_timing=True,row_stride=256,layer_stride=10240))

    def test_observer_identity_and_teardown_are_required(self):
        self.invalid(dict(observer_path='',runtime_modules=[],observer_live_instances_after_close=1,observer_live_devices_after_close=1,instance_API_request_observed=False,observed_instance_api_version=4198400,native_draw_calls=1))

    def test_actual_native_identity_and_queue_are_required(self):
        self.invalid(dict(device_uuid='0'*32,driver_uuid='0'*32,device_luid='0'*16,native_vendor_id=0,native_handle_verified=False,queue_index=1,queue_flags=0))

    def test_native_identity_stays_stable_between_processes(self):
        identity=self.check(self.row());row=self.row();row['driver_uuid']='f'*32
        with self.assertRaises(ValueError):self.check(row,identity=identity)

    def test_count_and_native_numeric_fields_reject_boolean(self):
        self.invalid({k:True for k in ('created_resources','released_resource_handles','used_resources','verified_resources','unique_native_resources_checked','native_device_id','native_driver_version','native_api_version','queue_family','queue_flags','loader_api_version','observed_instance_api_version')})

    def test_validation_mode_and_zero_normal_errors(self):
        self.invalid(dict(delivered_errors=1,loaded_validation_layer='unexpected'));self.invalid(dict(loaded_validation_layer=''),validation=True)

    def test_core_and_sync_diagnostics_are_delivered(self):
        for name in ('core-control','sync-control'):
            self.check(self.row(name,True),name,True);self.invalid(dict(control_diagnostic='unrelated',delivered_errors=0),name,True)

    def test_diagnostic_control_cannot_submit_or_count_as_resource_work(self):
        self.invalid(dict(submitted_control_work=1,created_resources=1,verified_resources=1,verified_images=4,native_resource_handles_verified=True),'sync-control',True)

    def test_no_timing_or_native_workload_shortcut_acceptance(self):
        self.invalid(dict(performance_samples=1,samples_ms=[1],gpu_ms=1.0,performance_accepted=True,native_workload_shortcuts=True))

    def test_native_memory_allocation_reclamation_and_cache_remain_unverified(self):
        self.invalid(dict(GPU_memory_reclamation_verified=True,native_allocation_count_verified=True,allocator_cache_state_known=True))

    def test_current_source_text_and_archived_native_bytes_match(self):
        names=[p.relative_to(v.ROOT).as_posix() for p in v.source_files()];self.assertEqual(names,sorted(names));receipt=saved().read(saved().HERE/'native/receipt.json')
        with zipfile.ZipFile(saved().HERE/'source.zip') as z,tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)
            for p,name in zip(v.source_files(),names):
                data=z.read(name);self.assertEqual(p.read_bytes().replace(b'\r\n',b'\n'),data.replace(b'\r\n',b'\n'));q=root/name;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(data)
            self.assertEqual(v.fingerprint(root),receipt['source_fingerprint'])

    def test_full_saved_evidence_replays_without_gpu(self):self.assertEqual(saved().verify()['verified_resources'],16016)

    def test_saved_output_tampering_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(saved().HERE,root);p=root/'native/texture1000-validation.stdout';p.write_bytes(p.read_bytes()+b' ')
            with self.assertRaisesRegex(ValueError,'Evidence hash differs'):saved().verify(root)

    def rebind(self,root,p):
        manifest=saved().read(root/'manifest.json');entry=next(e for e in manifest['files'] if e['path']==p.relative_to(root).as_posix());entry.update(bytes=p.stat().st_size,sha256=saved().sha(p.read_bytes()));(root/'manifest.json').write_bytes(json.dumps(manifest).encode())

    def test_rebound_release_count_still_rejected_independently(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(saved().HERE,root);p=root/'native/texture1000-validation.stdout';row=saved().read(p);row['released_resource_handles']=3999;p.write_bytes(json.dumps(row).encode());self.rebind(root,p)
            with self.assertRaisesRegex(ValueError,'Resource scope/lifecycle'):saved().verify(root)

    def test_rebound_source_zip_still_requires_executed_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(saved().HERE,root);p=root/'source.zip'
            with zipfile.ZipFile(p) as z:data={n:z.read(n) for n in z.namelist()}
            data['benchmarks/llgl_vulkan_allocation/main.cpp']+=b'\n// changed\n'
            with zipfile.ZipFile(p,'w') as z:
                for name,b in data.items():z.writestr(name,b)
            self.rebind(root,p)
            with self.assertRaisesRegex(ValueError,'Source ZIP differs'):saved().verify(root)

    def test_explicit_profile_accepts_only_resource_correctness(self):self.assertEqual(profile.validate(profile.read(profile.PROFILE))['verified_correctness'],['allocation'])

    def test_profile_does_not_grant_performance_or_native_memory_claims(self):
        for key,value in [('performance_accepted',True),('unmodified_upstream',True),('observer_free_performance_required',False),('GPU_memory_reclamation_verified',True),('native_allocation_count_verified',True),('allocator_cache_state_known',True)]:
            plan=profile.read(profile.PROFILE);plan[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

    def test_profile_keeps_all_performance_gates(self):
        for key,value in [('verified_correctness',['allocation','performance']),('pending_tasks',[]),('staging_in_create_release_boundary',True),('copy_submission_policy','same_as_SDK')]:
            plan=profile.read(profile.PROFILE);plan[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

    def test_profile_preserves_prior_contracts_and_receipt(self):
        for key in ('base_contract','prior_pso_contract','evidence'):
            plan=profile.read(profile.PROFILE);plan[key]['sha256']='0'*64
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

    def test_matrix_contains_all_twelve_intended_rejections(self):
        jobs=saved().read(saved().HERE/'native/commands.json');self.assertEqual(len(jobs),22)
        for name,token in saved().REJECT.items():
            self.assertEqual(next(j['returncode'] for j in jobs if j['name']==name),1);self.assertIn(token,(saved().HERE/'native'/(name+'.stderr')).read_bytes())

if __name__=='__main__':unittest.main()
