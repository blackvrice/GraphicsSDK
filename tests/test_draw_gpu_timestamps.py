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
import run_draw_gpu_timestamps as v
import check_draw_gpu_timestamps as acceptance

@functools.lru_cache(maxsize=1)
def saved():
    path=acceptance.E/'verify.py';spec=importlib.util.spec_from_file_location('saved_draw_gpu',path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

class DrawGPU(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.folder=acceptance.E/'native';cls.receipt=v.read(cls.folder/'receipt.json')

    def first(self,kind='measurement',mode='timestamp',sdk='sdl'):
        run=next(x for x in self.receipt['runs'] if x['job']['kind']==kind and x['job']['mode']==mode and x['job']['sdk']==sdk);return run, v.read(self.folder/run['native_path'])

    def reject_receipt(self,change):
        r=copy.deepcopy(self.receipt);change(r);reader=v.read
        with mock.patch.object(v,'read',side_effect=lambda p:r if pathlib.Path(p)==self.folder/'receipt.json' else reader(p)),self.assertRaises((ValueError,KeyError)):v.check(self.folder)

    def reject_native(self,change,kind='measurement',mode='timestamp'):
        run,data=self.first(kind,mode);change(data)
        with self.assertRaises((ValueError,KeyError)):v.native(run['job'],data,self.receipt,run['result'])

    def test_all_raw_processes_reproduce(self):self.assertEqual(v.check(self.folder)['GPU_samples'],900)
    def test_independent_replay_has_no_zip_code_or_gpu_execution(self):self.assertEqual(saved().verify()['measurement_CPU_samples'],2700)
    def test_current_acceptance_scope(self):self.assertTrue(acceptance.check()['accepted'])
    def test_seeded_schedule_matches_independent_implementation(self):self.assertEqual(v.schedule(v.read(v.PLAN)),saved().jobs())

    def test_all_21_controls_precede_90_measurements(self):
        jobs=v.schedule(v.read(v.PLAN));self.assertEqual(len(jobs),111);self.assertTrue(all(j['kind']!='measurement' for j in jobs[:21]));self.assertTrue(all(j['kind']=='measurement' for j in jobs[21:]));self.assertEqual(len([j for j in jobs if j['kind']=='validation']),6)

    def test_five_distinct_six_mode_sdk_orders_per_scenario(self):
        jobs=v.schedule(v.read(v.PLAN))[21:]
        for scenario in v.draw.WORK:
            orders=[tuple((j['sdk'],j['mode']) for j in jobs if j['scenario']==scenario and j['repetition']==rep) for rep in range(5)];self.assertEqual(len(set(orders)),5);self.assertTrue(all(set(order)==set(__import__('itertools').product(v.SDKS,v.MODES)) for order in orders))

    def test_plan_cannot_reduce_samples_or_claim_uninstrumented_gpu(self):
        for change in (lambda p:p['performance'].update(samples=3),lambda p:p.update(uninstrumented_GPU_time_claimed=True),lambda p:p['validation'].update(required_core_and_sync=False),lambda p:p['query'].update(results='CPU wait'),lambda p:p['work'].update(draw1=[True,1])):
            p=v.read(v.PLAN);change(p)
            with self.assertRaises(ValueError):v.validate_plan(p)

    def test_source_text_and_original_native_fingerprint_match(self):
        with zipfile.ZipFile(acceptance.E/'source.zip') as z,tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)
            for path in v.source_files():
                name=path.relative_to(v.ROOT).as_posix();raw=z.read(name);self.assertEqual(raw.replace(b'\r\n',b'\n'),path.read_bytes().replace(b'\r\n',b'\n'));p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)
            self.assertEqual(v.fingerprint(root),self.receipt['layer_fingerprint'])

    def test_missing_or_extra_process_is_rejected(self):self.reject_receipt(lambda r:r['runs'].pop());self.reject_receipt(lambda r:r['runs'].append(copy.deepcopy(r['runs'][-1])))
    def test_order_change_is_rejected(self):self.reject_receipt(lambda r:r['runs'].__setitem__(slice(21,23),r['runs'][21:23][::-1]))
    def test_overlapping_process_is_rejected(self):self.reject_receipt(lambda r:r['runs'][1].update(started=r['runs'][0]['started']))
    def test_changed_gpu_or_driver_identity_is_rejected(self):self.reject_receipt(lambda r:r['identity'].update(device_uuid='0'*32));self.reject_receipt(lambda r:r['identity'].update(driver_uuid='0'*32))
    def test_changed_statistics_are_rejected(self):self.reject_receipt(lambda r:r['groups'][0]['statistics']['cpu_total_ms'].update(p50_ms=999))
    def test_old_timings_or_overall_ranking_are_not_accepted(self):self.reject_receipt(lambda r:r.update(historical_timings_reused=True));self.reject_receipt(lambda r:r.update(overall_SDK_ranking=True))
    def test_gpu_retained_slice_cannot_include_warmup(self):self.reject_receipt(lambda r:next(x for x in r['runs'] if x['job']['kind']=='measurement' and x['job']['mode']=='timestamp').update(gpu_ms=[1.0]*35))
    def test_baseline_has_no_layer_or_counter_report(self):
        for run in self.receipt['runs']:
            if run['job']['kind']=='measurement' and run['job']['mode']=='baseline':self.assertIsNone(run['native_sha256']);self.assertIsNone(run['gpu_ms']);self.assertNotIn('VK_INSTANCE_LAYERS',run['overrides'])
    def test_audit_has_no_query_pool_or_counter_claim(self):
        _,data=self.first(mode='audit');d=next(d for d in data['devices'] if d['frames']);self.assertEqual(d['query_pools_created'],0);self.assertTrue(all(f['ticks'] is None and f['gpu_ms'] is None for f in d['frames']))
    def test_instrumentation_does_not_change_original_cpu_fingerprint(self):
        self.assertEqual({x['result']['source_fingerprint'] for x in self.receipt['runs'] if 'result' in x},{self.receipt['draw_fingerprint']})

    def test_wrong_native_device_is_rejected(self):self.reject_native(lambda d:next(x for x in d['devices'] if x['frames']).update(device_uuid='0'*32))
    def test_period_must_be_finite_positive(self):
        for value in (0,-1,float('nan'),float('inf'),True):
            with self.subTest(value=value):self.reject_native(lambda d:next(x for x in d['devices'] if x['frames']).update(timestamp_period_ns=value))
    def test_valid_bits_cannot_be_invalid_or_ambiguous(self):
        for value in (0,65,True,1):
            with self.subTest(value=value):self.reject_native(lambda d:next(x for x in d['devices'] if x['frames'])['frames'][0].update(timestamp_valid_bits=value))
    def test_fence_and_availability_are_required(self):
        for change in (lambda f:f.update(fence_completed=False),lambda f:f.update(availability=[1,0]),lambda f:f.update(query_result=1),lambda f:f.update(submitted=False),lambda f:f.update(fence='0')):
            self.reject_native(lambda d:change(next(x for x in d['devices'] if x['frames'])['frames'][0]))
    def test_uint64_counters_cannot_be_floats_or_booleans(self):
        for value in (1.0,True,'-1',str(1<<64),'01'):
            with self.subTest(value=value):self.reject_native(lambda d:next(x for x in d['devices'] if x['frames'])['frames'][0]['ticks'].__setitem__(0,value))
    def test_counter_conversion_cannot_use_cpu_wait(self):self.reject_native(lambda d:next(x for x in d['devices'] if x['frames'])['frames'][0].update(gpu_ms=999.0))
    def test_same_render_pass_boundary_is_required(self):self.reject_native(lambda d:next(x for x in d['devices'] if x['frames'])['frames'][0].update(start_before_render_pass=False))
    def test_same_actual_queue_is_required(self):self.reject_native(lambda d:next(x for x in d['devices'] if x['frames'])['frames'][-1].update(queue='99'))
    def test_exact_native_draw_and_scissor_work_is_required(self):
        for change in (lambda f:f.update(draw_calls=0),lambda f:f.update(instances=0),lambda f:f.update(bad_arguments=1),lambda f:f.update(scissor=[0,0,65,37]),lambda f:f.update(render_area=[0,0,64,37])):
            self.reject_native(lambda d:change(next(x for x in d['devices'] if x['frames'])['frames'][0]))
    def test_query_and_device_lifetime_is_required(self):self.reject_native(lambda d:next(x for x in d['devices'] if x['frames']).update(query_pools_destroyed=0));self.reject_native(lambda d:d.update(live_devices=1))
    def test_actual_missing_end_is_unavailable_after_completed_fence(self):
        for sdk in v.SDKS:
            run,data=self.first(kind='missing-end',mode='missing-end',sdk=sdk);self.assertEqual(v.native(run['job'],data,self.receipt,run['result']),[]);self.assertTrue(all(f['availability']==[1,0] and f['fence_completed'] for d in data['devices'] for f in d['frames']));self.assertIsNone(run['gpu_ms'])
    def test_actual_wrong_boundary_is_rejected_despite_valid_images(self):
        run,data=self.first(kind='wrong-boundary',mode='wrong-boundary');self.assertTrue(run['result']['readback_verified']);self.assertIsNone(run['gpu_ms']);self.assertTrue(all(not f['start_before_render_pass'] for d in data['devices'] for f in d['frames']))
    def test_actual_core_callback_has_no_gpu_work(self):
        run=next(x for x in self.receipt['runs'] if x['job']['kind']=='validation-control');data=v.read(self.folder/run['native_path']);v.validation_control(data,self.receipt);self.assertTrue(all(not x['frames'] for x in data['devices']));self.assertNotEqual(run['returncode'],0)
    def test_validation_control_cannot_accept_unrelated_failure(self):
        with self.assertRaises(ValueError):v.rejection(dict(kind='validation-control',sdk='graphicssdk'),1,b'',b'FAIL: unrelated initialization error')
    def test_layer_callstack_must_validate_injected_calls(self):
        v.layer_trace(b'vkCreateInstance layer callstack setup to: VK_LAYER_GFX_draw_timestamp VK_LAYER_KHRONOS_validation <Drivers>')
        with self.assertRaises(ValueError):v.layer_trace(b'vkCreateInstance layer callstack setup to: VK_LAYER_KHRONOS_validation VK_LAYER_GFX_draw_timestamp <Drivers>')
    def test_performance_cannot_load_validation(self):self.reject_native(lambda d:d['runtime_modules'].append('VkLayer_khronos_validation.dll'))
    def test_statistics_retain_all_outliers(self):
        values=[[float(i+1) for i in range(30)] for _ in range(5)];values[-1][-1]=10000;self.assertEqual(saved().stats(values),v.protocol.scalar_stats(values));self.assertEqual(saved().stats(values)['samples_removed'],0)
    def test_raw_counter_tampering_is_rejected(self):
        run,_=self.first()
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(acceptance.E,root);p=root/'native'/run['native_path'];p.write_bytes(p.read_bytes()+b' ')
            with self.assertRaisesRegex(ValueError,'Evidence hash differs'):saved().verify(root)
    def test_rebound_gpu_statistic_is_still_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)/'evidence';shutil.copytree(acceptance.E,root);p=root/'native/receipt.json';r=saved().read(p);next(g for g in r['groups'] if g['mode']=='timestamp')['statistics']['gpu_ms']['p50_ms']=999;p.write_bytes(json.dumps(r).encode());m=saved().read(root/'manifest.json');item=next(x for x in m['files'] if x['path']=='native/receipt.json');item.update(bytes=p.stat().st_size,sha256=saved().sha(p.read_bytes()));(root/'manifest.json').write_bytes(json.dumps(m).encode())
            with self.assertRaisesRegex(ValueError,'statistics'):saved().verify(root)
    def test_duplicate_json_keys_and_unsafe_paths_are_rejected(self):
        with self.assertRaises(ValueError):saved().loads('{"gpu_ms":1,"gpu_ms":2}')
        for name in ('../escape','/absolute','C:/escape','a\\b','cache/__pycache__/a.pyc'):
            with self.assertRaises(ValueError):saved().safe(name)

if __name__=='__main__':unittest.main()
