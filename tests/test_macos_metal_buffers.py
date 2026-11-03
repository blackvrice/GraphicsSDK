"""Adversarial buffer evidence checks; fixtures never establish native GPU execution."""
import copy
import pathlib
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import macos_metal_backend as b


class MetalBuffers(unittest.TestCase):
    def value(self):
        return dict(schema_version=1,contract='metal_buffer_transfer_v1',SDK_source_fingerprint='a'*64,
            full_cases=11,partial_cases=11,verified_bytes=2*sum(b.SIZES)+19*len(b.SIZES),assertions=150,
            buffer_transfer_executed=True,internal_tail_dispatch_executed=True,draw_executed=False,
            native_validation_accepted=False,physical_GPU_accepted=False,performance_accepted=False)

    def test_exact_full_and_partial_buffer_contracts(self):b.buffer_result(self.value(),'a'*64)

    def test_incomplete_stale_or_overclaimed_native_results_rejected(self):
        for key,value in [('SDK_source_fingerprint','b'*64),('full_cases',10),('partial_cases',0),('verified_bytes',0),('assertions',True),
            ('buffer_transfer_executed',False),('internal_tail_dispatch_executed',False),('draw_executed',True),
            ('native_validation_accepted',True),('physical_GPU_accepted',True),('performance_accepted',True)]:
            bad=self.value();bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):b.buffer_result(bad,'a'*64)

    def fixture(self,p):
        for n in b.SIZES:
            data=bytes((i*37+n*13+17)%251 for i in range(n))
            (p/f'full-{n}.bin').write_bytes(data);(p/f'partial-{n}.bin').write_bytes(data+b'\xd7'*19)

    def test_every_byte_of_all_22_payloads_is_checked(self):
        with tempfile.TemporaryDirectory() as td:
            p=pathlib.Path(td);self.fixture(p);self.assertEqual(b.payloads(p),self.value()['verified_bytes'])

    def test_payload_corruption_guard_overwrite_and_missing_extra_file_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p=pathlib.Path(td);self.fixture(p)
            for name in ('full-1048579.bin','partial-1.bin'):
                file=p/name;old=file.read_bytes();file.write_bytes(old[:-1]+bytes([old[-1]^1]))
                with self.subTest(name=name),self.assertRaises(ValueError):b.payloads(p)
                file.write_bytes(old)
            (p/'full-1.bin').rename(p/'wrong-name.bin')
            with self.assertRaises(ValueError):b.payloads(p)
            (p/'wrong-name.bin').rename(p/'full-1.bin');(p/'extra.bin').write_bytes(b'')
            with self.assertRaises(ValueError):b.payloads(p)

    def test_v2_requires_both_native_metal_ctests_and_all_cpu_cases(self):
        names=(*b.core.TESTS,'metal_submission_contracts','metal_buffer_contracts')
        xml='<testsuite>'+''.join(f'<testcase name="{n}" status="run" time="0.1"/>' for n in names)+'</testsuite>'
        b.junit(xml,2)
        for old,new in [('metal_buffer_contracts','metal_submission_contracts'),('status="run"','status="notrun"'),('/>','><skipped/></testcase>')]:
            with self.assertRaises(ValueError):b.junit(xml.replace(old,new,1),2)
        with self.assertRaises(ValueError):b.junit(xml,1)

    def test_v2_compiler_graph_cannot_omit_buffer_unit(self):
        rows=[]
        for n in b.UNITS_V2:
            args='/clang++ -arch arm64 -isysroot /SDK -O2 -DNDEBUG -std=c++23 '
            if n.startswith('src/'):args+='-DGFX_HAS_METAL=1 '
            if n=='src/backends/metal/Metal.cpp':args+='-x objective-c++ -fobjc-arc '
            rows.append(dict(file='/src/'+n,command=args+'-c /src/'+n))
        b.graph(rows,'/src','arm64','/SDK',2)
        with self.assertRaises(ValueError):b.graph(rows[:-1],'/src','arm64','/SDK',2)
        with self.assertRaises(ValueError):b.graph(rows,'/src','arm64','/SDK',1)

    def test_buffer_scope_is_versioned_without_physical_or_roadmap_promotion(self):
        self.assertFalse(b.SCOPE['resource_or_draw_executed']);self.assertTrue(b.SCOPE_V2['resource_or_draw_executed'])
        self.assertEqual(len(b.OWN_V2),len(b.OWN)+1)
        for k in ('physical_GPU_accepted','performance_accepted','native_validation_accepted','P4_MTL_03_completed','draw_executed'):
            self.assertIs(b.SCOPE_V2[k],False)


if __name__=='__main__':unittest.main()
