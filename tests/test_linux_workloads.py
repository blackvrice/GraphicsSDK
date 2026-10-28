import copy
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_workloads as v
import port_linux_benchmarks as port

class LinuxPort(unittest.TestCase):
    def test_all_four_adapters_preserve_workload_bodies(self):
        for profile in port.PROFILES:
            source=port.ROOT/('benchmarks/vulkan_'+profile)/'main.cpp';original=source.read_text(encoding='utf-8');before=port.sha(source)
            generated,changes=port.translate(profile,original)
            start=original.index('struct SDK');end=original.index('int main(')
            self.assertEqual(original[start:end],generated[generated.index('struct SDK'):generated.index('int main(')])
            self.assertEqual(len(changes),5);self.assertIn('<<"null"',generated);self.assertNotIn('GetModuleHandle',generated)
            self.assertLess(generated.index('linuxwork::guard();'),generated.index('if(validation){SDK required'))
            self.assertEqual(before,port.sha(source))
    def test_adapter_rejects_missing_or_duplicate_contract_tokens(self):
        text=(port.ROOT/'benchmarks/vulkan_upload/main.cpp').read_text()
        for changed in (text.replace('#include "Identity.hpp"',''),text.replace('unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle','other'),text+text):
            with self.assertRaises(ValueError):port.translate('upload',changed)
    def test_generation_is_deterministic_and_confined_to_out(self):
        with tempfile.TemporaryDirectory(dir=port.ROOT/'out') as td:
            p=pathlib.Path(td)/'upload.cpp';a=port.generate('upload',p);b=port.generate('upload',p);self.assertEqual(a,b);self.assertEqual(port.sha(p),a['generated_sha256'])
        with self.assertRaises(ValueError):port.generate('upload',port.ROOT/'bad.cpp')
        with self.assertRaises(ValueError):port.translate('missing','')

