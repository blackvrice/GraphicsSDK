"""Synthetic Metal inventory controls; these tests do not query or execute a GPU."""
import copy
import pathlib
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import macos_metal_probe as m


class MetalProbe(unittest.TestCase):
    def value(self,count=0):
        rows=[dict(registry_id=f'{i+1:016x}',name='synthetic GPU '+str(i),low_power=False,removable=False,headless=True,
            unified_memory=True,max_buffer_length=1<<30,max_threads_per_threadgroup=[1024,1024,64]) for i in range(count)]
        return dict(schema_version=1,contract='native_macos_metal_inventory_v1',devices=rows,default_registry_id=rows[0]['registry_id'] if rows else None,
            selected_registry_id=None,framework_modules=['/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation',
            '/System/Library/Frameworks/Metal.framework/Versions/A/Metal'],GPU_work_submitted=False,SDK_backend_used=False)

    def test_empty_inventory_is_explicit_absence_not_gpu_success(self):
        self.assertEqual(m.inventory(self.value()),[])
        with self.assertRaises(ValueError):m.inventory(self.value(),'0000000000000001')

    def test_multiple_devices_and_exact_registry_selection(self):
        value=self.value(2);self.assertEqual(m.inventory(value),['0000000000000001','0000000000000002'])
        value['selected_registry_id']='0000000000000002';m.inventory(value,value['selected_registry_id'])
        with self.assertRaises(ValueError):m.inventory(value,'0000000000000001')

    def test_duplicate_invalid_unordered_and_missing_default_devices_rejected(self):
        base=self.value(2)
        for mutate in (lambda v:v['devices'][0].update(registry_id=1),lambda v:v['devices'][0].update(registry_id='0'*16),
            lambda v:v['devices'][1].update(registry_id=v['devices'][0]['registry_id']),lambda v:v['devices'].reverse(),
            lambda v:v.update(default_registry_id='f'*16),lambda v:v['devices'][0].update(name='')):
            value=copy.deepcopy(base);mutate(value)
            with self.assertRaises(ValueError):m.inventory(value)

    def test_capability_types_frameworks_and_gpu_claims_rejected_on_drift(self):
        for key,value in [('max_buffer_length',True),('max_buffer_length',0),('max_threads_per_threadgroup',[1024,0,64]),('headless',1)]:
            v=self.value(1);v['devices'][0][key]=value
            with self.assertRaises(ValueError):m.inventory(v)
        for key,value in [('framework_modules',[]),('GPU_work_submitted',True),('SDK_backend_used',True)]:
            v=self.value(1);v[key]=value
            with self.assertRaises(ValueError):m.inventory(v)

    def test_selection_controls_require_specific_failure_without_output(self):
        token=b'Requested Metal registry ID absent or ambiguous';m.rejection(1,b'',b'FAIL: '+token,token)
        for code,out,err in [(True,b'',token),(0,b'',token),(1,b'{}',token),(1,b'',b'unrelated')]:
            with self.assertRaises(ValueError):m.rejection(code,out,err,token)

    def test_only_known_missing_tool_diagnostics_mean_unavailable(self):
        for value in (b'xcrun: unable to find utility "metal"',b'Cannot execute tool due to missing Metal Toolchain'):
            self.assertTrue(m.unavailable(value))
        for value in (b'shader.metal: error: expected expression',b'compiler crashed',b'permission denied',b''):
            self.assertFalse(m.unavailable(value))

    def test_availability_receipt_cannot_promote_sdk_or_gpu_acceptance(self):
        with tempfile.TemporaryDirectory() as td:
            folder=pathlib.Path(td);base=dict(schema_version=1,scope='macos-metal-availability-v1',status='completed',inputs_unchanged=True,**m.SCOPE)
            for key in m.SCOPE:
                value=copy.deepcopy(base);value[key]=True;m.save(folder/'receipt.json',value)
                with self.assertRaisesRegex(ValueError,'scope/metadata'):m.check(folder)

    def test_native_probe_rejects_other_hosts_and_core_architecture_mismatch(self):
        args=SimpleNamespace(core=pathlib.Path('/core'),output=m.ROOT/'out/unused-metal-fixture')
        with mock.patch.object(m.platform,'system',return_value='Linux'):
            with self.assertRaises(ValueError):m.run(args)
        with mock.patch.object(m.platform,'system',return_value='Darwin'),mock.patch.object(m.platform,'machine',return_value='x86_64'),mock.patch.object(m.core,'check'),mock.patch.object(m,'read',return_value=dict(architecture='arm64')):
            with self.assertRaises(ValueError):m.run(args)


if __name__=='__main__':unittest.main()
