"""CPU receipt rejection tests, not native Metal execution evidence."""
import copy
import pathlib
import sys
import unittest
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import macos_metal_backend as b


class MetalBackend(unittest.TestCase):
    def value(self):
        return dict(schema_version=1,contract='metal_submission_v1',SDK_source_fingerprint='a'*64,registry_ids=['0000000000000001'],
            default_registry_id='0000000000000001',completed_submissions=66,retired_pending_submissions=1,zero_poll_timed_out=True,assertions=111,
            **{k:v for k,v in b.SCOPE.items() if not k.startswith('P')})

    def test_submission_receipt_accepts_both_observed_zero_poll_outcomes(self):
        for outcome in (True,False):
            value=self.value();value['zero_poll_timed_out']=outcome;b.result(value,'a'*64,['0000000000000001'])

    def test_incomplete_or_overclaimed_native_execution_rejected(self):
        for key,value in [('completed_submissions',65),('retired_pending_submissions',0),('assertions',True),('assertions',99),
            ('SDK_source_fingerprint','b'*64),('zero_poll_timed_out',0),('default_registry_id','0000000000000002'),
            ('physical_GPU_accepted',True),('native_validation_accepted',True),('native_error_injected',True),
            ('resource_or_draw_executed',True),('SDK_Metal_submission_executed',False)]:
            bad=self.value();bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):b.result(bad,'a'*64,['0000000000000001'])

    def xml(self):
        return '<testsuite>'+''.join(f'<testcase name="{name}" status="run" time="0.1"/>' for name in (*b.core.TESTS,'metal_submission_contracts'))+'</testsuite>'

    def test_all_cpu_and_metal_ctests_must_execute_without_skip(self):
        b.junit(self.xml())
        for old,new in [('metal_submission_contracts','core_contracts'),('status="run"','status="notrun"'),('time="0.1"','time="nan"'),
            ('time="0.1"','time="-1"'),('/>','><skipped/></testcase>'),('/>','><failure/></testcase>')]:
            with self.subTest(new=new),self.assertRaises(ValueError):b.junit(self.xml().replace(old,new,1))

    def graph(self):
        rows=[]
        for name in b.UNITS:
            command='/Xcode/clang++ -arch arm64 -isysroot /SDK -O2 -DNDEBUG -std=c++23 '
            if name.startswith('src/'):command+='-DGFX_HAS_METAL=1 '
            if name=='src/backends/metal/Metal.cpp':command+='-x objective-c++ -fobjc-arc '
            rows.append(dict(file='/source/'+name,command=command+'-c /source/'+name))
        return rows

    def test_exact_native_compiler_graph_with_one_private_objcxx_unit(self):
        self.assertEqual(len(b.UNITS),18);b.graph(self.graph(),'/source','arm64','/SDK')
        rows=self.graph()
        for row in rows:
            if '/tests/' in row['file']:row['command']=row['command'].replace('-std=c++23','-std=gnu++23')
        b.graph(rows,'/source','arm64','/SDK')

    def test_backend_architecture_optimization_and_language_drift_rejected(self):
        for old,new in [('arm64','x86_64'),('/SDK','/other'),('-O2','-O3'),('-std=c++23','-std=c++17'),
            ('-fobjc-arc',''),('objective-c++','c++'),('-DGFX_HAS_METAL=1','-DGFX_HAS_VULKAN=1')]:
            rows=copy.deepcopy(self.graph());rows=[dict(r,command=r['command'].replace(old,new)) for r in rows]
            with self.subTest(new=new),self.assertRaises(ValueError):b.graph(rows,'/source','arm64','/SDK')
        with self.assertRaises(ValueError):b.graph(self.graph()[:-1],'/source','arm64','/SDK')
        rows=self.graph();rows[0]['command']=rows[0]['command'].replace('-std=c++23','-std=gnu++23')
        with self.assertRaises(ValueError):b.graph(rows,'/source','arm64','/SDK')

    def test_no_non_macos_fallback(self):
        for system in ('Linux','Windows'):
            with mock.patch.object(b.platform,'system',return_value=system),self.assertRaises(ValueError):b.run(SimpleNamespace())

    def test_roadmap_and_physical_acceptance_not_promoted(self):
        self.assertTrue(b.SCOPE['SDK_Metal_submission_executed'])
        self.assertFalse(any(v for k,v in b.SCOPE.items() if k!='SDK_Metal_submission_executed'))


if __name__=='__main__':unittest.main()
