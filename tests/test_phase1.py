import copy
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_phase1 as p
import benchmark_telemetry as t
from phase1_acceptance import evaluate


class Phase1Contracts(unittest.TestCase):
    def test_sampled_memory_or_trace_filename_cannot_complete_phase1(self):
        evidence={k:True for k in ('advanced_native_cpu_references','repeated_advanced_observations','application_cache_cold_warm_hit_miss','matching_advanced_sdk_comparisons')}
        for trace in ({},{'status':'checkpoint_maximum'},{'status':'verified','etl':'trace.etl'},{'status':'verified','event_loss':1,'allocation_lifetime_reconciled':True,'transient_positive_control':True,'process_filtered_event_sha256':'a','raw_etl_sha256':'b'}):
            result=evaluate({**evidence,'gpu_memory_trace':trace});self.assertFalse(result['phase1_complete']);self.assertIn('continuous_transient_gpu_memory',result['unmet'])
        trace={'status':'verified','event_loss':0,'allocation_lifetime_reconciled':True,'transient_positive_control':True,'process_filtered_event_sha256':'a'*64,'raw_etl_sha256':'b'*64}
        self.assertTrue(evaluate({**evidence,'gpu_memory_trace':trace})['phase1_complete'])
        self.assertFalse(evaluate({**evidence,'matching_advanced_sdk_comparisons':False,'gpu_memory_trace':trace})['phase1_complete'])
    def result(self,name='compute_buffer'):
        point={key:100 for key in t.MEMORY_FIELDS}
        return {'schema_version':1,'sdk':'llgl','api':'d3d12','category':'phase1_extension','scenario':name,
                'warmup':3,'validation':True,'readback_verified':True,'reference_verified':True,'reference_kind':'full_gpu_readback_CPU_reference',
                'draw_calls':0,'dispatch_calls':1,'payload_bytes':256*256*4,'reference_checks':2,'samples':[{'frame_ms':2.,'gpu_ms':1.}],
                'gpu_scope':p.GPU_SCOPE,'cache_stats':None,'contract':'explicit workload','environment':{'luid':'1:2'},
                'telemetry':{'revision':1,'scope':t.SCOPE,'gpu_node':0,'gpu_luid':'1:2','cpu_normalization':t.NORMALIZATION,
                             'wall_ms':2.,'user_ms':1.,'kernel_ms':0.,'cpu_ms':1.,'cpu_one_core_percent':50.,
                             'checkpoints':[{**point,'phase':'baseline','sample':0},{**point,'phase':'sample_end','sample':0}]}}

    def test_both_cpu_references_and_real_completed_gpu_results_are_mandatory(self):
        good=self.result();p.validate(good,'compute_buffer',1,True)
        for change in ({'reference_checks':1},{'readback_verified':False},{'reference_verified':1},{'schema_version':True},{'gpu_scope':'CPU_wait'},{'samples':[]}):
            with self.subTest(change=change),self.assertRaises(ValueError):p.validate({**good,**change},'compute_buffer',1,True)
        for key in ('frame_ms','gpu_ms'):
            for value in (None,True,-1,float('nan'),float('inf')):
                bad=copy.deepcopy(good);bad['samples'][0][key]=value
                with self.subTest(key=key,value=value),self.assertRaises(ValueError):p.validate(bad,'compute_buffer',1,True)

    def test_video_composition_rejects_a_solid_overlay_or_wrong_plane_payload(self):
        good=self.result('video_layers');good.update(draw_calls=2,dispatch_calls=0,payload_bytes=1920*1080*3,contract='H_two_independent_NV12_videos_alpha')
        p.validate(good,'video_layers',1,True)
        for change in ({'contract':'one_video_solid_overlay'},{'payload_bytes':1920*1080*3//2},{'draw_calls':1}):
            with self.assertRaises(ValueError):p.validate({**good,**change},'video_layers',1,True)

    def test_cache_counters_prove_actual_application_layer_and_include_warmup(self):
        for name,hit,layer in [('shader_warm',True,'application_shader_bytecode'),('shader_cold',False,'application_shader_bytecode'),('pso_hit',True,'application_pso_handle'),('pso_miss',False,'application_pso_handle')]:
            good=self.result(name);good['gpu_scope']='disabled';good['samples'][0]['gpu_ms']=None
            good.update(draw_calls=0,dispatch_calls=0,payload_bytes=0)
            if name.startswith('shader_'):good.update(readback_verified=None,reference_kind='compiler_binary_exact_match')
            good['cache_stats']={'hits':5 if hit else 0,'misses':0 if hit else 5,'layer':layer};p.validate(good,name,1,True)
            for stats in (None,{'hits':1,'misses':0,'layer':layer},{**good['cache_stats'],'layer':'native_driver_cache'},{**good['cache_stats'],'hits':True}):
                with self.subTest(name=name,stats=stats),self.assertRaises(ValueError):p.validate({**good,'cache_stats':stats},name,1,True)

    def test_other_sdk_adapters_are_not_silently_replaced_with_llgl_or_native(self):
        support=p.support()
        self.assertEqual(len(support['llgl']),22)
        for sdk in ('graphicssdk','sdl','bgfx','diligent_core','the_forge'):
            self.assertTrue(all(v=='adapter_not_implemented_in_this_harness' for v in support[sdk].values()))
        with tempfile.TemporaryDirectory() as directory:
            out=pathlib.Path(directory)/'absent'
            child=subprocess.run([sys.executable,str(p.ROOT/'tools/run_phase1.py'),'--sdk','graphicssdk','--output',str(out)],capture_output=True,text=True)
            self.assertEqual(child.returncode,2);self.assertFalse(out.exists())

    def test_registry_matches_compiled_cli_and_ctest_scenarios(self):
        source=(p.ROOT/'benchmarks/phase1/LLGLSuite.cpp').read_text()
        block=source.split('constexpr std::array scenarioNames{',1)[1].split('};',1)[0]
        native=json.loads('['+block+']');self.assertEqual(tuple(native),p.SCENARIOS)
        cmake=(p.ROOT/'benchmarks/CMakeLists.txt').read_text()
        self.assertTrue(all(name in cmake for name in native))


if __name__=='__main__':unittest.main()
