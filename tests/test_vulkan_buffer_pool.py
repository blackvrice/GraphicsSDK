import copy
import pathlib
import sys
import unittest
from unittest import mock
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_vulkan_buffer_pool as v
import prepare_vulkan_buffer_pool as prepare
import test_vulkan_allocation_trace as fixture

class BufferPoolTrialContracts(unittest.TestCase):
    def setUp(self):
        f=fixture.VulkanAllocationTraceContracts();f.setUp();self.r=copy.deepcopy(f.r);self.ref=copy.deepcopy(f.r);self.ids=dict(baseline='b'*64,candidate='d'*64)
        self.r.update(validation=True,loaded_validation_layer='pinned.dll')
        self.patcher=mock.patch.object(v.t,'fingerprint',return_value='a'*64);self.patcher.start();self.addCleanup(self.patcher.stop)
    def candidate(self,n=1):
        self.r.update(SDK_source_fingerprint='d'*64,scenario='buffer1' if n==1 else 'buffer1000',resources_per_iteration=n,created_resources=4*n,released_resource_handles=4*n,used_resources=4*n,verified_resources=4*n,verified_buffers=4*n,verified_bytes=4*n*4096)
        blocks=(n+15)//16;reserved=blocks*65536;frames=[]
        def live(m,b,bytes):return dict(memories=m,buffers=b,bytes=bytes,images=0)
        for i in range(4):
            current=live(0,0,0)
            for phase in range(1,6):
                before=copy.deepcopy(current);c=fixture.empty()
                if phase==1:
                    c['allocated_bytes']=reserved;c['calls'].update(CreateBuffer=n,BindBufferMemory=n,AllocateMemory=blocks);current=live(blocks,n,reserved)
                elif phase==2:
                    c['allocated_bytes']=n*8192;c['calls'].update(CreateBuffer=n*2,BindBufferMemory=n*2,AllocateMemory=n*2);current=live(blocks+n*2,n*3,reserved+n*8192)
                elif phase==3:
                    c['freed_bytes']=n*8192;c['calls'].update(DestroyBuffer=n*2,FreeMemory=n*2);current=live(blocks,n,reserved)
                elif phase==4:
                    c['freed_bytes']=reserved;c['calls'].update(DestroyBuffer=n,FreeMemory=blocks);current=live(0,0,0)
                c['successes']=copy.deepcopy(c['calls']);frames.append(dict(phase=phase,iteration=i,before=before,after=copy.deepcopy(current),counts=c))
        frames.append(dict(phase=6,iteration=4,before=live(0,0,0),after=live(0,0,0),counts=fixture.empty()));self.r['native_trace']['frames']=frames
    def validate(self,variant='baseline'):
        return v.validate_trace(self.r,variant,self.r['scenario'],'high',self.ref,self.ids)
    def reject(self,fn,variant='candidate'):
        fn(self.r)
        with self.assertRaises(ValueError):self.validate(variant)
    def test_original_dedicated_control(self):self.validate()
    def test_single_buffer_reservation(self):self.candidate();self.validate('candidate')
    def test_1000_buffers_63_blocks(self):self.candidate(1000);self.validate('candidate')
    def test_candidate_fingerprint(self):self.candidate();self.reject(lambda r:r.update(SDK_source_fingerprint='b'*64))
    def test_no_physical_reclamation_claim(self):self.candidate();self.reject(lambda r:r.update(GPU_memory_reclamation_verified=True))
    def test_no_trace_timings(self):self.candidate();self.reject(lambda r:r.update(timings_excluded=False))
    def test_missing_byte_readback(self):self.candidate(1000);self.reject(lambda r:r.update(verified_bytes=r['verified_bytes']-1))
    def test_changed_gpu(self):self.candidate();self.reject(lambda r:r.update(device_uuid='5'*32))
    def test_trace_source_identity(self):self.candidate();self.reject(lambda r:r.update(source_fingerprint='f'*64))
    def test_fewer_blocks_than_required(self):self.candidate(1000);self.reject(lambda r:r['native_trace']['frames'][0]['counts']['successes'].update(AllocateMemory=62))
    def test_reserved_byte_accounting(self):self.candidate();self.reject(lambda r:r['native_trace']['frames'][0]['counts'].update(allocated_bytes=4096))
    def test_empty_block_release(self):self.candidate();self.reject(lambda r:r['native_trace']['frames'][3]['counts']['successes'].update(FreeMemory=0))
    def test_missing_phase(self):self.candidate();self.reject(lambda r:r['native_trace']['frames'].pop())
    def test_cross_phase_continuity(self):self.candidate();self.reject(lambda r:r['native_trace']['frames'][1]['before'].update(bytes=0))
    def test_native_ledger_error(self):self.candidate();self.reject(lambda r:r['native_trace'].update(errors=1))
    def test_native_dropped_event(self):self.candidate();self.reject(lambda r:r['native_trace'].update(dropped_events=1))
    def test_native_implicit_teardown(self):self.candidate();self.reject(lambda r:r['native_trace'].update(device_cleanup_resources=1))
    def test_final_live_bytes(self):self.candidate();self.reject(lambda r:r['native_trace']['live'].update(bytes=65536))
    def test_exact_anchor_required(self):
        with self.assertRaises(ValueError):prepare.patch('old old','old','new')
    def test_missing_anchor_rejected(self):
        with self.assertRaises(ValueError):prepare.patch('old','other','new')
    def test_unique_patch(self):self.assertEqual(prepare.patch('start old end','old','new'),'start new end')
    def test_outside_source_root_rejected(self):
        with self.assertRaises(ValueError):prepare.prepare(v.ROOT/'invalid-outside-candidate')
    def test_invalid_native_counter_type(self):self.candidate();self.reject(lambda r:r['native_trace']['frames'][0]['counts']['calls'].update(AllocateMemory=True))
    def test_invalid_native_counter_value(self):self.candidate();self.reject(lambda r:r['native_trace']['frames'][0]['counts']['calls'].update(AllocateMemory=-1))
    def test_layer_schema_revision(self):self.candidate();self.reject(lambda r:r['native_trace'].update(schema_version=2))
    def test_required_validation_layer(self):self.candidate();self.reject(lambda r:r.update(loaded_validation_layer=''))
    def test_logical_release_count(self):self.candidate();self.reject(lambda r:r.update(released_resource_handles=3))
if __name__=='__main__':unittest.main()