class LinuxWorkloadContracts(unittest.TestCase):
    def setUp(self):
        self.native=dict(name='GPU',device_uuid='a'*32,driver_uuid='b'*32,vendor_id=65541,device_id=0,driver_version_raw=1,api_version_raw=4210000,device_type=4)
        self.base=dict(platform='Linux',kernel='kernel',machine='x86_64',cpu='CPU',ram_bytes=1000,compiler='GCC 14',pointer_bits=64)
        self.ctx=dict(qualification=dict(selected=self.native,environment=self.base,SDK_source_fingerprint='c'*64,SDL_commit='d'*40,validation_library='/runtime/libVkLayer_khronos_validation.so'),profile_fingerprints={p:'e'*64 for p in port.PROFILES},shader_header_sha256='f'*64)
    def job(self,p='upload',sdk='sdl',probe=False):return dict(profile=p,sdk=sdk,kind='probe' if probe else 'validation-smoke')
    def result(self,p='upload',sdk='sdl',probe=False):
        spec=v.read(v.ROOT/'benchmarks/phase3_comparison/contract.json')['profiles'][p];n=0 if probe else 4;d=self.native
        env={**self.base,'gpu':'GPU','software_renderer':True,'device_uuid':d['device_uuid'],'driver_uuid':d['driver_uuid'],'device_luid':None}
        r=dict(schema_version=1,sdk=sdk,api='vulkan',category=spec['category'],contract=spec['contract'],probe=probe,scenario='probe' if probe else v.SCENARIOS[p],preference='high',configuration='Release',validation=not probe,
            source_fingerprint='e'*64,SDK_source_fingerprint='c'*64,SDL_commit='d'*40,device_name='GPU',device_uuid=d['device_uuid'],driver_uuid=d['driver_uuid'],device_luid=None,native_vendor_id=d['vendor_id'],native_device_id=d['device_id'],native_driver_version=d['driver_version_raw'],native_api_version=d['api_version_raw'],identity_attribution=v.ATTRIBUTION,
            warmup=0 if probe else 1,sample_count=0 if probe else 3,readback_verified=not probe,gpu_ms=None,loaded_validation_layer='' if probe else self.ctx['qualification']['validation_library'],environment=env,
            verified_images=n if p!='allocation' else 0,verified_bytes=n*(4096 if p=='allocation' else 65*37*4),samples_ms=[] if probe else [1.,2.,3.],requested_instance_api_version=4198400)
        shape=[0,0,0] if p=='allocation' or (p=='upload' and probe) else [65,37,4];r.update(zip(('width','height','texel_bytes'),shape))
        count=0 if probe else 1
        if p=='draw':r.update(columns=['record_ms','submit_ms','wait_ms','total_ms'],CPU_intervals_ms=[] if probe else [[0,0,t,t] for t in r['samples_ms']],draw_calls_per_frame=count,instances_per_call=count,submitted_draw_calls=n,scissor=[5,3,55,31],vertices_per_instance=3,vertices_per_frame=3*count,first_vertex=0,first_instance=0)
        if p in ('draw','pso'):r['shader_header_sha256']=self.ctx['shader_header_sha256']
        if p in ('pso','allocation'):r.update(columns=['create_ms','public_release_ms'],samples_ms=[] if probe else [[1.,0],[2.,.1],[3.,.2]],GPU_use_in_timing=False,GPU_memory_reclamation_verified=False)
        if p=='pso':r.update(pipelines_per_iteration=count,created_pipelines=n,released_pipeline_handles=n,used_pipeline_regions=n,includes_shader_object_create_release=True,cache_state='uncontrolled_driver_cache_repeated_public_creation')
        if p=='allocation':r.update(resources_per_iteration=count,resource_kind='buffer',buffer_bytes=4096,verified_buffers=n,verified_resources=n,created_resources=n,released_resource_handles=n,used_resources=n,allocation_policy='public_device_resource_default_policy',usage_policy='sdl_vertex_buffer' if sdk=='sdl' else 'sdk_general_device_buffer',native_allocation_count_verified=False)
        return r
    def validate(self,r,p='upload',sdk='sdl',probe=False):return v.validate_result(r,self.job(p,sdk,probe),self.ctx)
    def test_four_profiles_both_sdks_probe_and_smoke(self):
        for p in port.PROFILES:
            for sdk in v.q.SDKS:
                for probe in (False,True):self.validate(self.result(p,sdk,probe),p,sdk,probe)
    def test_platform_and_source_cannot_be_mixed(self):
        for key,value in [('device_luid','00'),('identity_attribution','DXGI'),('source_fingerprint','1'*64),('SDK_source_fingerprint','1'*64),('native_driver_version',0),('gpu_ms',1.)]:
            r=self.result();r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.validate(r)
    def test_full_readback_and_validation_required(self):
        for p in port.PROFILES:
            for key,value in [('verified_bytes',1),('readback_verified',False),('loaded_validation_layer',''),('validation',False)]:
                r=self.result(p);r[key]=value
                with self.subTest(p=p,key=key),self.assertRaises(ValueError):self.validate(r,p)
    def test_probe_must_not_execute_work(self):
        for p in port.PROFILES:
            for key,value in [('samples_ms',[1.]),('verified_bytes',1),('sample_count',1),('readback_verified',True)]:
                r=self.result(p,probe=True);r[key]=value
                with self.subTest(p=p,key=key),self.assertRaises(ValueError):self.validate(r,p,probe=True)
    def test_draw_partition_and_instancing_contract(self):
        r=self.result('draw');r['CPU_intervals_ms'][0][0]=1
        with self.assertRaises(ValueError):self.validate(r,'draw')
        r=self.result('draw');r['instances_per_call']=1000
        with self.assertRaises(ValueError):self.validate(r,'draw')
    def test_pipeline_and_allocation_use_and_release(self):
        for p,key in [('pso','used_pipeline_regions'),('pso','released_pipeline_handles'),('allocation','verified_buffers'),('allocation','used_resources'),('allocation','released_resource_handles')]:
            r=self.result(p);r[key]=3
            with self.subTest(p=p,key=key),self.assertRaises(ValueError):self.validate(r,p)
    def test_zero_release_is_allowed_but_nonfinite_times_are_not(self):
        for p in ('pso','allocation'):
            self.validate(self.result(p),p)
            for value in (-1,True,float('nan'),float('inf')):
                r=self.result(p);r['samples_ms'][0][1]=value
                with self.subTest(p=p,value=value),self.assertRaises(ValueError):self.validate(r,p)
    def test_shader_api_and_usage_boundaries(self):
        for p,key,value in [('draw','shader_header_sha256','bad'),('pso','requested_instance_api_version',4194304),('pso','cache_state','cold'),('allocation','native_allocation_count_verified',True),('allocation','usage_policy','other')]:
            r=self.result(p);r[key]=value
            with self.subTest(p=p,key=key),self.assertRaises(ValueError):self.validate(r,p)
    def test_schedule_and_explicit_argv(self):
        jobs=v.schedule();self.assertEqual(len(jobs),40);self.assertEqual(len({j['name'] for j in jobs}),40)
        self.assertEqual(sum(j['kind']=='probe' for j in jobs),8);self.assertEqual(sum(j['kind']=='validation-smoke' for j in jobs),8)
        c={**self.ctx,'binaries':{p:dict(path=p+'.exe') for p in port.PROFILES}}
        self.assertEqual(v.arguments(self.job(probe=True),c),['upload.exe','--probe','sdl','high','GPU'])
        self.assertEqual(v.arguments(self.job('pso'),c),['pso.exe','sdl','pso1','high','1','3','1','GPU'])
    def test_software_scope_cannot_be_promoted(self):
        r=dict(qualification=dict(physical_probe_qualified=False),P3_BEN_08_completed=False,physical_GPU_accepted=False,performance_accepted=False,all_workload_scenarios_accepted=False,performance_samples=0,validation_timings_excluded=True)
        v.validate_scope(r)
        for key,value in [('P3_BEN_08_completed',True),('physical_GPU_accepted',True),('performance_accepted',True),('all_workload_scenarios_accepted',True),('performance_samples',False),('validation_timings_excluded',False)]:
            bad=copy.deepcopy(r);bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_scope(bad)

if __name__=='__main__':unittest.main()
