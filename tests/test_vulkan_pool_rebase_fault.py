import copy
import json
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_vulkan_pool_rebase_fault as trial
ROOT=pathlib.Path(__file__).resolve().parents[1]
class RebasedFallbackContracts(unittest.TestCase):
    def data(self):
        p=ROOT/'benchmarks/results/2026-10-24-vulkan-pool-rebase-fault/results.json'
        if not p.exists():self.skipTest('Recorded hardware matrix required')
        return json.loads(p.read_text(encoding='utf-8'))['runs']
    def reject(self,fn,case='mixed-host'):
        r=copy.deepcopy(next(x['result'] for x in self.data() if x['kind']=='mixed' and x['result']['case']==case));fn(r)
        with self.assertRaises((ValueError,KeyError,TypeError)):trial.validate_mixed(r,case,r['validation'])
    def test_all_fresh_mixed_profiles(self):
        for row in self.data():
            if row['kind']=='mixed':
                r=row['result']
                with self.subTest(name=row['name']):trial.validate_mixed(r,r['case'],r['validation'])
    def test_wrong_sdk(self):self.reject(lambda r:r.update(SDK_source_fingerprint=trial.SDK))
    def test_wrong_source(self):self.reject(lambda r:r.update(source_fingerprint='0'*64))
    def test_boolean_schema(self):self.reject(lambda r:r.update(schema_version=True))
    def test_missing_cycle(self):self.reject(lambda r:r['cycles'].pop())
    def test_missing_readback(self):self.reject(lambda r:r.update(verified_buffers=1))
    def test_wrong_bytes(self):self.reject(lambda r:r.update(verified_bytes=r['verified_bytes']-1))
    def test_wrong_assertions(self):self.reject(lambda r:r.update(assertions=r['assertions']+1))
    def test_missing_binding(self):self.reject(lambda r:r['cycles'][0]['before']['bindings'].pop())
    def test_boolean_binding(self):self.reject(lambda r:r['cycles'][0]['before']['bindings'][0].update(offset=False))
    def test_invalid_memory_type(self):self.reject(lambda r:r['cycles'][0]['before']['bindings'][0].update(type_bits=0))
    def test_unaligned_binding(self):self.reject(lambda r:r['cycles'][0]['before']['bindings'][0].update(offset=1))
    def test_wrong_memory_bytes(self):self.reject(lambda r:r['cycles'][0]['before'].update(bytes=1))
    def test_hidden_empty_block(self):self.reject(lambda r:r['cycles'][0]['clean'].update(memories=1,bytes=4096))
    def test_fault_consumed_on_reuse(self):self.reject(lambda r:r['cycles'][0].update(pending_after_reuse=0),'reuse-then-host')
    def test_wrong_reuse_offset(self):
        def mutate(r):
            old={e['buffer'] for e in r['cycles'][0]['before']['bindings']}
            next(e for e in r['cycles'][0]['reused']['bindings'] if e['buffer'] not in old)['offset']=0
        self.reject(mutate,'reuse-then-device')
    def test_missing_fallback_binding(self):self.reject(lambda r:r['cycles'][0]['after_target']['bindings'].pop())
    def test_failed_target_changes_owners(self):self.reject(lambda r:r['cycles'][0]['after_target']['bindings'][0].update(memory=1),'mixed-double')
    def test_failed_target_changes_growth(self):self.reject(lambda r:r['cycles'][0]['retry'].update(bytes=r['cycles'][0]['retry']['bytes']+4096),'mixed-double')
    def test_pending_fault(self):self.reject(lambda r:r['native_trace']['fault'].update(pending=1))
    def test_invalid_free(self):self.reject(lambda r:r['native_trace']['fault'].update(invalid_frees=1))
    def test_binding_guard(self):self.reject(lambda r:r['native_trace']['fault'].update(guard_errors=1))
    def test_unpoisoned_allocation(self):self.reject(lambda r:r['native_trace']['fault']['injections'][0].update(poisoned=False))
    def test_wrong_block_request(self):self.reject(lambda r:r['native_trace']['fault']['injections'][0].update(bytes_or_offset=4096))
    def test_wrong_growth_after_reuse(self):self.reject(lambda r:r['native_trace']['fault']['injections'][0].update(bytes_or_offset=8192),'reuse-then-device')
    def test_missing_second_oom(self):self.reject(lambda r:r['native_trace']['fault']['injections'].pop(),'mixed-double')
    def test_wrong_binding_error(self):self.reject(lambda r:r['native_trace']['fault']['injections'][1].update(result=-2),'held-fallback-bind')
    def test_unknown_error_retried_as_oom(self):self.reject(lambda r:r['native_trace']['fault']['injections'][0].update(result=-2),'mixed-unknown')
    def test_trace_drop(self):self.reject(lambda r:r['native_trace'].update(dropped_events=1))
    def test_implicit_cleanup(self):self.reject(lambda r:r['native_trace'].update(device_cleanup_resources=1))
    def test_boolean_trace_error(self):self.reject(lambda r:r['native_trace'].update(errors=False))
    def test_missing_phase(self):self.reject(lambda r:r['native_trace']['frames'].pop())
    def test_native_final_leak(self):self.reject(lambda r:r['native_trace']['live'].update(bytes=1))
if __name__=='__main__':unittest.main()
