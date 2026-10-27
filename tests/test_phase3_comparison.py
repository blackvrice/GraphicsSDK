import copy
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import prepare_vulkan_comparison as v


class ComparisonInputs(unittest.TestCase):
    def setUp(self):
        self.contract=v.read(v.PLAN)
        self.context=dict(expected_gpu='GPU',SDL_commit='c'*40,SDK_source_fingerprint='b'*64,profile_fingerprints={n:'a'*64 for n in v.NAMES},shader_header_sha256='d'*64)

    def probe(self,n='upload',sdk='sdl'):
        return dict(schema_version=1,sdk=sdk,api='vulkan',configuration='Release',category='end_to_end' if n in ('upload','draw') else 'cpu_api',contract=v.MODULES[n].CONTRACT,
            identity_attribution='unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle',device_name='GPU',device_uuid='1'*32,driver_uuid='2'*32,device_luid='0100000000000000',native_vendor_id=4318,native_device_id=2,native_driver_version=3,native_api_version=4,
            environment=dict(gpu='GPU',software_renderer=False,pointer_bits=64,driver_query_available=True,vendor_id=4318,device_id=2,luid='0:1',cpu='CPU',driver='driver',os='OS'),source_fingerprint='a'*64,SDK_source_fingerprint='b'*64,SDL_commit='c'*40,
            probe=True,scenario='probe',preference='high',validation=False,warmup=0,sample_count=0,samples_ms=[],readback_verified=False,gpu_ms=None,loaded_validation_layer='',shader_header_sha256='d'*64,verified_bytes=0,created_resources=0,CPU_intervals_ms=[])

    def test_all_eight_metadata_probes_accept(self):
        ref=None
        for n in v.NAMES:
            for sdk in ('graphicssdk','sdl'):ref=v.validate_probe(n,sdk,self.probe(n,sdk),self.context,ref)

    def test_contract_matches_existing_thirteen_workloads(self):
        self.assertEqual(v.validate_contract(self.contract),self.contract)
        self.assertEqual(sum(len(p['shapes']) for p in self.contract['profiles'].values()),13)

    def test_build_and_workload_drift_rejected(self):
        for section,key,value in [('build','runtime_library','MultiThreaded'),('build','SDK_VULKAN','OFF'),('adapter_policy','SDL_native_handle_verified',True),('measurement_policy','next_gate','done')]:
            c=copy.deepcopy(self.contract);c[section][key]=value
            with self.subTest(section=section,key=key),self.assertRaises(ValueError):v.validate_contract(c)
        for n,key,value in [('upload','shapes',{}),('draw','scissor',[0,0,65,37]),('pso','work',{'pso1':1,'pso8':7}),('allocation','buffer_bytes',8192)]:
            c=copy.deepcopy(self.contract);c['profiles'][n][key]=value
            with self.subTest(profile=n),self.assertRaises(ValueError):v.validate_contract(c)

    def test_probe_stage_cannot_promote_results(self):
        for key,value in [('performance_samples',1),('performance_samples',False),('correctness_accepted',True),('historical_timings_reused',True),('performance_comparison_accepted',True),('overall_SDK_ranking',True),('future_required',[])]:
            c=copy.deepcopy(self.contract);c['measurement_policy'][key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):v.validate_contract(c)

    def test_sample_or_workload_contamination(self):
        for key,value in [('samples_ms',[1]),('sample_count',1),('sample_count',False),('warmup',1),('probe',False),('readback_verified',True),('gpu_ms',1),('verified_bytes',1),('created_resources',1),('submitted_commands',1),('CPU_intervals_ms',[[1]])]:
            r=self.probe();r[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):v.validate_probe('upload','sdl',r,self.context)

    def test_stale_sources_and_pins(self):
        for key,value in [('source_fingerprint','d'*64),('SDK_source_fingerprint','d'*64),('SDL_commit','d'*40)]:
            r=self.probe();r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_probe('upload','sdl',r,self.context)
        r=self.probe('draw');r['shader_header_sha256']='e'*64
        with self.assertRaises(ValueError):v.validate_probe('draw','sdl',r,self.context)

    def test_expected_gpu_and_no_software_fallback(self):
        r=self.probe();r['device_name']=r['environment']['gpu']='other'
        with self.assertRaises(ValueError):v.validate_probe('upload','sdl',r,self.context)
        r=self.probe();r['environment']['software_renderer']=True
        with self.assertRaises(ValueError):v.validate_probe('upload','sdl',r,self.context)

    def test_native_dxgi_identity_must_agree(self):
        for key,value in [('device_luid','0200000000000000'),('native_vendor_id',9),('identity_attribution','SDL_native_handle'),('device_uuid','0'*32)]:
            r=self.probe();r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_probe('upload','sdl',r,self.context)

    def test_cross_profile_driver_and_environment_stable(self):
        ref=v.validate_probe('upload','sdl',self.probe(),self.context)
        for key,value in [('driver_uuid','3'*32),('native_driver_version',7),('device_uuid','4'*32)]:
            r=self.probe('pso');r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_probe('pso','sdl',r,self.context,ref)
        r=self.probe('pso');r['environment']['cpu']='other'
        with self.assertRaises(ValueError):v.validate_probe('pso','sdl',r,self.context,ref)

    def test_validation_mode_cannot_contaminate_probe(self):
        for key,value in [('validation',True),('loaded_validation_layer','layer.dll')]:
            r=self.probe();r[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):v.validate_probe('upload','sdl',r,self.context)

    def test_cache_requires_new_packages_and_release_flags(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp);c=dict(CMAKE_GENERATOR='Visual Studio 17 2022',CMAKE_GENERATOR_PLATFORM='x64',CMAKE_CONFIGURATION_TYPES='Release',CMAKE_MSVC_RUNTIME_LIBRARY='MultiThreadedDLL',CMAKE_INTERPROCEDURAL_OPTIMIZATION='OFF',GFX_INSTALLED_SDK=str(root/'sdk-install'),SDL_BINARY_DIR=str(root/'sdl-build'),GraphicsSDK_DIR=str(root/'sdk-install/lib/cmake/GraphicsSDK'),SDL3_DIR=str(root/'sdl-build'))
            v.validate_cache(c,'upload',root)
            for key,value in [('GFX_INSTALLED_SDK',str(root/'old')),('SDL3_DIR',str(root/'old')),('CMAKE_MSVC_RUNTIME_LIBRARY','MultiThreaded'),('CMAKE_CONFIGURATION_TYPES','Debug;Release'),('CMAKE_INTERPROCEDURAL_OPTIMIZATION','ON')]:
                bad={**c,key:value}
                with self.subTest(key=key),self.assertRaises(ValueError):v.validate_cache(bad,'upload',root)

    def test_frozen_inputs_detect_content_changes_and_removal(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp)/'source';p.write_bytes(b'abc');rows=[v.record(p)];self.assertTrue(v.unchanged(rows));p.write_bytes(b'xyz');self.assertFalse(v.unchanged(rows));p.unlink();self.assertFalse(v.unchanged(rows))

    def test_sdl_explicitly_disabled_ltcg_is_accepted(self):
        v.validate_compiler_flags('/c /O2 /MD /GL- D:/SDK/GL/file.c')

    def test_enabled_ltcg_or_debug_runtime_rejected(self):
        for flags in ('/O2 /MD /GL','/O2 /MD /GL+','/Od /MD','/O2 /MDd','/O2 /MD /MDd'):
            with self.subTest(flags=flags),self.assertRaises(ValueError):v.validate_compiler_flags(flags)


if __name__=='__main__':unittest.main()
