"""CPU rejection tests for full graphics evidence; no native GPU execution."""
import pathlib
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import macos_metal_backend as b
import macos_metal_graphics as g


class MetalGraphics(unittest.TestCase):
    def test_all_18_independent_draw_references(self):
        rows=g.catalog();self.assertEqual(len(rows),18);self.assertEqual(len({r['name'] for r in rows}),18)
        for row in rows:g.verify_bytes(row,g.reference(row))
        self.assertEqual(sum(r['size'] for r in rows),161024)

    def test_snapshot_scissor_boundary_and_padding_corruption_rejected(self):
        for name,x,y in [('snapshots.bin',31,0),('snapshots.bin',32,16),('blend-scissor.bin',4,3),('blend-scissor.bin',5,3)]:
            row=next(r for r in g.catalog() if r['name']==name);data=bytearray(g.reference(row));data[y*row['row_pitch']+4*x]^=128
            with self.subTest(name=name,x=x),self.assertRaises(ValueError):g.verify_bytes(row,bytes(data))
        row=g.catalog()[0];data=bytearray(g.reference(row));data[-1]=0
        with self.assertRaises(ValueError):g.verify_bytes(row,bytes(data))

    def test_depth_hdr_nan_and_sampler_mismatch_rejected(self):
        for name,mutate in [('mrt-depth.bin',lambda p:struct.pack_into('<f',p,0,float('nan'))),('hdr.bin',lambda p:struct.pack_into('<H',p,0,0x7e00)),('point-repeat.bin',lambda p:p.__setitem__(1,255))]:
            row=next(r for r in g.catalog() if r['name']==name);data=bytearray(g.reference(row));mutate(data)
            with self.subTest(name=name),self.assertRaises(ValueError):g.verify_bytes(row,bytes(data))

    def test_saved_payloads_require_every_image(self):
        with tempfile.TemporaryDirectory() as td:
            p=pathlib.Path(td)
            for row in g.catalog():(p/row['name']).write_bytes(g.reference(row))
            self.assertEqual(g.payloads(p),161024);(p/'index16.bin').unlink()
            with self.assertRaises(ValueError):g.payloads(p)

    def test_native_result_counts_source_and_acceptance_scope(self):
        value=dict(schema_version=1,contract='metal_graphics_v1',SDK_source_fingerprint='a'*64,images=18,recorded_draws=20,verified_bytes=161024,assertions=10000,draw_executed=True,native_validation_accepted=False,physical_GPU_accepted=False,performance_accepted=False)
        g.result(value,'a'*64)
        for k,v in [('images',17),('recorded_draws',19),('verified_bytes',0),('assertions',True),('SDK_source_fingerprint','b'*64),('draw_executed',False),('physical_GPU_accepted',True),('native_validation_accepted',True)]:
            with self.subTest(key=k),self.assertRaises(ValueError):g.result(dict(value,**{k:v}),'a'*64)

    def test_version_four_requires_graphics_ctest_and_compiler_unit(self):
        names=(*b.core.TESTS,'metal_submission_contracts','metal_buffer_contracts','metal_texture_pass_contracts','metal_graphics_contracts')
        xml='<testsuite>'+''.join(f'<testcase name="{n}" status="run" time=".1"/>' for n in names)+'</testsuite>';b.junit(xml,4)
        with self.assertRaises(ValueError):b.junit(xml.replace('metal_graphics_contracts','metal_texture_pass_contracts'),4)
        rows=[]
        for name in b.UNITS_V4:
            command='/clang++ -arch arm64 -isysroot /SDK -O2 -DNDEBUG -std=c++23 '
            if name.startswith('src/'):command+='-DGFX_HAS_METAL=1 '
            if name.endswith('/metal/Metal.cpp'):command+='-x objective-c++ -fobjc-arc '
            rows.append(dict(file='/src/'+name,command=command+'-c /src/'+name))
        b.graph(rows,'/src','arm64','/SDK',4)
        with self.assertRaises(ValueError):b.graph(rows[:-1],'/src','arm64','/SDK',4)

    def test_legacy_scopes_stay_without_draw_acceptance(self):
        self.assertFalse(b.SCOPE_V3['draw_executed']);self.assertTrue(b.SCOPE_V4['draw_executed']);self.assertEqual(len(b.OWN_V4),14)
        for k in ('physical_GPU_accepted','native_validation_accepted','performance_accepted','P4_MTL_05_completed'):self.assertIs(b.SCOPE_V4[k],False)


if __name__=='__main__':unittest.main()
