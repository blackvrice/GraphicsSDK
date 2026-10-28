import copy
import hashlib
import importlib.util
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import llgl_vulkan_upload as v
import check_llgl_transfer_profile as profile
from prepare_llgl_transfer_src_candidate import patch


class LLGLVulkanUpload(unittest.TestCase):
    def setUp(self):
        self.hashes={n:[str(i)*64 for i in range(1,5)] for n in v.SHAPES}
        self.row=dict(schema_version=1,task='P3-CMP-03',sdk='llgl',api='vulkan',configuration='Release',contract=v.CONTRACT,
            source_fingerprint='a'*64,LLGL_commit=v.PIN,dependency_profile='upstream',validation=False,scenario='odd',control=False,native_handle_verified=True,
            device_name='GPU',device_uuid='1'*32,driver_uuid='2'*32,device_luid='3'*16,native_vendor_id=4318,native_device_id=8644,
            native_driver_version=1,native_api_version=4211045,queue_family=0,queue_index=0,queue_flags=15,loader_api_version=4211045,
            instance_API_request_observed=False,submitted_control_work=0,row_stride=0,layer_stride=0,performance_samples=0,samples_ms=[],gpu_ms=None,
            performance_accepted=False,native_workload_shortcuts=False,loaded_validation_layer='',runtime_modules=['system'],
            width=65,height=37,texel_bytes=4,warmup=1,sample_count=3,verified_images=4,verified_bytes=38480,readback_verified=True,
            payload_sha256=self.hashes['odd'],delivered_errors=0,control_diagnostic='')

    def check(self,row=None,name='odd',validation=False,identity=None):
        return v.validate(self.row if row is None else row,name,validation,'a'*64,self.hashes,identity)

    def test_accepts_all_four_full_changing_workloads(self):
        for name,(w,h,bpp) in v.SHAPES.items():
            row=copy.deepcopy(self.row);row.update(scenario=name,width=w,height=h,texel_bytes=bpp,verified_bytes=w*h*bpp*4,payload_sha256=self.hashes[name])
            with self.subTest(name=name):self.check(row,name)

    def test_payload_body_matches_existing_SDK_and_SDL(self):
        v.verify_payload_source()

    def test_byte_exact_reference_RGBA8_first_pixel(self):
        self.assertEqual(v.payload_reference((1,1,4),1),hashlib.sha256(bytes.fromhex('86e7379e')).hexdigest())

    def test_byte_exact_reference_RGBA16F_first_pixel(self):
        self.assertEqual(v.payload_reference((1,1,8),1),hashlib.sha256(bytes.fromhex('004200000042003c')).hexdigest())

    def test_reference_changes_with_seed_and_includes_all_pixels(self):
        self.assertNotEqual(v.payload_reference((65,37,4),1),v.payload_reference((65,37,4),2))
        self.assertNotEqual(v.payload_reference((65,37,4),1),v.payload_reference((65,36,4),1))

    def test_fingerprint_orders_paths_like_CMake(self):
        files=v.source_files()
        names=[p.relative_to(v.ROOT).as_posix() for p in files]
        self.assertEqual(names,sorted(names))
        self.assertLess(names.index('benchmarks/llgl_vulkan_upload/Payload.hpp'),names.index('benchmarks/llgl_vulkan_upload/main.cpp'))

    def test_wrong_sdk_backend_or_pinned_source_rejected(self):
        for key,value in [('sdk','sdl'),('api','d3d12'),('LLGL_commit','0'*40),('source_fingerprint','b'*64),('configuration','Debug')]:
            row=copy.deepcopy(self.row);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_partial_readback_or_missing_iteration_rejected(self):
        for key,value in [('verified_images',3),('verified_bytes',38479),('sample_count',2),('warmup',0),('readback_verified',False),('verified_images',True)]:
            row=copy.deepcopy(self.row);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_unchanging_or_modified_payload_hash_rejected(self):
        for hashes in ([self.hashes['odd'][0]]*4, self.hashes['odd'][:3], ['f'*64]*4):
            row=copy.deepcopy(self.row);row['payload_sha256']=hashes
            with self.subTest(hashes=hashes),self.assertRaises(ValueError):self.check(row)

    def test_wrong_format_or_shape_rejected(self):
        for key,value in [('width',64),('height',38),('texel_bytes',8),('row_stride',256),('layer_stride',37)]:
            row=copy.deepcopy(self.row);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_all_native_identifiers_required(self):
        for key in ('device_uuid','driver_uuid','device_luid'):
            row=copy.deepcopy(self.row);row[key]='0'*len(row[key])
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_each_process_must_match_native_identity(self):
        identity=self.check()
        row=copy.deepcopy(self.row);row['driver_uuid']='f'*32
        with self.assertRaises(ValueError):self.check(row,identity=identity)

    def test_native_handles_cannot_be_inferred_from_name(self):
        self.row['native_handle_verified']=False
        with self.assertRaises(ValueError):self.check()

    def test_wrong_or_missing_layer_mode_rejected(self):
        row=copy.deepcopy(self.row);row['loaded_validation_layer']='layer'
        with self.assertRaises(ValueError):self.check(row)
        row=copy.deepcopy(self.row);row['validation']=True
        with self.assertRaises(ValueError):self.check(row,validation=True)

    def test_delivered_errors_in_normal_upload_rejected(self):
        self.row['delivered_errors']=1
        with self.assertRaises(ValueError):self.check()

    def test_native_workload_shortcut_or_performance_claim_rejected(self):
        for key,value in [('native_workload_shortcuts',True),('performance_accepted',True),('performance_samples',1),('samples_ms',[1]),('gpu_ms',1.0),('instance_API_request_observed',True)]:
            row=copy.deepcopy(self.row);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_modified_candidate_cannot_pass_as_unmodified_upstream(self):
        self.row['dependency_profile']='transfer-src-candidate'
        with self.assertRaises(ValueError):self.check()
        v.validate(self.row,'odd',False,'a'*64,self.hashes,profile='transfer-src-candidate')

    def test_native_numeric_identity_fields_reject_booleans(self):
        for key in ('native_device_id','native_driver_version','native_api_version','queue_family','queue_flags','loader_api_version'):
            row=copy.deepcopy(self.row);row[key]=True
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def control(self,name):
        row=copy.deepcopy(self.row);row.update(scenario=name,control=True,validation=True,loaded_validation_layer='layer',width=0,height=0,texel_bytes=0,
            warmup=0,sample_count=0,verified_images=0,verified_bytes=0,readback_verified=False,payload_sha256=[],delivered_errors=2,
            control_diagnostic='VUID-VkPhysicalDeviceProperties2-sType-sType' if name=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE')
        return row

    def test_core_and_sync_controls_require_specific_diagnostic(self):
        for name in ('core-control','sync-control'):
            row=self.control(name);self.check(row,name,True)
            row['control_diagnostic']='unrelated error'
            with self.subTest(name=name),self.assertRaises(ValueError):self.check(row,name,True)

    def test_controls_must_not_submit_GPU_work(self):
        row=self.control('sync-control');row['submitted_control_work']=1
        with self.assertRaises(ValueError):self.check(row,'sync-control',True)

    def test_zero_error_control_rejected(self):
        row=self.control('core-control');row['delivered_errors']=0
        with self.assertRaises(ValueError):self.check(row,'core-control',True)

    def test_controls_cannot_count_as_verified_images(self):
        row=self.control('sync-control');row['verified_images']=4
        with self.assertRaises(ValueError):self.check(row,'sync-control',True)

    def saved(self):
        path=v.ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-upload/verify.py'
        spec=importlib.util.spec_from_file_location('saved_llgl_upload',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

    def test_saved_original_failure_and_modified_success_replay(self):
        self.assertEqual(self.saved().verify()['verified_images'],32)

    def test_saved_native_output_tamper_rejected(self):
        module=self.saved()
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(module.HERE,root)
            path=root/'candidate/odd-validation.stdout';path.write_bytes(path.read_bytes()+b' ')
            with self.assertRaisesRegex(ValueError,'Evidence hash differs'):module.verify(root)

    def test_exact_reviewed_patch_and_upstream_guard(self):
        root=self.saved().HERE/'patch'
        original=(root/'VKCommandContext.original.cpp').read_text();changed=(root/'VKCommandContext.cpp').read_text()
        self.assertEqual(patch(original),changed)
        with self.assertRaises(ValueError):patch('upstream drift')

    def test_human_approved_profile_accepts_only_upload_correctness(self):
        self.assertEqual(profile.validate(profile.read(profile.PROFILE))['verified_correctness'],['upload'])

    def test_approved_patch_cannot_be_labeled_upstream_or_performance(self):
        for key in ('unmodified_upstream','performance_accepted','unmodified_upstream_correctness_accepted'):
            plan=profile.read(profile.PROFILE);plan[key]=True
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

    def test_approved_patch_does_not_complete_other_workloads(self):
        plan=profile.read(profile.PROFILE);plan['verified_correctness'].append('draw')
        with self.assertRaises(ValueError):profile.validate(plan)

    def test_approved_profile_rejects_changed_patch_evidence(self):
        plan=profile.read(profile.PROFILE);plan['evidence']['patch']['sha256']='0'*64
        with self.assertRaises(ValueError):profile.validate(plan)

    def test_profile_requires_actual_human_scope_choice(self):
        plan=profile.read(profile.PROFILE);plan['human_decision']=''
        with self.assertRaises(ValueError):profile.validate(plan)


if __name__=='__main__':unittest.main()
