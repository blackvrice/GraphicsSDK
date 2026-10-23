import copy
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_vulkan_pso as v

class VulkanPsoContracts(unittest.TestCase):
    def setUp(self):
        self.r=dict(schema_version=1,sdk='sdl',api='vulkan',configuration='Release',category='cpu_api',contract=v.CONTRACT,identity_attribution='unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle',
            device_name='GPU',device_uuid='1'*32,driver_uuid='2'*32,device_luid='0100000000000000',native_vendor_id=1,native_device_id=2,native_driver_version=3,native_api_version=4,
            environment=dict(gpu='GPU',software_renderer=False,pointer_bits=64,driver_query_available=True,vendor_id=1,device_id=2,luid='0:1',cpu='CPU',driver='driver',os='OS'),source_fingerprint='a'*64,SDK_source_fingerprint='b'*64,SDL_commit='c'*40,
            scenario='pso1',preference='high',warmup=1,sample_count=3,validation=False,probe=False,readback_verified=True,gpu_ms=None,width=65,height=37,texel_bytes=4,verified_images=4,verified_bytes=65*37*4*4,samples_ms=[[1.,.1],[2.,.2],[3.,.3]],loaded_validation_layer='')
        self.r.update(requested_instance_api_version=4198400,pipelines_per_iteration=1,created_pipelines=4,released_pipeline_handles=4,used_pipeline_regions=4,shader_header_sha256=v.hashlib.sha256((v.ROOT/'tests/fixtures/SpirvFixtures.hpp').read_bytes()).hexdigest(),includes_shader_object_create_release=True,GPU_use_in_timing=False,GPU_memory_reclamation_verified=False,cache_state='uncontrolled_driver_cache_repeated_public_creation',columns=['create_ms','public_release_ms'])
        self.ref=copy.deepcopy(self.r)
    def validate(self,r=None,validation=False):return v.validate_result(r or self.r,'sdl','pso1','high',1,3,validation,self.ref,'a'*64,'b'*64,'c'*40)
    def reject(self,key,value):
        self.r[key]=value
        with self.assertRaises(ValueError):self.validate()
    def test_matching_completed_payload(self):self.assertEqual(self.validate(),[[1,.1],[2,.2],[3,.3]])
    def test_api_separation(self):self.reject('api','d3d12')
    def test_category_separation(self):self.reject('category','gpu_timestamp')
    def test_contract_revision(self):self.reject('contract','other')
    def test_release_only(self):self.reject('configuration','Debug')
    def test_not_probe(self):self.reject('probe',True)
    def test_full_readback_required(self):self.reject('readback_verified',False)
    def test_every_iteration_verified(self):self.reject('verified_images',3)
    def test_every_byte_verified(self):self.reject('verified_bytes',self.r['verified_bytes']-1)
    def test_no_timestamp_inference(self):self.reject('gpu_ms',1.)
    def test_required_null_field(self):
        del self.r['gpu_ms']
        with self.assertRaises(ValueError):self.validate()
    def test_invalid_times(self):
        for bad in (float('nan'),float('inf'),-1.,0.,True,'1'):
            with self.subTest(bad=bad):
                r=copy.deepcopy(self.r);r['samples_ms'][1]=bad
                with self.assertRaises(ValueError):self.validate(r)
    def test_sample_count(self):self.reject('samples_ms',[1,2])
    def test_sample_count_boolean(self):self.reject('sample_count',True)
    def test_warmup_contract(self):self.reject('warmup',2)
    def test_shape(self):self.reject('width',66)
    def test_sdk_source_stale(self):self.reject('SDK_source_fingerprint','d'*64)
    def test_harness_source_stale(self):self.reject('source_fingerprint','d'*64)
    def test_dependency_pin_stale(self):self.reject('SDL_commit','d'*40)
    def test_gpu_changed(self):self.reject('device_uuid','3'*32)
    def test_driver_changed(self):self.reject('native_driver_version',5)
    def test_cpu_changed(self):
        self.r['environment']['cpu']='other'
        with self.assertRaises(ValueError):self.validate()
    def test_native_dxgi_luid_disagree(self):self.reject('device_luid','0200000000000000')
    def test_native_dxgi_vendor_disagree(self):self.reject('native_vendor_id',9)
    def test_invalid_identity(self):
        for invalid in ('0'*32,'x'*32,'1'*31):
            with self.subTest(invalid=invalid):self.reject('device_uuid',invalid)
    def test_software_rejected(self):
        self.r['environment']['software_renderer']=True
        with self.assertRaises(ValueError):self.validate()
    def test_validation_contamination(self):self.reject('loaded_validation_layer','layer.dll')
    def test_required_validation_layer(self):
        self.r['validation']=True
        with self.assertRaises(ValueError):self.validate(validation=True)
        self.r['loaded_validation_layer']='pinned.dll';self.assertEqual(self.validate(validation=True),[[1,.1],[2,.2],[3,.3]])
    def test_percentile_interpolation(self):
        self.assertEqual(v.percentile([1,4,2,3],.5),2.5);self.assertAlmostEqual(v.percentile([1,4,2,3],.95),3.85)
    def test_outliers_retained_and_process_medians(self):
        s=v.summarize([dict(samples_ms=[[1,0],[2,0],[3,0]]),dict(samples_ms=[[2,0],[3,0],[100,0]])])
        self.assertEqual(s['create_ms']['samples'],6);self.assertEqual(s['create_ms']['process_medians_ms'],[2,3]);self.assertEqual(s['create_ms']['max_ms'],100);self.assertEqual(s['create_ms']['samples_removed'],0);self.assertEqual(s['public_release_ms']['p50_ms'],0)
    def test_invalid_percentile(self):
        for values,q in (([],.5),([1],-1),([1],2),([1],float('nan'))):
            with self.assertRaises(ValueError):v.percentile(values,q)

    def test_created_count(self):self.reject('created_pipelines',3)
    def test_release_count(self):self.reject('released_pipeline_handles',3)
    def test_used_regions(self):self.reject('used_pipeline_regions',3)
    def test_shader_objects_included(self):self.reject('includes_shader_object_create_release',False)
    def test_GPU_use_untimed(self):self.reject('GPU_use_in_timing',True)
    def test_actual_memory_not_claimed(self):self.reject('GPU_memory_reclamation_verified',True)
    def test_cache_not_classified(self):self.reject('cache_state','warm_hit')
    def test_zero_release_allowed(self):
        self.r['samples_ms'][0][1]=0;self.validate()
    def test_interval_values(self):
        for value in (float('nan'),float('inf'),-1,True,'1'):
            with self.subTest(value=value):
                self.r['samples_ms'][0][1]=value
                with self.assertRaises(ValueError):self.validate()
    def test_no_total_interval(self):
        self.r['samples_ms'][0].append(1.1)
        with self.assertRaises(ValueError):self.validate()
    def test_instance_request(self):self.reject('requested_instance_api_version',4194304)
    def test_shader_identity(self):self.reject('shader_header_sha256','f'*64)
    def test_validation_errors_on_stdout(self):
        with self.assertRaises(ValueError):v.parse_output(b'Validation Error: VUID-shader ERROR',b'')
if __name__=='__main__':unittest.main()
