import copy
import json
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_vulkan_pool_churn as trial
import prepare_vulkan_pool_churn as prepare
import recorded_sources
ROOT=pathlib.Path(__file__).resolve().parents[1]
class PoolChurnEvidenceContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        recorded_sources.bind(cls, trial, 'churn')

    def rows(self):
        p=ROOT/'benchmarks/results/2026-10-24-vulkan-pool-churn/results.json'
        self.assertTrue(p.is_file(), 'Required recorded hardware matrix missing')
        return json.loads(p.read_text(encoding='utf-8'))['runs']
    def validate(self,r):return trial.validate(r,r['profile'],r['validation'],r['candidate'])
    def reject(self,fn,profile='mixed',candidate=True):
        r=copy.deepcopy(next(x['result'] for x in self.rows() if x['result']['profile']==profile and x['result']['candidate'] is candidate));fn(r)
        with self.assertRaises((ValueError,KeyError,TypeError)):self.validate(r)
    def test_all_recorded_profiles(self):
        for row in self.rows():
            with self.subTest(name=row['name']):self.validate(row['result'])
    def test_missing_checkpoint(self):self.reject(lambda r:r['checkpoints'].pop())
    def test_duplicate_checkpoint(self):self.reject(lambda r:r['checkpoints'].__setitem__(2,copy.deepcopy(r['checkpoints'][1])))
    def test_wrong_source(self):self.reject(lambda r:r.update(source_fingerprint='0'*64))
    def test_wrong_sdk(self):self.reject(lambda r:r.update(SDK_source_fingerprint=trial.SDK))
    def test_boolean_schema(self):self.reject(lambda r:r.update(schema_version=True))
    def test_missing_native_class(self):self.reject(lambda r:r.update(strides=[4096]))
    def test_no_actual_reuse(self):self.reject(lambda r:r['model'].update(reuses=0))
    def test_unaccounted_growth(self):self.reject(lambda r:r['model'].update(growths=r['model']['growths']+1))
    def test_missing_model_checks(self):self.reject(lambda r:r['model'].update(checks=1))
    def test_missing_readback(self):self.reject(lambda r:r.update(verified_buffers=r['verified_buffers']-1))
    def test_wrong_readback_bytes(self):self.reject(lambda r:r.update(verified_bytes=r['verified_bytes']+1))
    def test_nonempty_cache(self):self.reject(lambda r:r['checkpoints'][-1].update(reserved=1))
    def test_wrong_occupancy(self):self.reject(lambda r:r['checkpoints'][1].update(buffers=1))
    def test_wrong_payload(self):self.reject(lambda r:r['checkpoints'][1].update(payload=1))
    def test_sparse_survivor_count(self):self.reject(lambda r:r['checkpoints'][2].update(buffers=r['checkpoints'][2]['buffers']+1),'sparse')
    def test_singleton_reset(self):self.reject(lambda r:r['checkpoints'][5].update(reserved=65536),'sparse')
    def test_held_retention(self):self.reject(lambda r:r['checkpoints'][3].update(buffers=72),'held')
    def test_baseline_sharing_claim(self):self.reject(lambda r:r['checkpoints'][1].update(allocations=1),candidate=False)
    def test_native_live_leak(self):self.reject(lambda r:r['native_trace']['live'].update(bytes=1))
    def test_native_trace_drop(self):self.reject(lambda r:r['native_trace'].update(dropped_events=1))
    def test_implicit_cleanup(self):self.reject(lambda r:r['native_trace'].update(device_cleanup_bytes=64))
    def test_boolean_native_error(self):self.reject(lambda r:r['native_trace'].update(errors=False))
    def test_missing_native_frame(self):self.reject(lambda r:r['native_trace']['frames'].pop())
    def test_wrong_native_counter(self):self.reject(lambda r:r['native_trace']['frames'][0]['counts']['calls'].update(CreateBuffer=0))
    def test_missing_injection(self):self.reject(lambda r:r['native_trace']['fault']['injections'].pop())
    def test_wrong_injected_error(self):self.reject(lambda r:r['native_trace']['fault']['injections'][0].update(result=-13))
    def test_pending_fault(self):self.reject(lambda r:r['native_trace']['fault'].update(pending=1))
    def test_invalid_free(self):self.reject(lambda r:r['native_trace']['fault'].update(invalid_frees=1))
    def test_unsafe_range(self):self.reject(lambda r:r['native_trace']['fault'].update(guard_errors=1))
    def test_unique_rebase_anchor(self):
        self.assertEqual(prepare.replace('abc','b','x'),'axc')
        for s in ('aa','bb'):
            with self.assertRaises(ValueError):prepare.replace(s,'a','x')
if __name__=='__main__':unittest.main()
