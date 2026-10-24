import copy
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_vulkan_allocation_trace as v

def empty():return dict(calls=dict.fromkeys(v.CALLS,0),successes=dict.fromkeys(v.CALLS,0),allocated_bytes=0,freed_bytes=0,errors=0)
def live(mem=0):return dict(memories=mem,bytes=mem*4096,buffers=mem,images=0)
class VulkanAllocationTraceContracts(unittest.TestCase):
    def setUp(self):
        self.r=dict(schema_version=1,sdk='graphicssdk',scenario='buffer1',preference='high',api='vulkan',configuration='Release',category='native_allocation_trace',contract=v.CONTRACT,validation=False,source_fingerprint='a'*64,SDK_source_fingerprint='b'*64,SDL_commit='c'*40,warmup=1,sample_count=3,probe=False,readback_verified=True,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,native_allocation_count_verified=True,timings_excluded=True,gpu_ms=None,columns=[],samples_ms=[],requested_instance_api_version=4198400,
            device_name='GPU',device_uuid='1'*32,driver_uuid='2'*32,device_luid='0100000000000000',native_vendor_id=1,native_device_id=2,native_driver_version=3,native_api_version=4,identity_attribution='unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle',environment=dict(gpu='GPU',software_renderer=False,pointer_bits=64,driver_query_available=True,vendor_id=1,device_id=2,luid='0:1',cpu='CPU',os='OS',driver='driver'),loaded_validation_layer='',resources_per_iteration=1,created_resources=4,released_resource_handles=4,used_resources=4,verified_resources=4,verified_images=0,verified_buffers=4,verified_bytes=16384,width=0,height=0,texel_bytes=0,buffer_bytes=4096,resource_kind='buffer',allocation_policy='public_device_resource_default_policy',usage_policy='sdk_general_device_buffer')
        frames=[]
        for i in range(4):
            for phase,before,after,n in ((1,0,1,1),(2,1,3,2),(3,3,1,2),(4,1,0,1),(5,0,0,0)):
                c=empty()
                for name in (('CreateBuffer','AllocateMemory','BindBufferMemory') if phase in (1,2) else ('DestroyBuffer','FreeMemory') if phase in (3,4) else ()):
                    c['calls'][name]=c['successes'][name]=n
                if phase in (1,2):c['allocated_bytes']=4096*n
                if phase in (3,4):c['freed_bytes']=4096*n
                frames.append(dict(phase=phase,iteration=i,before=live(before),after=live(after),counts=c))
        frames.append(dict(phase=6,iteration=4,before=live(),after=live(),counts=empty()))
        self.r['native_trace']=dict(schema_version=1,source_fingerprint='a'*64,dropped_events=0,errors=0,device_cleanup_resources=0,device_cleanup_bytes=0,live=live(),outside=empty(),frames=frames)
        self.ref=copy.deepcopy(self.r)
    def validate(self):return v.validate(self.r,'graphicssdk','buffer1','high',False,self.ref,'a'*64,'b'*64,'c'*40)
    def reject(self,fn):
        fn(self.r)
        with self.assertRaises(ValueError):self.validate()
    def test_complete_balanced_trace(self):self.assertEqual(len(self.validate()['frames']),21)
    def test_performance_samples_rejected(self):self.reject(lambda r:r.update(samples_ms=[[1,2]]))
    def test_timings_must_be_excluded(self):self.reject(lambda r:r.update(timings_excluded=False))
    def test_old_contract_rejected(self):self.reject(lambda r:r.update(contract=v.base.CONTRACT))
    def test_category_separation(self):self.reject(lambda r:r.update(category='cpu_api'))
    def test_stale_layer(self):self.reject(lambda r:r['native_trace'].update(source_fingerprint='d'*64))
    def test_dropped_events(self):self.reject(lambda r:r['native_trace'].update(dropped_events=1))
    def test_accounting_errors(self):self.reject(lambda r:r['native_trace']['outside'].update(errors=1))
    def test_phase_errors(self):self.reject(lambda r:r['native_trace'].update(errors=1))
    def test_implicit_cleanup_not_counted_as_free(self):self.reject(lambda r:r['native_trace'].update(device_cleanup_resources=1))
    def test_implicit_cleanup_bytes(self):self.reject(lambda r:r['native_trace'].update(device_cleanup_bytes=4096))
    def test_missing_scope(self):self.reject(lambda r:r['native_trace']['frames'].pop())
    def test_duplicate_scope(self):self.reject(lambda r:r['native_trace']['frames'].__setitem__(1,r['native_trace']['frames'][0]))
    def test_byte_conservation(self):self.reject(lambda r:r['native_trace']['frames'][0]['counts'].update(allocated_bytes=4095))
    def test_object_conservation(self):self.reject(lambda r:r['native_trace']['frames'][0]['after'].update(memories=2))
    def test_cross_scope_continuity(self):self.reject(lambda r:r['native_trace']['frames'][1]['before'].update(bytes=8192))
    def test_unknown_call(self):self.reject(lambda r:r['native_trace']['frames'][0]['counts']['calls'].update(Unknown=1))
    def test_native_call_failure(self):self.reject(lambda r:r['native_trace']['frames'][0]['counts']['calls'].update(AllocateMemory=2))
    def test_counter_boolean_rejected(self):self.reject(lambda r:r['native_trace']['frames'][0]['counts']['calls'].update(AllocateMemory=True))
    def test_counter_negative_rejected(self):self.reject(lambda r:r['native_trace']['frames'][0]['counts']['calls'].update(AllocateMemory=-1))
    def test_counter_overflow_rejected(self):self.reject(lambda r:r['native_trace']['frames'][0]['counts']['calls'].update(AllocateMemory=2**64))
    def test_more_successes_than_calls(self):self.reject(lambda r:r['native_trace']['frames'][0]['counts']['successes'].update(CreateBuffer=2))
    def test_full_readback(self):self.reject(lambda r:r.update(verified_bytes=16383))
    def test_no_memory_reclamation_claim(self):self.reject(lambda r:r.update(GPU_memory_reclamation_verified=True))
    def test_no_gpu_timestamp(self):self.reject(lambda r:r.update(gpu_ms=1))
    def test_validation_contamination(self):self.reject(lambda r:r.update(loaded_validation_layer='vvl.dll'))
    def test_gpu_identity(self):self.reject(lambda r:r.update(device_uuid='3'*32))
    def test_final_live_resources(self):self.reject(lambda r:r['native_trace']['live'].update(memories=1))
    def test_snapshot_boolean(self):self.reject(lambda r:r['native_trace']['frames'][0]['after'].update(bytes=True))
    def test_actual_interception_required(self):self.reject(lambda r:r.update(native_allocation_count_verified=False))
if __name__=='__main__':unittest.main()
