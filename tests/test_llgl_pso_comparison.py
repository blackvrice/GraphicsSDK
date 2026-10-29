import copy
import functools
import importlib.util
import json
import math
import pathlib
import shutil
import sys
import tempfile
import unittest
import zipfile
from unittest import mock

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import compare_llgl_pso as v
import check_llgl_pso_comparison as profile

@functools.lru_cache(maxsize=1)
def saved():
    path=v.ROOT/'benchmarks/reports/validation/2026-10-29-phase3-three-sdk-pso/verify.py';spec=importlib.util.spec_from_file_location('saved_three_sdk_pso',path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

class ThreeSDKPSO(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.folder=saved().HERE/'native';cls.original=v.read(cls.folder/'receipt.json')

    def rejected(self,change):
        r=copy.deepcopy(self.original);change(r);original_reader=v.read
        def reader(p):return r if pathlib.Path(p)==self.folder/'receipt.json' else original_reader(p)
        with mock.patch.object(v,'read',side_effect=reader),self.assertRaises((ValueError,KeyError)):v.check(self.folder)

    def test_all_58_raw_processes_and_statistics_reproduce(self):self.assertEqual(v.check(self.folder)['measurement_samples'],900)

    def test_independent_replay_does_not_execute_zip_or_gpu(self):self.assertEqual(saved().verify()['verified_images'],1082)

    def test_validation_and_controls_precede_measurement(self):
        jobs=v.schedule(v.validate_plan(v.read(v.PLAN)));self.assertEqual(len(jobs),58);self.assertEqual(len([x for x in jobs[:28] if x['kind']=='validation']),6);self.assertTrue(all(j['kind']=='measurement' for j in jobs[28:]));self.assertFalse(any(j['kind']=='measurement' for j in jobs[:28]))

    def test_three_adjacent_sdks_and_five_distinct_orders_per_scenario(self):
        jobs=v.schedule(v.read(v.PLAN))[28:]
        for name in v.SHAPES:
            orders=[]
            for rep in range(5):
                selected=[j for j in jobs if j['scenario']==name and j['repetition']==rep];self.assertEqual({x['sdk'] for x in selected},set(v.SDKS));orders.append(tuple(x['sdk'] for x in selected))
            self.assertEqual(len(set(orders)),5)

    def test_seeded_schedule_matches_independent_implementation(self):self.assertEqual(v.schedule(v.read(v.PLAN)),saved().jobs())

    def test_plan_cannot_reduce_work_or_accept_gpu_timestamp(self):
        for change in (lambda p:p['performance'].update(samples=3),lambda p:p['validation'].update(required_core_and_sync=False),lambda p:p['timing'].update(GPU_timestamp_measurement=True),lambda p:p.update(historical_timings_reused=True)):
            p=v.read(v.PLAN);change(p)
            with self.assertRaises(ValueError):v.validate_plan(p)

    def test_statistics_reject_changed_p50_or_missing_process(self):
        self.rejected(lambda r:r['groups'][0]['statistics'].update(p50_ms=999));self.rejected(lambda r:r['runs'].pop())

    def test_schedule_rejects_order_changes_and_extra_process(self):
        self.rejected(lambda r:r['runs'].__setitem__(slice(28,30),r['runs'][28:30][::-1]));self.rejected(lambda r:r['runs'].append(copy.deepcopy(r['runs'][-1])))

    def test_validation_cannot_be_injected_into_performance(self):self.rejected(lambda r:r['runs'][28]['overrides'].update(VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'))

    def test_observer_cannot_be_injected_into_performance(self):self.rejected(lambda r:r['runs'][28]['overrides'].update(VK_INSTANCE_LAYERS='VK_LAYER_GFX_llgl_draw'))

    def test_no_overlapping_process_intervals(self):self.rejected(lambda r:r['runs'][1].update(started=r['runs'][0]['started']))

    def test_control_cannot_succeed_or_produce_samples(self):
        self.rejected(lambda r:r['runs'][13].update(returncode=0))
        for sdk,message in [('graphicssdk','Unique exact SDK GPU required'),('sdl','Selected GPU differs from expected GPU')]:
            job=dict(sdk=sdk,kind='wrong-target');v.validate_rejection(job,1,b'',('FAIL: '+message+'\n').encode())
            with self.assertRaises(ValueError):v.validate_rejection(job,1,b'',b'FAIL: Unrelated startup failure\n')

    def test_changed_luid_or_driver_is_rejected(self):
        self.rejected(lambda r:r['identity'].update(device_luid='0100000000000000'));self.rejected(lambda r:r['identity'].update(driver_uuid='0'*32))

    def test_readback_totals_cannot_omit_validation_or_warmup(self):
        self.rejected(lambda r:r.update(verified_images=2100));self.rejected(lambda r:r.update(verified_bytes=r['verified_bytes']-9620))

    def test_parsed_samples_must_match_raw_output(self):self.rejected(lambda r:r['runs'][28]['result'].update(samples_ms=[999]*30))

    def test_historical_samples_or_overall_ranking_cannot_be_promoted(self):
        self.rejected(lambda r:r.update(historical_timings_reused=True));self.rejected(lambda r:r.update(overall_SDK_ranking=True));self.rejected(lambda r:r.update(SDL_native_handle_verified=True))

    def test_five_process_statistics_preserve_all_outliers(self):
        values=[[float(i+1) for i in range(30)] for _ in range(5)];values[-1][-1]=10000;self.assertEqual(saved().stats(values),v.protocol.scalar_stats(values));self.assertEqual(saved().stats(values)['max_ms'],10000);self.assertEqual(saved().stats(values)['samples_removed'],0)

    def test_independent_statistics_reject_nan_infinity_boolean_and_negative(self):
        for value in (float('nan'),float('inf'),True,-1):
            rows=[[1.0]*30 for _ in range(5)];rows[-1][-1]=value
            with self.subTest(value=value),self.assertRaises(ValueError):saved().stats(rows)

    def test_all_validation_rows_use_current_identity_and_exact_work(self):
        for item in self.original['runs']:
            if item['job']['kind']=='validation':self.assertEqual(v.validate_output(item['job'],item['result'],self.original),self.original['identity'])

    def test_llgl_observer_and_validation_modules_absent_in_measurements(self):
        for run in self.original['runs']:
            if run['job']['kind']=='measurement' and run['job']['sdk']=='llgl':
                row=copy.deepcopy(run['result']);row['runtime_modules'].append('VkLayer_gfx_llgl_draw.dll')
                with self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)
                row=copy.deepcopy(run['result']);row['runtime_modules'].append('VkLayer_khronos_validation.dll')
                with self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)

    def test_actual_llgl_observation_is_separate_from_timing(self):
        row=self.original['runs'][3]['result'];self.assertTrue(row['observer_enabled']);self.assertEqual(row['observed_instance_api_version'],4211045);self.assertEqual(row['samples_ms'],[])
        for run in self.original['runs'][28:]:
            if run['job']['sdk']=='llgl':self.assertFalse(run['result']['observer_enabled']);self.assertEqual(run['result']['observed_instance_api_version'],0);self.assertIsNone(run['result']['native_draw_calls'])

    def test_llgl_full_changing_reference_cannot_be_truncated(self):
        run=next(x for x in self.original['runs'] if x['job']['sdk']=='llgl' and x['job']['kind']=='validation');row=copy.deepcopy(run['result']);row['frame_sha256'].pop()
        with self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)

    def test_llgl_raw_fingerprint_and_device_metadata_are_bound(self):
        run=next(x for x in self.original['runs'] if x['job']['sdk']=='llgl' and x['job']['kind']=='measurement')
        for key,value in [('source_fingerprint','0'*64),('native_handle_verified',False),('native_vendor_id',0),('device_uuid','0'*32),('verified_bytes',0),('gpu_ms',1.0)]:
            row=copy.deepcopy(run['result']);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)

    def test_current_source_text_and_original_native_bytes_match(self):
        with zipfile.ZipFile(saved().HERE/'source.zip') as z,tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)
            for p in v.source_files():
                name=p.relative_to(v.ROOT).as_posix();raw=z.read(name);self.assertEqual(p.read_bytes().replace(b'\r\n',b'\n'),raw.replace(b'\r\n',b'\n'));q=root/name;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(raw)
            self.assertEqual(v.fingerprint(root),self.original['LLGL_pso_fingerprint'])

    def test_saved_output_tampering_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(saved().HERE,root);name=self.original['runs'][28]['stdout'];p=root/'native'/name;p.write_bytes(p.read_bytes()+b' ')
            with self.assertRaisesRegex(ValueError,'Evidence hash differs'):saved().verify(root)

    def test_rebound_statistic_still_fails_independent_replay(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(saved().HERE,root);p=root/'native/receipt.json';r=saved().read(p);r['groups'][0]['statistics']['p50_ms']=999;p.write_bytes(json.dumps(r).encode());manifest=saved().read(root/'manifest.json');item=next(i for i in manifest['files'] if i['path']=='native/receipt.json');item.update(bytes=p.stat().st_size,sha256=saved().sha(p.read_bytes()));(root/'manifest.json').write_bytes(json.dumps(manifest).encode())
            with self.assertRaisesRegex(ValueError,'statistics'):saved().verify(root)

    def test_duplicate_json_keys_and_unsafe_paths_rejected(self):
        with self.assertRaises(ValueError):saved().loads('{"sdk":"llgl","sdk":"sdl"}')
        for path in ('../escape','C:/escape','a\\b','/absolute','x/__pycache__/a.pyc'):
            with self.subTest(path=path),self.assertRaises(ValueError):saved().safe(path)

    def test_narrow_profile_accepts_only_pso_performance(self):self.assertEqual(profile.validate(profile.read(profile.PROFILE))['verified_performance'],['pso'])

    def test_extension_cannot_grant_other_workloads_or_drop_required_gates(self):
        for key,value in [('verified_performance',['pso','allocation']),('pending_tasks',[]),('overall_SDK_ranking',True),('unmodified_upstream',True),('GPU_timestamp_measurement',True),('GPU_memory_reclamation_verified',True),('native_allocation_count_verified',True),('Linux_hardware_accepted',True)]:
            p=profile.read(profile.PROFILE);p[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(p)

    def test_extension_requires_prior_contracts_and_exact_native_receipt(self):
        for key in ('base_contract','prior_performance_contract','evidence'):
            p=profile.read(profile.PROFILE);p[key]['sha256']='0'*64
            with self.subTest(key=key),self.assertRaises(ValueError):profile.validate(p)

    def test_create_release_pair_cannot_be_truncated_or_negative(self):
        run=next(x for x in self.original['runs'] if x['job']['kind']=='measurement')
        for change in (lambda r:r['samples_ms'][0].__setitem__(0,0),lambda r:r['samples_ms'][0].__setitem__(1,-1),lambda r:r['samples_ms'][0].pop(),lambda r:r.update(columns=['gpu_ms'])):
            row=copy.deepcopy(run['result']);change(row)
            with self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)

    def test_llgl_native_call_and_instance_counts_cannot_be_reduced(self):
        for run in self.original['runs']:
            if run['job']['kind'] in ('audit','validation') and run['job']['sdk']=='llgl':
                for key in ('native_draw_calls','native_forwarded_draw_calls','native_instances','native_vertices','native_max_instances'):
                    row=copy.deepcopy(run['result']);row[key]-=1
                    with self.subTest(key=key,scenario=run['job']['scenario']),self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)

    def test_measurement_cannot_claim_native_observation(self):
        run=next(x for x in self.original['runs'] if x['job']['kind']=='measurement' and x['job']['sdk']=='llgl');row=copy.deepcopy(run['result']);row['native_draw_calls']=row['used_pipeline_regions']
        with self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)

    def test_public_pipeline_count_and_shader_are_fixed(self):
        run=next(x for x in self.original['runs'] if x['job']['kind']=='measurement')
        for key,value in [('pipelines_per_iteration',0),('created_pipelines',999),('released_pipeline_handles',0),('used_pipeline_regions',0),('shader_header_sha256','0'*64),('requested_instance_api_version',4211045)]:
            row=copy.deepcopy(run['result']);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)

    def test_cache_or_physical_reclamation_claim_is_rejected(self):
        run=next(x for x in self.original['runs'] if x['job']['kind']=='measurement' and x['job']['sdk']=='llgl')
        for key,value in [('native_pipeline_count_verified',True),('driver_cache_state_known',True),('GPU_memory_reclamation_verified',True),('GPU_use_in_timing',True),('includes_shader_object_create_release',False),('cache_state','cold')]:
            row=copy.deepcopy(run['result']);row[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)

    def test_nested_boolean_work_or_sample_policy_is_rejected(self):
        for change in (lambda p:p['work'].update(pso1=True),lambda p:p['performance'].update(warmup=True),lambda p:p['statistics'].update(samples_removed=False)):
            p=v.read(v.PLAN);change(p)
            with self.assertRaises(ValueError):v.validate_plan(p)

    def test_public_shader_counts_are_checked(self):
        run=next(x for x in self.original['runs'] if x['job']['kind']=='validation' and x['job']['sdk']=='llgl')
        for key in ('created_shader_handles','released_shader_handles'):
            row=copy.deepcopy(run['result']);row[key]-=1
            with self.assertRaises(ValueError):v.validate_output(run['job'],row,self.original)

    def test_gpu_use_readback_and_close_keep_approved_mapping(self):
        original=(v.ROOT/'benchmarks/llgl_vulkan_pso/main.cpp').read_text(encoding='utf-8');current=(v.ROOT/'benchmarks/llgl_vulkan_pso_compare/PSO.hpp').read_text(encoding='utf-8')
        for start,end in [('    void use(', '    void release('),('    std::vector<std::byte> read()', '    ~PSO()')]:
            block=original[original.index(start):original.index(end,original.index(start))];self.assertIn(block,current)

if __name__=='__main__':unittest.main()
