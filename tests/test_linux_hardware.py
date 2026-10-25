"""Synthetic hardware-gate controls; these are never physical GPU acceptance."""
import copy
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import linux_hardware as hardware

FP='a'*64
UUID='0123456789abcdef0123456789abcdef'


def identity():
    value=14695981039346656037
    for b in bytes.fromhex(UUID):value=((value^b)*1099511628211)&((1<<64)-1)
    return dict(schema_version=1,case='selection',SDK_source_fingerprint=FP,software_adapters=0,hardware_adapters=1,device_creations=2,
        adapters=[dict(name='Synthetic discrete GPU',id=str(value),device_type=2,vendor_id=0x10de,device_id=1,api_version_raw=4198400,driver_version_raw=1,device_uuid=UUID,driver_uuid='b'*32)],
        rejections=[dict(case=k) for k in ('software-kind-mismatch','backend-id-mismatch','missing-exact-id-no-fallback','zero-id-no-fallback','uncompiled-backend-enumeration','unsupported-loss-opt-in')])


class PhysicalGateContracts(unittest.TestCase):
    def check(self,row,pci=None,expected=None):
        return hardware.physical(row,[UUID] if expected is None else expected,FP,[dict(vendor_id=0x10de,device_id=1,driver='nvidia')] if pci is None else pci)
    def reject(self,mutate):
        row=identity();mutate(row)
        with self.assertRaises(ValueError):self.check(row)
    def test_synthetic_gate_model(self):self.assertEqual(len(self.check(identity())),1)
    def test_cpu_and_software_rejected(self):self.reject(lambda r:r.update(software_adapters=1,hardware_adapters=0))
    def test_virtual_and_other_kinds_rejected(self):
        for kind in (0,3,4,True):
            with self.subTest(kind=kind):self.reject(lambda r:r['adapters'][0].update(device_type=kind))
    def test_source_mismatch_rejected(self):self.reject(lambda r:r.update(SDK_source_fingerprint='0'*64))
    def test_numeric_large_adapter_id_rejected(self):self.reject(lambda r:r['adapters'][0].update(id=int(r['adapters'][0]['id'])))
    def test_wrong_exact_id_rejected(self):self.reject(lambda r:r['adapters'][0].update(id='1'))
    def test_missing_driver_uuid_rejected(self):self.reject(lambda r:r['adapters'][0].update(driver_uuid='0'*32))
    def test_boolean_metadata_rejected(self):self.reject(lambda r:r['adapters'][0].update(vendor_id=True))
    def test_missing_pci_provenance_rejected(self):
        with self.assertRaises(ValueError):self.check(identity(),pci=[])
    def test_virtual_pci_driver_rejected(self):
        with self.assertRaises(ValueError):self.check(identity(),pci=[dict(vendor_id=0x10de,device_id=1,driver='virtio-pci')])
    def test_uuid_set_mismatch_rejected(self):
        for expected in ([],[UUID,UUID],['c'*32]):
            with self.subTest(expected=expected),self.assertRaises(ValueError):self.check(identity(),expected=expected)
    def test_missing_exact_creation_controls_rejected(self):self.reject(lambda r:r.update(device_creations=1))
    def test_missing_selection_rejections_rejected(self):self.reject(lambda r:r.update(rejections=[]))
    def test_workload_and_validation_are_required(self):
        row=identity();text='source='+FP+' validation=1\nadapter=Synthetic discrete GPU id='+row['adapters'][0]['id']+'\nPASS assertions=361 transfers=24 verified_bytes=28521273 adapters=1\n'
        self.assertEqual(hardware.suite_result('buffer',text,row,FP)['transfers'],24)
        for bad in (text.replace('validation=1','validation=0'),text.replace('transfers=24','transfers=23'),text.replace('adapters=1','adapters=2'),text.replace('id='+row['adapters'][0]['id'],'id=1')):
            with self.subTest(text=bad),self.assertRaises(ValueError):hardware.suite_result('buffer',bad,row,FP)


if __name__=='__main__':unittest.main()
