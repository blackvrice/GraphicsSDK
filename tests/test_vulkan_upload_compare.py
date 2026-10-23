import copy
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import compare_vulkan_upload as c

class CPUStageContracts(unittest.TestCase):
    def setUp(self):
        self.ref=dict(device_name='GPU',device_uuid='1'*32,driver_uuid='2'*32,device_luid='3'*16,environment=dict(cpu='CPU',driver='driver'))
        self.r=dict(**self.ref,category='diagnostic_CPU_API_intervals',contract=c.PROFILE_CONTRACT,api='vulkan',gpu_ms=None,SDK_source_fingerprint='old',validation=False,sample_count=3,warmup=1,scenario='upload1080',verified_images=4,verified_bytes=1920*1080*4*4,columns=['write_ms','begin_ms','record_ms','submit_ms','wait_ms','total_ms'],samples_ms=[[1,2,3,4,5,15] for _ in range(3)])
        self.r=copy.deepcopy(self.r)
    def validate(self):c.validate_profile(self.r,self.ref,'old',3,1)
    def reject(self,key,value):
        self.r[key]=value
        with self.assertRaises(ValueError):self.validate()
    def test_partition(self):self.validate()
    def test_end_to_end_is_separate(self):self.reject('category','end_to_end')
    def test_not_GPU_time(self):self.reject('gpu_ms',15)
    def test_changed_sdk(self):self.reject('SDK_source_fingerprint','new')
    def test_changed_driver(self):
        self.r['environment']['driver']='other'
        with self.assertRaises(ValueError):self.validate()
    def test_changed_device(self):self.reject('device_uuid','4'*32)
    def test_validation_excluded(self):self.reject('validation',True)
    def test_incomplete_reference(self):self.reject('verified_images',3)
    def test_byte_count(self):self.reject('verified_bytes',1)
    def test_boundary_order(self):self.reject('columns',['begin_ms','write_ms','record_ms','submit_ms','wait_ms','total_ms'])
    def test_sum_does_not_match(self):
        self.r['samples_ms'][1][-1]=16
        with self.assertRaises(ValueError):self.validate()
    def test_missing_rows(self):self.reject('samples_ms',self.r['samples_ms'][:2])
    def test_bad_intervals(self):
        for bad in (float('nan'),float('inf'),-1,True,'1'):
            with self.subTest(bad=bad):
                self.r['samples_ms'][0][0]=bad
                with self.assertRaises(ValueError):self.validate()
if __name__=='__main__':unittest.main()
