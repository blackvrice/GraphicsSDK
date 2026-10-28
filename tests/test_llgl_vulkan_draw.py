import copy
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
import llgl_vulkan_draw as v
import check_llgl_draw_profile as profile

class LLGLVulkanDraw(unittest.TestCase):
    def row(self,name='draw1',validation=False):
        control=name in ('core-control','sync-control');calls,instances=(0,0) if control else v.WORK[name]
        return dict(schema_version=1,task='P3-CMP-04',sdk='llgl',api='vulkan',configuration='Release',profile_id='llgl-fixed-pin-transfer-src-v1',contract=v.CONTRACT,
            source_fingerprint='a'*64,shader_header_sha256='b'*64,scenario=name,validation=validation,control=control,native_handle_verified=True,native_vendor_id=4318,queue_index=0,
            instance_API_request_observed=True,clear_strategy='automatic_offscreen_pass_plus_full_attachment_clear',texture_bind_policy='color_attachment_sampled_copy_src',
            width=65,height=37,texel_bytes=4,scissor=[5,3,55,31],vertices_per_instance=3,first_vertex=0,first_instance=0,draw_calls_per_frame=calls,instances_per_call=instances,
            warmup=0 if control else 1,sample_count=0 if control else 3,verified_images=0 if control else 4,verified_bytes=0 if control else 38480,readback_verified=not control,
            native_draw_calls=calls*4,native_forwarded_draw_calls=calls*4,native_instances=calls*instances*4,native_vertices=calls*instances*12,native_bad_arguments=0,
            native_min_instances=instances,native_max_instances=instances,submitted_control_work=0,observer_live_instances_after_close=0,observer_live_devices_after_close=0,
            performance_samples=0,samples_ms=[],CPU_intervals_ms=[],gpu_ms=None,performance_accepted=False,native_workload_shortcuts=False,
            device_name='GPU',device_uuid='1'*32,driver_uuid='2'*32,device_luid='3'*16,native_device_id=8644,native_driver_version=1,native_api_version=4211045,
            queue_family=0,queue_flags=15,loader_api_version=4211045,observed_instance_api_version=4211045,loaded_validation_layer='layer' if validation else '',
            runtime_modules=['system','observer'],observer_path='observer',frame_sha256=[] if control else [hashlib.sha256(v.reference(i)).hexdigest() for i in range(4)],
            control_diagnostic=('VUID-VkPhysicalDeviceProperties2-sType-sType' if name=='core-control' else 'SYNC-HAZARD-WRITE-AFTER-WRITE') if control else '',delivered_errors=2 if control else 0)

    def check(self,row,name='draw1',validation=False,identity=None):return v.validate(row,name,validation,'a'*64,'b'*64,identity)

    def test_all_workloads_and_validation_modes(self):
        for name in v.WORK:
            for mode in (False,True):
                with self.subTest(name=name,mode=mode):self.check(self.row(name,mode),name,mode)

    def test_reference_covers_entire_image_and_scissor_edges(self):
        data=v.reference(0)
        self.assertEqual(len(data),9620)
        for x,y,rgba in [(0,0,(0,0,255,255)),(5,3,(255,0,0,255)),(59,33,(255,0,0,255)),(60,33,(0,0,255,255)),(59,34,(0,0,255,255)),(64,36,(0,0,255,255))]:
            with self.subTest(x=x,y=y):self.assertEqual(data[(y*65+x)*4:(y*65+x)*4+4],bytes(rgba))

    def test_reference_alternates_clear_and_triangle_colors(self):
        self.assertEqual(v.reference(0),v.reference(2));self.assertEqual(v.reference(1),v.reference(3));self.assertNotEqual(v.reference(0),v.reference(1))

    def test_wrong_backend_profile_or_source_rejected(self):
        for key,value in [('sdk','sdl'),('api','d3d12'),('profile_id','unmodified_llgl'),('source_fingerprint','c'*64),('shader_header_sha256','c'*64),('configuration','Debug')]:
            row=self.row();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_native_call_count_detects_visually_identical_missing_draw(self):
        row=self.row('draw1000');row['native_draw_calls']-=1
        with self.assertRaises(ValueError):self.check(row,'draw1000')

    def test_native_instance_count_detects_visually_identical_missing_instance(self):
        for key,value in [('native_instances',3996),('native_min_instances',999),('native_max_instances',999)]:
            row=self.row('instanced1000');row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row,'instanced1000')

    def test_observer_must_forward_actual_calls(self):
        row=self.row();row['native_forwarded_draw_calls']=0
        with self.assertRaises(ValueError):self.check(row)

    def test_native_vertices_and_arguments_verified(self):
        for key,value in [('native_vertices',11),('native_bad_arguments',1),('first_vertex',1),('first_instance',1),('vertices_per_instance',4)]:
            row=self.row();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_no_partial_or_unchanging_reference(self):
        for key,value in [('verified_images',3),('verified_bytes',38479),('frame_sha256',[hashlib.sha256(v.reference(0)).hexdigest()]*4),('readback_verified',False)]:
            row=self.row();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_exact_geometry_and_clear_policy_required(self):
        for key,value in [('width',64),('height',36),('scissor',[6,3,55,31]),('clear_strategy','swapchain_pass'),('texture_bind_policy','unknown')]:
            row=self.row();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_instance_request_must_be_actually_observed(self):
        row=self.row();row['instance_API_request_observed']=False
        with self.assertRaises(ValueError):self.check(row)
        row=self.row();row['observed_instance_api_version']=4198400
        with self.assertRaises(ValueError):self.check(row)

    def test_observer_retirement_and_module_required(self):
        for key,value in [('observer_path',''),('observer_live_instances_after_close',1),('observer_live_devices_after_close',1),('runtime_modules',[])]:
            row=self.row();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_actual_native_identity_and_queue_required(self):
        for key,value in [('device_uuid','0'*32),('driver_uuid','0'*32),('device_luid','0'*16),('queue_flags',0),('native_handle_verified',False),('native_vendor_id',0),('queue_index',1)]:
            row=self.row();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_identity_stable_between_processes(self):
        identity=self.check(self.row());row=self.row();row['driver_uuid']='f'*32
        with self.assertRaises(ValueError):self.check(row,identity=identity)

    def test_numeric_native_fields_reject_bool(self):
        for key in ('native_device_id','native_driver_version','native_api_version','queue_family','queue_flags','loader_api_version','observed_instance_api_version'):
            row=self.row();row[key]=True
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_validation_mode_and_delivered_errors(self):
        row=self.row();row['loaded_validation_layer']='layer'
        with self.assertRaises(ValueError):self.check(row)
        row=self.row(validation=True);row['loaded_validation_layer']=''
        with self.assertRaises(ValueError):self.check(row,validation=True)
        row=self.row();row['delivered_errors']=1
        with self.assertRaises(ValueError):self.check(row)

    def test_no_timing_or_native_shortcut_acceptance(self):
        for key,value in [('performance_samples',1),('samples_ms',[1]),('CPU_intervals_ms',[[1,2,3,4]]),('gpu_ms',1.0),('performance_accepted',True),('native_workload_shortcuts',True)]:
            row=self.row();row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row)

    def test_core_and_sync_diagnostics_required(self):
        for name in ('core-control','sync-control'):
            self.check(self.row(name,True),name,True)
            row=self.row(name,True);row['control_diagnostic']='unrelated'
            with self.subTest(name=name),self.assertRaises(ValueError):self.check(row,name,True)

    def test_controls_cannot_submit_or_count_as_workload(self):
        for key,value in [('submitted_control_work',1),('native_draw_calls',1),('verified_images',4),('delivered_errors',0)]:
            row=self.row('sync-control',True);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row,'sync-control',True)

    def test_fingerprint_uses_CMake_case_sensitive_order(self):
        names=[p.relative_to(v.ROOT).as_posix() for p in v.source_files()];self.assertEqual(names,sorted(names));self.assertIn('tests/fixtures/SpirvFixtures.hpp',names)

    def saved(self):
        path=v.ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-draw/verify.py';spec=importlib.util.spec_from_file_location('saved_llgl_draw',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

    def test_saved_native_draw_observations_replay_without_GPU(self):
        self.assertEqual(self.saved().verify()['native_draw_calls'],8016)

    def test_saved_output_tampering_rejected(self):
        module=self.saved()
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(module.HERE,root);p=root/'native/instanced1000-validation.stdout';p.write_bytes(p.read_bytes()+b' ')
            with self.assertRaisesRegex(ValueError,'Evidence hash differs'):module.verify(root)

    def test_rebound_source_zip_still_requires_original_fingerprint(self):
        module=self.saved()
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(module.HERE,root);p=root/'source.zip'
            with zipfile.ZipFile(p) as z:data={n:z.read(n) for n in z.namelist()}
            data['benchmarks/llgl_vulkan_draw/main.cpp']+=b'\n// changed source\n'
            with zipfile.ZipFile(p,'w') as z:
                for n,b in data.items():z.writestr(n,b)
            manifest=module.read(root/'manifest.json');entry=next(e for e in manifest['files'] if e['path']=='source.zip');entry.update(bytes=p.stat().st_size,sha256=module.sha(p.read_bytes()));(root/'manifest.json').write_bytes(json.dumps(manifest).encode())
            with self.assertRaises(ValueError):module.verify(root)

    def test_observer_manifest_uses_loader_compatible_string_version(self):
        layer=self.saved().read(self.saved().HERE/'build/VkLayer_gfx_llgl_draw.json')['layer'];self.assertEqual(layer['implementation_version'],'1');self.assertEqual(layer['name'],'VK_LAYER_GFX_llgl_draw')

    def test_explicit_draw_extension_accepts_bound_native_evidence(self):
        self.assertEqual(profile.validate(profile.read(profile.PROFILE))['verified_correctness'],['draw'])

    def test_extension_cannot_claim_performance_or_unmodified_upstream(self):
        for key,value in [('performance_accepted',True),('unmodified_upstream',True),('observer_free_performance_required',False)]:
            plan=profile.read(profile.PROFILE);plan[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

    def test_extension_cannot_complete_other_correctness_or_drop_gates(self):
        for key,value in [('verified_correctness',['draw','pso']),('pending_tasks',[])]:
            plan=profile.read(profile.PROFILE);plan[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

    def test_extension_requires_unchanged_base_contract_and_receipt(self):
        for key in ('base_contract','evidence'):
            plan=profile.read(profile.PROFILE);plan[key]['sha256']='0'*64
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

if __name__=='__main__':unittest.main()
