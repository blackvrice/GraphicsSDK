import copy
import pathlib
import sys
import unittest
from unittest import mock
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_vulkan_buffer_pool_fault as v
import test_vulkan_allocation_trace as fixture

class BufferFaultContracts(unittest.TestCase):
    def setUp(self):
        self.patcher=mock.patch.object(v,'fingerprint',return_value='a'*64);self.patcher.start();self.addCleanup(self.patcher.stop)
    def data(self,case='double-device',unsafe=False):
        f=fixture.VulkanAllocationTraceContracts();f.setUp();r=copy.deepcopy(f.r)
        g,alloc,bind,size,host,success,held=v.CASES[case]
        r.update(schema_version=1,contract='vulkan_buffer_fault_v1',case=case,validation=True,SDK_source_fingerprint='d'*64,source_fingerprint='a'*64,repetitions=3,verified_buffers=3*(2*g+1),verified_bytes=3*(g*8192+size),regression_detected=unsafe,assertions=50,loaded_validation_layer='pinned.dll')
        frames=[];injections=[]
        for iteration in range(3):
            live=dict(memories=0,bytes=0,buffers=0,images=0)
            for phase in range(1,6):
                before=copy.deepcopy(live);c=fixture.empty();a=c['calls'];s=c['successes']
                def call(name,count,successful=None):a[name]=count;s[name]=count if successful is None else successful
                if phase==1:
                    pool=0 if not g else 4096 if g==1 else 12288
                    for name in ('CreateBuffer','BindBufferMemory','AllocateMemory'):call(name,3*g+held)
                    for name in ('DestroyBuffer','FreeMemory'):call(name,2*g)
                    c.update(allocated_bytes=pool+(2*g+held)*4096,freed_bytes=2*g*4096)
                if phase==2:
                    shared=g==2 and not alloc;mem=success or bool(bind);good=int(mem and not shared);reserved=(4096 if alloc else 4096 if not g else 8192)*good
                    call('CreateBuffer',1);call('AllocateMemory',len(alloc)+good,good);call('BindBufferMemory',int(mem),int(success));call('DestroyBuffer',int(not success));call('FreeMemory',int(bool(bind) and not shared)+unsafe,int(bool(bind) and not shared))
                    c.update(allocated_bytes=reserved,freed_bytes=reserved if bind else 0)
                    for index,code in enumerate(alloc):injections.append(dict(iteration=iteration,call='AllocateMemory',result=code,bytes_or_offset=size if host or size>4096 else 8192 if index==0 else 4096,poisoned=True))
                    if bind:injections.append(dict(iteration=iteration,call='BindBufferMemory',result=bind,bytes_or_offset=4096 if g==2 else 0,poisoned=False))
                if phase==3:
                    helpers=(1 if host else 2)+g-held;retry=int(not success);new=retry*int(not(g==2 and not host));native=(size if host or size>4096 else 4096 if not g else 8192)*new
                    for name in ('CreateBuffer','BindBufferMemory'):call(name,helpers+retry)
                    call('AllocateMemory',helpers+new)
                    for name in ('DestroyBuffer','FreeMemory'):call(name,helpers)
                    helper_bytes=(size if host else 2*size)+(g-held)*4096;c.update(allocated_bytes=helper_bytes+native,freed_bytes=helper_bytes)
                if phase==4:
                    call('DestroyBuffer',live['buffers']);call('FreeMemory',live['memories']);c['freed_bytes']=live['bytes']
                live={k:before[k]+d for k,d in dict(memories=s['AllocateMemory']-s['FreeMemory'],buffers=s['CreateBuffer']-s['DestroyBuffer'],images=0,bytes=c['allocated_bytes']-c['freed_bytes']).items()}
                frames.append(dict(phase=phase,iteration=iteration,before=before,after=copy.deepcopy(live),counts=c))
        frames.append(dict(phase=6,iteration=3,before=live,after=live,counts=fixture.empty()))
        r['native_trace'].update(frames=frames,fault=dict(pending=0,guard_errors=0,invalid_frees=3 if unsafe else 0,injections=injections));return r
    def validate(self,r,unsafe=False):return v.validate(r,r['case'],True,'d'*64,unsafe)
    def reject(self,fn,case='double-device'):
        r=self.data(case);fn(r)
        with self.assertRaises(ValueError):self.validate(r)
    def test_all_clean_case_models(self):
        for case in v.CASES:
            with self.subTest(case=case):self.validate(self.data(case))
    def test_two_guarded_regressions(self):
        for case in ('double-device','dedicated-oom'):self.validate(self.data(case,True),True)
    def test_invalid_free_rejected(self):self.reject(lambda r:r['native_trace']['fault'].update(invalid_frees=1))
    def test_pending_fault_rejected(self):self.reject(lambda r:r['native_trace']['fault'].update(pending=1))
    def test_guard_failure_rejected(self):self.reject(lambda r:r['native_trace']['fault'].update(guard_errors=1))
    def test_poisoned_output_required(self):self.reject(lambda r:r['native_trace']['fault']['injections'][0].update(poisoned=False))
    def test_no_silent_null_only_injection(self):self.reject(lambda r:r['native_trace']['fault']['injections'].clear())
    def test_fallback_size_required(self):self.reject(lambda r:r['native_trace']['fault']['injections'][1].update(bytes_or_offset=8192))
    def test_reused_bind_offset_required(self):self.reject(lambda r:r['native_trace']['fault']['injections'][0].update(bytes_or_offset=0),'bind-reuse')
    def test_recorded_bind_offset_required(self):self.reject(lambda r:r['native_trace']['fault']['injections'][0].update(bytes_or_offset=0),'held-bind')
    def test_wrong_error_rejected(self):self.reject(lambda r:r['native_trace']['fault']['injections'][0].update(result=-4))
    def test_missing_trace_frame(self):self.reject(lambda r:r['native_trace']['frames'].pop())
    def test_continuity_required(self):self.reject(lambda r:r['native_trace']['frames'][1]['before'].update(bytes=0))
    def test_missing_failed_buffer_destroy(self):self.reject(lambda r:r['native_trace']['frames'][1]['counts']['successes'].update(DestroyBuffer=0))
    def test_leaked_shared_slot_rejected(self):self.reject(lambda r:r['native_trace']['frames'][2]['counts']['calls'].update(AllocateMemory=9),'bind-reuse')
    def test_unknown_error_must_not_fallback(self):self.reject(lambda r:r['native_trace']['frames'][1]['counts']['calls'].update(AllocateMemory=2),'pool-unknown')
    def test_full_byte_readback_required(self):self.reject(lambda r:r.update(verified_bytes=1))
    def test_all_guard_readbacks_required(self):self.reject(lambda r:r.update(verified_buffers=3))
    def test_validation_layer_required(self):self.reject(lambda r:r.update(loaded_validation_layer=''))
    def test_sdk_identity_required(self):self.reject(lambda r:r.update(SDK_source_fingerprint='f'*64))
    def test_layer_identity_required(self):self.reject(lambda r:r['native_trace'].update(source_fingerprint='f'*64))
    def test_gpu_luid_required(self):self.reject(lambda r:r.update(device_luid='1'*16))
    def test_no_implicit_device_cleanup(self):self.reject(lambda r:r['native_trace'].update(device_cleanup_resources=1))
    def test_no_dropped_events(self):self.reject(lambda r:r['native_trace'].update(dropped_events=1))
    def test_no_accounting_errors(self):self.reject(lambda r:r['native_trace'].update(errors=1))
    def test_valid_counter_types(self):self.reject(lambda r:r['native_trace']['frames'][1]['counts']['calls'].update(AllocateMemory=True))
    def test_regression_must_be_explicit(self):self.reject(lambda r:r.update(regression_detected=True))
    def test_boolean_guard_counter_rejected(self):self.reject(lambda r:r['native_trace']['fault'].update(invalid_frees=False))
if __name__=='__main__':unittest.main()
