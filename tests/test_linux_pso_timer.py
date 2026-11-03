"""PSO timer adaptation and adversarial diagnostic checks; no performance acceptance."""
import copy
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import port_linux_pso_campaign as port
import linux_pso_timer_smoke as smoke


class PSOTimer(unittest.TestCase):
    def sources(self):return {n:(port.ROOT/n).read_text(encoding='utf-8') for n in port.OWN}

    def sample(self):return json.loads((port.ROOT/'tests/fixtures/LinuxPsoTimer.json').read_text(encoding='utf-8'))

    def check(self,v):return smoke.validate(v,'pso8',True,v['device_uuid'])

    def test_complete_lifecycle_and_timed_loop_preserved_in_both_modes(self):
        source=self.sources()
        for mode in ('physical','software-diagnostic'):
            out,_=port.translate(source,mode)
            old=source['benchmarks/llgl_vulkan_pso_compare/PSO.hpp']
            self.assertEqual(old[old.index('struct PSO {'):],out['PSO.hpp'][out['PSO.hpp'].index('struct PSO {'):])
            old=source['benchmarks/llgl_vulkan_pso_compare/main.cpp'];a=old.index('        for(unsigned i=0;');b=old.index('    auto runtime=modules();',a)
            self.assertIn(old[a:b],out['main.cpp'])
            self.assertIn('GFX_LLGL_PSO_COMPARE_KIND',out['Platform.hpp'])
            self.assertIn('P3_LBEN_03_completed',out['Platform.hpp'])
            self.assertEqual(source['benchmarks/llgl_vulkan_draw/Layer.cpp'].replace('__declspec(dllexport)','__attribute__((visibility("default")))'),out['Layer.cpp'])

    def test_generation_is_deterministic_mode_bound_confined_and_readonly(self):
        before=[port.sha(port.ROOT/n) for n in port.OWN]
        with tempfile.TemporaryDirectory(dir=port.ROOT/'out') as td:
            a=port.generate(pathlib.Path(td)/'a','physical');b=port.generate(pathlib.Path(td)/'b','software-diagnostic')
            self.assertEqual(a,port.generate(pathlib.Path(td)/'a','physical'))
            self.assertNotEqual(a['source_fingerprint'],b['source_fingerprint'])
            self.assertFalse(a['timed_loop_changed']);self.assertFalse(a['workload_bodies_changed'])
            self.assertEqual(set(a['generated']),set(port.OUTPUTS))
        self.assertEqual(before,[port.sha(port.ROOT/n) for n in port.OWN])
        with self.assertRaises(ValueError):port.generate(port.ROOT/'forbidden','physical')
        with self.assertRaises(ValueError):port.translate(self.sources(),'unknown')

    def test_platform_anchor_drift_is_rejected(self):
        source=self.sources()
        for name,anchor in [('Identity.hpp','unsigned number('),('PSO.hpp','#include "../vulkan_pso/Frame.hpp"'),('main.cpp','P3-CMP-09')]:
            changed=copy.deepcopy(source);key='benchmarks/llgl_vulkan_pso_compare/'+name;changed[key]=changed[key].replace(anchor,'changed')
            with self.subTest(name=name),self.assertRaises(ValueError):port.translate(changed,'physical')

    def test_real_software_fixture_matches_independent_reference_and_native_counts(self):
        self.check(self.sample())

    def test_full_image_geometry_and_lifecycle_tampering_rejected(self):
        for key,value in [('frame_sha256',[]),('scissor',[0,0,65,37]),('created_pipelines',31),('released_pipeline_handles',31),('created_shader_handles',63),('used_pipeline_regions',31),('verified_bytes',1),('GPU_use_in_timing',True),('driver_cache_state_known',True),('native_pipeline_count_verified',True)]:
            v=self.sample();v[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(v)

    def test_native_counter_and_module_tampering_rejected(self):
        for key,value in [('native_draw_calls',None),('native_forwarded_draw_calls',31),('native_instances',31),('native_vertices',95),('native_min_instances',0),('observer_live_devices_after_close',1),('instance_API_request_observed',False),('observed_instance_api_version',0)]:
            v=self.sample();v[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check(v)
        v=self.sample();v['runtime_modules'].append('/tmp/libVkLayer_khronos_validation.so');v['runtime_modules'].sort()
        with self.assertRaises(ValueError):self.check(v)

    def test_invalid_cpu_intervals_and_boolean_counts_rejected(self):
        for row in ([float('nan'),1],[-1,1],[True,1],[1],[1,float('inf')]):
            v=self.sample();v['samples_ms'][0]=row
            with self.subTest(row=row),self.assertRaises(ValueError):self.check(v)
        v=self.sample();v['sample_count']=True
        with self.assertRaises(ValueError):self.check(v)

    def test_plain_process_reports_unobserved_native_counters(self):
        v=self.sample();v.update(observer_enabled=False,frame_sha256=[],instance_API_request_observed=False,observed_instance_api_version=0)
        v['runtime_modules']=[x for x in v['runtime_modules'] if not x.endswith('/libVkLayer_gfx_llgl_draw.so')]
        for key in ('native_draw_calls','native_forwarded_draw_calls','native_instances','native_vertices','native_bad_arguments','native_min_instances','native_max_instances'):v[key]=None
        smoke.validate(v,'pso8',False,v['device_uuid'])
        v['native_draw_calls']=32
        with self.assertRaises(ValueError):smoke.validate(v,'pso8',False,v['device_uuid'])


if __name__=='__main__':unittest.main()
