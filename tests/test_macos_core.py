"""Adversarial CPU-only macOS receipt tests; synthetic fixtures are not Mac evidence."""
import copy
import json
import pathlib
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import macos_core as c


class MacCore(unittest.TestCase):
    def xml(self):return '<testsuite>'+''.join(f'<testcase name="{name}" status="run" time="0.1"/>' for name in c.TESTS)+'</testsuite>'

    def test_all_eleven_cpu_ctests_must_really_run(self):
        self.assertEqual(len(c.TESTS),11);c.junit(self.xml())

    def test_missing_duplicate_skipped_failed_and_nonfinite_ctests_rejected(self):
        xml=self.xml()
        cases=[xml.replace('<testcase name="core_contracts" status="run" time="0.1"/>',''),
            xml.replace('adapter_contracts','core_contracts'),xml.replace('status="run"','status="notrun"',1),
            xml.replace('time="0.1"','time="nan"',1),xml.replace('time="0.1"','time="-1"',1)]
        cases += [xml.replace('time="0.1"/>',f'time="0.1"><{kind}/></testcase>',1) for kind in ('skipped','error','failure')]
        for value in cases:
            with self.subTest(value=value[:100]),self.assertRaises(ValueError):c.junit(value)

    def graph(self,arch):
        return [dict(file='/source/'+name,command=f'/Xcode/clang++ -arch {arch} -isysroot /SDK -O2 -DNDEBUG -std=c++23 -c /source/{name}') for name in c.UNITS]

    def test_exact_core_tests_examples_graph_on_both_architectures(self):
        self.assertEqual(len(c.UNITS),18)
        for arch in ('arm64','x86_64'):c.compile_graph(self.graph(arch),['/source/'+n for n in c.UNITS],arch,'/SDK')

    def test_backend_flags_arch_sdk_and_missing_compilation_rejected(self):
        graph=self.graph('arm64');units=['/source/'+n for n in c.UNITS]
        for replacement in ('-DGFX_HAS_VULKAN=1','-DGFX_HAS_D3D12=1','-flto','-O3'):
            bad=copy.deepcopy(graph);bad[0]['command']+=' '+replacement
            with self.assertRaises(ValueError):c.compile_graph(bad,units,'arm64','/SDK')
        for old,new in (('arm64','x86_64'),('/SDK','/Other'),('-std=c++23','-std=c++17'),('-DNDEBUG',''),('-O2','-O0')):
            bad=copy.deepcopy(graph);bad[0]['command']=bad[0]['command'].replace(old,new)
            with self.assertRaises(ValueError):c.compile_graph(bad,units,'arm64','/SDK')
        with self.assertRaises(ValueError):c.compile_graph(graph[:-1],units,'arm64','/SDK')
        bad=copy.deepcopy(graph);bad[0]['file']=bad[1]['file']
        with self.assertRaises(ValueError):c.compile_graph(bad,units,'arm64','/SDK')

    def python_fixture(self,folder):
        (folder/'unittest.log').write_text('fixture: no actual test execution\n',encoding='utf-8')
        r=dict(source_commit='a'*40,SDK_source_fingerprint='b'*64,architecture='arm64',source_inputs=[dict(path='tools/macos_core.py',sha256='c'*64)])
        p=dict(source_commit=r['source_commit'],SDK_source_fingerprint=r['SDK_source_fingerprint'],successful=True,inputs_unchanged=True,
            failures=[],errors=[],skipped=[],expected_failures=[],unexpected_successes=[],native_GPU_executed_by_this_Python_suite=False,
            memory_or_Intel_investigation_resumed=False,platform=dict(system='Darwin',machine='arm64'),tests=10,
            log=dict(sha256=c.sha(folder/'unittest.log')),inputs=[dict(path='tools/macos_core.py',sha256='c'*64)])
        return r,p

    def test_python_source_architecture_and_executed_runner_binding(self):
        with tempfile.TemporaryDirectory() as td:
            folder=pathlib.Path(td);r,p=self.python_fixture(folder);c.save(folder/'receipt.json',p)
            self.assertEqual(c.validate_python(folder,r),10)

    def test_python_failures_skips_gpu_claims_source_and_code_drift_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            folder=pathlib.Path(td);r,p=self.python_fixture(folder)
            for key,value in [('source_commit','d'*40),('SDK_source_fingerprint','d'*64),('successful',False),('inputs_unchanged',False),
                ('tests',True),('tests',0),('skipped',[dict(test='x')]),('native_GPU_executed_by_this_Python_suite',True)]:
                bad=copy.deepcopy(p);bad[key]=value;c.save(folder/'receipt.json',bad)
                with self.subTest(key=key),self.assertRaises(ValueError):c.validate_python(folder,r)
            for mutate in (lambda p:p['platform'].update(machine='x86_64'),lambda p:p['platform'].update(system='Linux'),lambda p:p['inputs'][0].update(sha256='d'*64)):
                bad=copy.deepcopy(p);mutate(bad);c.save(folder/'receipt.json',bad)
                with self.assertRaises(ValueError):c.validate_python(folder,r)

    def test_python_log_corruption_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            folder=pathlib.Path(td);r,p=self.python_fixture(folder);c.save(folder/'receipt.json',p)
            (folder/'unittest.log').write_text('corrupt',encoding='utf-8')
            with self.assertRaises(ValueError):c.validate_python(folder,r)

    def test_native_execution_cannot_fall_back_to_another_os_or_architecture(self):
        args=SimpleNamespace(architecture='arm64',output=c.ROOT/'out/unused-mac-fixture')
        for system,machine in (('Linux','arm64'),('Windows','AMD64'),('Darwin','x86_64')):
            with mock.patch.object(c.platform,'system',return_value=system),mock.patch.object(c.platform,'machine',return_value=machine):
                with self.assertRaises(ValueError):c.run(args)

    def test_cpu_receipt_cannot_promote_gpu_or_phase_acceptance(self):
        with tempfile.TemporaryDirectory() as td:
            folder=pathlib.Path(td)
            base=dict(schema_version=1,scope='macos-core-cpu-v1',status='completed',inputs_unchanged=True,system='Darwin',pointer_bits=64,**c.SCOPE)
            for key in c.SCOPE:
                bad=copy.deepcopy(base);bad[key]=True;c.save(folder/'receipt.json',bad)
                with self.subTest(key=key),self.assertRaisesRegex(ValueError,'scope/metadata'):c.check(folder)


if __name__=='__main__':unittest.main()
