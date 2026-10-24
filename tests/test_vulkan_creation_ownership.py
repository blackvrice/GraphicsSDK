import copy
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import audit_licenses as audit
import run_vulkan_creation_ownership as trial
import vulkan_creation_provenance as history

class ProductOwnershipContracts(unittest.TestCase):
    def data(self,before=False):
        cases=[dict(case=c,api=trial.CPU_APIS[c],ordinal=2 if c in ('sampler-second','shader-second','swap-semaphore-second') else 1,invalid_cleanup=int(before and c not in trial.SAFE_BEFORE),created=2,destroyed=2,live=0,assertions=10) for c in trial.CPU_CASES]
        return dict(SDK_source_fingerprint='d'*64,GPU_calls=0,cases=cases,invalid_cleanup=sum(c['invalid_cleanup'] for c in cases),assertions=270,created=54)
    def reject(self,fn,before=False):
        r=self.data(before);fn(r)
        with self.assertRaises(ValueError):trial.validate_cpu(r,'d'*64,before)
    def test_valid_clean_matrix(self):trial.validate_cpu(self.data(),'d'*64,False)
    def test_valid_regression_matrix(self):trial.validate_cpu(self.data(True),'d'*64,True)
    def test_missing_case(self):self.reject(lambda r:r['cases'].pop())
    def test_duplicate_case(self):self.reject(lambda r:r['cases'][0].update(case='image'))
    def test_invalid_cleanup(self):self.reject(lambda r:r['cases'][0].update(invalid_cleanup=1))
    def test_leaked_object(self):self.reject(lambda r:r['cases'][0].update(live=1))
    def test_undestroyed_object(self):self.reject(lambda r:r['cases'][0].update(destroyed=1))
    def test_wrong_sdk(self):self.reject(lambda r:r.update(SDK_source_fingerprint='f'*64))
    def test_cpu_is_not_gpu(self):self.reject(lambda r:r.update(GPU_calls=1))
    def test_boolean_gpu_calls(self):self.reject(lambda r:r.update(GPU_calls=False))
    def test_masked_regression(self):self.reject(lambda r:r['cases'][0].update(invalid_cleanup=0),True)
    def test_counter_aggregate(self):self.reject(lambda r:r.update(created=1))

class HistoricalOwnershipContracts(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=pathlib.Path(self.temp.name)
        self.name='src/backends/vulkan/Vulkan.cpp';p=self.root/self.name;p.parent.mkdir(parents=True);p.write_bytes(b'old');self.row=audit.record_file(self.root,p)
    def snapshot(self,data=b'old'):
        p=self.root/history.SNAPSHOT/self.name;p.parent.mkdir(parents=True);p.write_bytes(data);(self.root/self.name).write_bytes(b'new');return p
    def test_unchanged_current_input(self):self.assertEqual(history.resolve(self.row,self.root),self.row)
    def test_verified_snapshot_resolution(self):
        p=self.snapshot()
        with mock.patch.object(audit,'sdk_fingerprint',return_value=history.OLD):self.assertEqual(history.resolve(self.row,self.root),audit.record_file(self.root,p))
    def test_changed_snapshot_bytes(self):
        self.snapshot(b'wrong')
        with mock.patch.object(audit,'sdk_fingerprint',return_value=history.OLD),self.assertRaises(audit.AuditError):history.resolve(self.row,self.root)
    def test_wrong_snapshot_identity(self):
        self.snapshot()
        with mock.patch.object(audit,'sdk_fingerprint',return_value='f'*64),self.assertRaises(audit.AuditError):history.resolve(self.row,self.root)
    def test_unreviewed_historical_path(self):
        row=dict(self.row,path='other.txt')
        with self.assertRaises(audit.AuditError):history.resolve(row,self.root)
    def test_traversal_path(self):
        with self.assertRaises(audit.AuditError):history.resolve(dict(self.row,path='../outside'),self.root)
    def test_absolute_path(self):
        with self.assertRaises(audit.AuditError):history.resolve(dict(self.row,path=str((self.root/self.name).resolve())),self.root)
    def test_matching_current_is_preferred(self):
        self.snapshot();(self.root/self.name).write_bytes(b'old');self.assertEqual(history.resolve(self.row,self.root),self.row)
if __name__=='__main__':unittest.main()
