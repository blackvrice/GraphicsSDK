"""Synthetic texture reference rejection tests; no native/GPU execution."""
import pathlib
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import macos_metal_backend as b
import macos_metal_textures as t


class MetalTextures(unittest.TestCase):
    def test_catalog_covers_four_formats_clear_load_depth_and_eight_mrt(self):
        rows=t.catalog();self.assertEqual(len(rows),44);self.assertEqual(len({r['name'] for r in rows}),44)
        self.assertEqual({r['format'] for r in rows},{0,1,2,3})
        self.assertEqual(len([r for r in rows if r['name'].startswith('mrt-8-')]),9)
        for row in rows:self.assertEqual(row['row_pitch']%256,0);t.verify_bytes(row,t.reference(row))

    def test_every_texel_and_final_row_padding_are_checked(self):
        row=next(r for r in t.catalog() if r['name']=='roundtrip-1-65x17.bin');data=t.reference(row)
        for index in (0,row['row_bytes'],len(data)-1):
            bad=bytearray(data);bad[index]^=1
            with self.assertRaises(ValueError):t.verify_bytes(row,bytes(bad))
        with self.assertRaises(ValueError):t.verify_bytes(row,data[:-1])

    def test_float_nan_depth_and_bgra_swizzle_corruption_rejected(self):
        for name,change in [('clear-1-2.bin',lambda a:struct.pack_into('<H',a,0,0x7e00)),
            ('depth-0.bin',lambda a:struct.pack_into('<f',a,0,float('nan'))),('clear-3-0.bin',lambda a:a.__setitem__(0,64))]:
            row=next(r for r in t.catalog() if r['name']==name);data=bytearray(t.reference(row));change(data)
            with self.subTest(name=name),self.assertRaises(ValueError):t.verify_bytes(row,bytes(data))

    def test_all_44_payload_files_required_without_extras(self):
        with tempfile.TemporaryDirectory() as td:
            p=pathlib.Path(td)
            for row in t.catalog():(p/row['name']).write_bytes(t.reference(row))
            self.assertEqual(t.payloads(p),sum(r['size'] for r in t.catalog()))
            (p/'extra.bin').write_bytes(b'')
            with self.assertRaises(ValueError):t.payloads(p)

    def test_result_rejects_missing_cases_wrong_source_and_scope_promotion(self):
        value=dict(schema_version=1,contract='metal_texture_pass_v1',SDK_source_fingerprint='a'*64,images=44,passes=17,
            verified_bytes=sum(r['size'] for r in t.catalog()),assertions=10000,texture_transfer_executed=True,clear_load_pass_executed=True,
            draw_executed=False,native_validation_accepted=False,physical_GPU_accepted=False,performance_accepted=False)
        t.result(value,'a'*64)
        for key,v in [('images',43),('passes',0),('verified_bytes',0),('SDK_source_fingerprint','b'*64),('assertions',True),
            ('draw_executed',True),('native_validation_accepted',True),('physical_GPU_accepted',True)]:
            with self.subTest(key=key),self.assertRaises(ValueError):t.result(dict(value,**{key:v}),'a'*64)

    def test_v3_requires_texture_ctest_and_compilation(self):
        names=(*b.core.TESTS,'metal_submission_contracts','metal_buffer_contracts','metal_texture_pass_contracts')
        xml='<testsuite>'+''.join(f'<testcase name="{n}" status="run" time=".1"/>' for n in names)+'</testsuite>';b.junit(xml,3)
        with self.assertRaises(ValueError):b.junit(xml.replace('metal_texture_pass_contracts','metal_buffer_contracts'),3)
        rows=[]
        for n in b.UNITS_V3:
            command='/clang++ -arch arm64 -isysroot /SDK -O2 -DNDEBUG -std=c++23 '
            if n.startswith('src/'):command+='-DGFX_HAS_METAL=1 '
            if n.endswith('/metal/Metal.cpp'):command+='-x objective-c++ -fobjc-arc '
            rows.append(dict(file='/src/'+n,command=command+'-c /src/'+n))
        b.graph(rows,'/src','arm64','/SDK',3)
        with self.assertRaises(ValueError):b.graph(rows[:-1],'/src','arm64','/SDK',3)

    def test_legacy_receipts_are_not_promoted_to_texture_acceptance(self):
        self.assertEqual(len(b.OWN_V3),len(b.OWN_V2)+2);self.assertTrue(b.SCOPE_V3['SDK_Metal_textures_executed'])
        for k in ('P4_MTL_04_completed','native_validation_accepted','physical_GPU_accepted','performance_accepted','draw_executed'):self.assertIs(b.SCOPE_V3[k],False)
        self.assertNotIn('SDK_Metal_textures_executed',b.SCOPE_V2)


if __name__=='__main__':unittest.main()
