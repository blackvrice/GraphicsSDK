import copy
import json
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_vulkan_loader_fault as trial
import report_vulkan_loader_fault as report
ROOT=pathlib.Path(__file__).resolve().parents[1]
class LoaderEvidenceContracts(unittest.TestCase):
    def data(self):
        # Recorded native evidence makes mutation tests independent of a second
        # hand-written implementation of the loader ownership state machine.
        folder=ROOT/'benchmarks/results/2026-10-24-vulkan-loader-fault-v2'
        if not (folder/'results.json').exists():self.skipTest('Recorded hardware matrix required')
        return json.loads((folder/'results.json').read_text(encoding='utf-8'))['runs']
    def validate(self,row):
        r=row['result'];return trial.validate(r,r['kind'],r['validation'],r['result'],r['poison'],trial.OLD if row['variant']=='before' else trial.SDK,row['variant']=='before')
    def test_every_recorded_profile(self):
        for row in self.data():
            with self.subTest(name=row['name']):self.validate(row)
    def rejected(self,mutate,kind='device',before=False):
        row=copy.deepcopy(next(r for r in self.data() if r['result']['kind']==kind and r['variant']==('before' if before else 'after')));mutate(row['result'])
        with self.assertRaises((ValueError,KeyError,TypeError)):self.validate(row)
    def test_missing_cycle(self):self.rejected(lambda r:r['cycles'].pop())
    def test_duplicate_cycle(self):self.rejected(lambda r:r['cycles'].__setitem__(1,copy.deepcopy(r['cycles'][0])))
    def test_wrong_sdk(self):self.rejected(lambda r:r.update(SDK_source_fingerprint=trial.OLD))
    def test_wrong_layer(self):self.rejected(lambda r:r.update(source_fingerprint='0'*64))
    def test_wrong_profile(self):self.rejected(lambda r:r.update(kind='instance'))
    def test_boolean_error(self):self.rejected(lambda r:r.update(result=True))
    def test_boolean_poison(self):self.rejected(lambda r:r.update(poison=False))
    def test_missing_guard_data(self):self.rejected(lambda r:r.update(verified_buffers=r['verified_buffers']-1))
    def test_missing_frame(self):self.rejected(lambda r:r.update(frames=0),'surface')
    def test_wrong_byte_count(self):self.rejected(lambda r:r.update(verified_bytes=1))
    def test_unconsumed_error(self):self.rejected(lambda r:r['cycles'][0]['failed'].update(pending=1))
    def test_extra_injection(self):self.rejected(lambda r:r['cycles'][0]['failed'].update(injections=2))
    def test_invalid_cleanup(self):self.rejected(lambda r:r['final'].update(invalid=1))
    def test_hidden_regression(self):self.rejected(lambda r:r['final'].update(invalid=0),'messenger',True)
    def test_leaked_native_instance(self):self.rejected(lambda r:r['final']['live'].__setitem__(0,1))
    def test_unaccounted_device_destruction(self):self.rejected(lambda r:r['final']['destroyed'].__setitem__(2,0))
    def test_failed_creation_adopts_output(self):self.rejected(lambda r:r['cycles'][0]['failed']['created'].__setitem__(2,2))
    def test_retry_not_observed(self):self.rejected(lambda r:r['cycles'][0].update(retry=copy.deepcopy(r['cycles'][0]['failed'])))
    def test_trace_errors(self):self.rejected(lambda r:r['cycles'][0]['before'].update(errors=1))
    def test_boolean_counter(self):self.rejected(lambda r:r['cycles'][0]['before']['attempts'].__setitem__(0,True))
    def test_unknown_snapshot_field(self):self.rejected(lambda r:r['cycles'][0]['before'].update(hidden=1))
    def test_boolean_schema(self):self.rejected(lambda r:r.update(schema_version=True))
    def test_json_scalar_identity(self):
        self.assertFalse(report.same({'nested':[0,1]},{'nested':[False,True]}))
        self.assertTrue(report.same({'b':1,'a':[0]},{'a':[0],'b':1}))
if __name__=='__main__':unittest.main()
