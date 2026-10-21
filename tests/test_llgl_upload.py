import copy
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import run_benchmarks as bench


class LLGLUploadContracts(unittest.TestCase):
    def test_adapter_identity_changes_with_pin_harness_or_copy_layout(self):
        with tempfile.TemporaryDirectory() as folder:
            root=pathlib.Path(folder); directory=root/'benchmarks/competitors'; directory.mkdir(parents=True)
            lock=directory/'sources.lock.json'; lock.write_text(json.dumps({'llgl':{'commit':'first'}}))
            files=[root/'benchmarks/CMakeLists.txt',directory/'LLGL.cpp',root/'benchmarks/UploadLayout.hpp']
            for path in files: path.write_text('first')
            before=bench.llgl_fingerprint(root)
            for path in files:
                path.write_text('changed'); self.assertNotEqual(before,bench.llgl_fingerprint(root)); path.write_text('first')
            lock.write_text(json.dumps({'llgl':{'commit':'second'}})); self.assertNotEqual(before,bench.llgl_fingerprint(root))

    def test_new_upload_and_telemetry_contracts_stay_separate_from_historical_data(self):
        env={'cpu':'same','ram_bytes':1,'os':'same','luid':'1:2','driver':'same','software_renderer':False,'compiler':'same'}
        current={'api':'d3d12','category':'end_to_end','scenario':'upload4k','count':1,'validation':False,
                 'configuration':'Release','environment':env,'contract':{'revision':5,**bench.upload_contract()}}
        self.assertTrue(bench.comparable(current,copy.deepcopy(current)))
        for revision in (2,4):
            self.assertFalse(bench.comparable(current,{**current,'contract':{'revision':revision}}))
        observed=copy.deepcopy(current); observed['contract']['telemetry']={'enabled':True}
        self.assertFalse(bench.comparable(current,observed))


if __name__=='__main__': unittest.main()
