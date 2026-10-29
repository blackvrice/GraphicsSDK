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
import llgl_vulkan_pso as v
import check_llgl_pso_profile as profile

class LLGLVulkanPSO(unittest.TestCase):
    def saved(self):
        path=v.ROOT/'benchmarks/reports/validation/2026-10-28-phase3-llgl-pso/verify.py';spec=importlib.util.spec_from_file_location('saved_llgl_pso',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

    def row(self,name='pso1',validation=False):
        name=name if name in ('core-control','sync-control') else name+('-validation' if validation else '-plain')
        return json.loads((self.saved().HERE/'native'/(name+'.stdout')).read_bytes())

    def check(self,row,name='pso1',validation=False,identity=None):
        receipt=self.saved().read(self.saved().HERE/'native/receipt.json')
        return v.validate(row,name,validation,receipt['source_fingerprint'],receipt['shader_header_sha256'],identity)

    def invalid(self,changes,name='pso1',validation=False):
        for key,value in changes.items():
            row=self.row(name,validation);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(row,name,validation)

    def test_all_scenarios_and_validation_modes(self):
        for name in v.WORK:
            for mode in (False,True):
                with self.subTest(name=name,mode=mode):self.check(self.row(name,mode),name,mode)

    def test_independent_references_match_native_full_readbacks(self):
        for name,count in v.WORK.items():
            row=self.row(name)
            for i in range(4):
                data=v.reference(i,count);self.assertEqual(len(data),9620);self.assertEqual(data,self.saved().frame(i,count));self.assertEqual(hashlib.sha256(data).hexdigest(),row['frame_sha256'][i])

    def test_uneven_eight_stripe_boundaries_and_outer_clear(self):
        image=v.reference(0,8);edges=[5,11,18,25,32,39,46,53,60]
        for i in range(8):
            for x in (edges[i],edges[i+1]-1):
                for y in (3,33):self.assertEqual(image[(65*y+x)*4:(65*y+x)*4+4],bytes((0,255,0,255) if i%2 else (255,0,0,255)))
        for x,y in ((0,0),(4,3),(60,33),(59,34),(64,36)):self.assertEqual(image[(65*y+x)*4:(65*y+x)*4+4],bytes((0,0,255,255)))

    def test_each_iteration_changes_pipeline_regions_and_clear(self):
        for count in (1,8):self.assertNotEqual(v.reference(0,count),v.reference(1,count))
        self.assertNotEqual(v.reference(0,1),v.reference(0,8))

    def test_requires_all_public_creates_uses_and_releases(self):
        self.invalid(dict(created_pipelines=31,released_pipeline_handles=31,used_pipeline_regions=31,pipelines_per_iteration=7),'pso8')

    def test_shader_creation_and_release_are_in_declared_create_boundary(self):
        self.invalid(dict(created_shader_handles=63,released_shader_handles=63,includes_shader_object_create_release=False),'pso8')

    def test_native_count_detects_visually_equivalent_missing_draw(self):
        self.invalid(dict(native_draw_calls=31,native_forwarded_draw_calls=31,native_instances=31),'pso8')

    def test_native_vertices_and_first_arguments(self):
        self.invalid(dict(native_vertices=95,native_bad_arguments=1,vertices_per_pipeline=4,first_vertex=1,first_instance=1),'pso8')

    def test_no_partial_or_unchanging_reference(self):
        self.invalid(dict(verified_images=3,verified_bytes=38479,frame_sha256=[self.row()['frame_sha256'][0]]*4,readback_verified=False))

    def test_geometry_and_public_mapping_must_match(self):
        self.invalid(dict(width=64,height=36,scissor=[6,3,55,31],clear_strategy='present_pass',texture_bind_policy='unknown'))

    def test_observer_must_be_active_and_teardown_complete(self):
        self.invalid(dict(observer_path='',runtime_modules=[],observer_live_instances_after_close=1,observer_live_devices_after_close=1))

    def test_instance_api_request_is_observed(self):
        self.invalid(dict(instance_API_request_observed=False,observed_instance_api_version=4198400))

    def test_actual_identity_and_queue_required(self):
        self.invalid(dict(device_uuid='0'*32,driver_uuid='0'*32,device_luid='0'*16,queue_flags=0,native_handle_verified=False,native_vendor_id=0,queue_index=1))

    def test_identity_stable_between_processes(self):
        identity=self.check(self.row());row=self.row();row['driver_uuid']='f'*32
        with self.assertRaises(ValueError):self.check(row,identity=identity)

    def test_numeric_counts_and_native_fields_reject_boolean(self):
        self.invalid({k:True for k in ('created_pipelines','released_pipeline_handles','used_pipeline_regions','created_shader_handles','released_shader_handles','native_device_id','native_driver_version','native_api_version','queue_family','queue_flags','loader_api_version','observed_instance_api_version')})

    def test_validation_is_required_and_normal_errors_zero(self):
        self.invalid(dict(loaded_validation_layer='unexpected',delivered_errors=1));self.invalid(dict(loaded_validation_layer=''),validation=True)

    def test_core_and_sync_diagnostics_are_delivered(self):
        for name in ('core-control','sync-control'):
            self.check(self.row(name,True),name,True);self.invalid(dict(control_diagnostic='unrelated',delivered_errors=0),name,True)

    def test_diagnostic_control_cannot_submit_or_count_workload(self):
        self.invalid(dict(submitted_control_work=1,created_pipelines=1,used_pipeline_regions=1,verified_images=4),'sync-control',True)

    def test_no_performance_or_native_workload_shortcut_acceptance(self):
        self.invalid(dict(performance_samples=1,samples_ms=[1],gpu_ms=1.0,performance_accepted=True,native_workload_shortcuts=True,GPU_use_in_timing=True))

    def test_no_physical_native_allocation_or_cache_claims(self):
        self.invalid(dict(GPU_memory_reclamation_verified=True,native_pipeline_count_verified=True,driver_cache_state_known=True,cache_state='cold_driver_cache'))

    def test_current_source_text_and_archived_native_bytes_match(self):
        # Runtime fingerprints stay byte-exact. Git may change only checkout EOLs
        # across platforms, so compare current text and fingerprint a separate
        # restoration of the executed bytes, without rewriting the checkout.
        receipt=self.saved().read(self.saved().HERE/'native/receipt.json');names=[p.relative_to(v.ROOT).as_posix() for p in v.source_files()];self.assertEqual(names,sorted(names))
        with zipfile.ZipFile(self.saved().HERE/'source.zip') as z,tempfile.TemporaryDirectory() as directory:
            restored=pathlib.Path(directory)
            for p,name in zip(v.source_files(),names):
                original=z.read(name);self.assertEqual(p.read_bytes().replace(b'\r\n',b'\n'),original.replace(b'\r\n',b'\n'))
                destination=restored/name;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(original)
            self.assertEqual(v.fingerprint(restored),receipt['source_fingerprint'])

    def test_full_saved_native_evidence_replay_without_gpu(self):self.assertEqual(self.saved().verify()['created_pipelines'],72)

    def test_saved_output_tampering_rejected(self):
        module=self.saved()
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(module.HERE,root);p=root/'native/pso8-validation.stdout';p.write_bytes(p.read_bytes()+b' ')
            with self.assertRaisesRegex(ValueError,'Evidence hash differs'):module.verify(root)

    def rebind(self,module,root,p):
        m=module.read(root/'manifest.json');entry=next(e for e in m['files'] if e['path']==p.relative_to(root).as_posix());entry.update(bytes=p.stat().st_size,sha256=module.sha(p.read_bytes()));(root/'manifest.json').write_bytes(json.dumps(m).encode())

    def test_rebound_release_count_still_rejected_by_independent_replay(self):
        module=self.saved()
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(module.HERE,root);p=root/'native/pso8-validation.stdout';row=module.read(p);row['released_pipeline_handles']=31;p.write_bytes(json.dumps(row).encode());self.rebind(module,root,p)
            with self.assertRaisesRegex(ValueError,'PSO scope/lifecycle'):module.verify(root)

    def test_rebound_source_zip_still_requires_executed_source(self):
        module=self.saved()
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(module.HERE,root);p=root/'source.zip'
            with zipfile.ZipFile(p) as z:data={n:z.read(n) for n in z.namelist()}
            data['benchmarks/llgl_vulkan_pso/main.cpp']+=b'\n// changed\n'
            with zipfile.ZipFile(p,'w') as z:
                for name,b in data.items():z.writestr(name,b)
            self.rebind(module,root,p)
            with self.assertRaisesRegex(ValueError,'Source ZIP differs'):module.verify(root)

    def test_extension_accepts_only_bound_pso_evidence(self):self.assertEqual(profile.validate(profile.read(profile.PROFILE))['verified_correctness'],['pso'])

    def test_extension_does_not_grant_performance_or_upstream_status(self):
        for key,value in [('performance_accepted',True),('unmodified_upstream',True),('observer_free_performance_required',False),('GPU_memory_reclamation_verified',True),('native_pipeline_count_verified',True),('driver_cache_state_known',True)]:
            plan=profile.read(profile.PROFILE);plan[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

    def test_extension_cannot_complete_allocation_or_drop_pending_gates(self):
        for key,value in [('verified_correctness',['pso','allocation']),('pending_tasks',[])]:
            plan=profile.read(profile.PROFILE);plan[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

    def test_extension_preserves_historical_contracts_and_receipt(self):
        for key in ('base_contract','prior_draw_contract','evidence'):
            plan=profile.read(profile.PROFILE);plan[key]['sha256']='0'*64
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(plan)

    def test_native_matrix_rejects_missing_release_and_wrong_pipeline(self):
        module=self.saved();jobs=module.read(module.HERE/'native/commands.json');self.assertEqual(len(jobs),16)
        for name in ('missing-pipeline','skip-release','wrong-pipeline','skip-region'):
            self.assertEqual(next(j['returncode'] for j in jobs if j['name']==name),1);self.assertIn(module.REJECT[name],(module.HERE/'native'/(name+'.stderr')).read_bytes())

if __name__=='__main__':unittest.main()
