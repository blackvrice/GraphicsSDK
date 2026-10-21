import copy
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_benchmarks as bench


class TimestampContracts(unittest.TestCase):
    def result(self):
        return {'sdk':'llgl','scenario':'draw_gpu','count':10000,'warmup':20,'validation':False,
                'api':'d3d12','category':'gpu_timestamp','configuration':'Release','readback_verified':True,
                'gpu_timestamp_scope':bench.GPU_SCOPE,'samples':[{'gpu_ms':.5,'frame_ms':1,'query_read_ms':.05}]}

    def validate(self,result): bench.validate_result(result,'llgl','draw_gpu',10000,20,1,False)

    def test_completed_gpu_query_is_required_and_finite(self):
        result=self.result(); self.validate(result)
        for invalid in (None,-1,float('nan'),float('inf'),True,'0.5'):
            for metric in ('gpu_ms','query_read_ms'):
                bad=copy.deepcopy(result); bad['samples'][0][metric]=invalid
                with self.assertRaises(RuntimeError): self.validate(bad)
        zero=copy.deepcopy(result);zero['samples'][0]['gpu_ms']=0;self.validate(zero)

    def test_missing_or_different_interval_and_cpu_wait_masquerade_are_rejected(self):
        for changes in ({'gpu_timestamp_scope':'whole_frame'},{'gpu_timestamp_scope':None},{'category':'end_to_end'},{'readback_verified':False}):
            with self.assertRaises(RuntimeError): self.validate({**self.result(),**changes})
        result=self.result(); del result['samples'][0]['gpu_ms']
        with self.assertRaises(RuntimeError): self.validate(result)
        result=self.result(); result['scenario']='draw'; result['category']='end_to_end'
        with self.assertRaises(RuntimeError): bench.validate_result(result,'llgl','draw',10000,20,1,False)

    def test_unsupported_adapters_fail_before_output_creation(self):
        for sdk in ('sdl','bgfx'):
            for scenario in bench.GPU_SCENARIOS:
                self.assertIn('matching',bench.scenario_error(sdk,scenario))
            with tempfile.TemporaryDirectory() as directory:
                output=pathlib.Path(directory)/'absent'
                result=subprocess.run([sys.executable,str(bench.ROOT/'tools/run_benchmarks.py'),'--sdks',sdk,'--scenarios','draw_gpu','--output',str(output)],capture_output=True,text=True)
                self.assertEqual(result.returncode,2);self.assertFalse(output.exists())

    def test_statistics_preserve_gpu_query_values_and_contract_separation(self):
        result=self.result(); summary=bench.summarize([result,result,result])
        self.assertEqual(summary['gpu_ms']['p50'],.5); self.assertEqual(summary['gpu_ms']['sample_count'],3)
        env={'cpu':'same','ram_bytes':100,'os':'same','luid':'same','driver':'same','software_renderer':False,'compiler':'same'}
        result.update(environment=env,contract={'gpu_timestamps':True,'timestamp_scope':bench.GPU_SCOPE})
        other=copy.deepcopy(result);self.assertTrue(bench.comparable(result,other))
        other['contract']['timestamp_scope']='whole_frame';self.assertFalse(bench.comparable(result,other))
        other=copy.deepcopy(result);other['category']='end_to_end';self.assertFalse(bench.comparable(result,other))

    def test_diligent_start_resolve_inside_interval_cannot_enter_common_comparison(self):
        contract=bench.timestamp_contract('graphicssdk')
        for sdk in ('llgl','the_forge'):self.assertEqual(contract,bench.timestamp_contract(sdk))
        diligent=bench.timestamp_contract('diligent_core')
        self.assertNotEqual(contract,diligent);self.assertIn('inside_interval',diligent['query_resolve'])
        result=self.result();result.update(environment={'cpu':'same','ram_bytes':100,'os':'same','luid':'same','driver':'same','software_renderer':False,'compiler':'same'},contract=contract)
        self.assertFalse(bench.comparable(result,{**result,'sdk':'diligent_core','contract':diligent}))


if __name__=='__main__': unittest.main()
