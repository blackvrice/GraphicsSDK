import copy
import pathlib
import sys
import unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_vulkan_recovery import validate_rows


def fixture(count=1):
    adapters=[dict(event='adapter',adapter_id=i+42,software=True) for i in range(count)]
    cases=[dict(event='cycle',adapter_id=a['adapter_id'],mode=m,automatic=False,passed=True) for a in adapters for m in (1,2,3)]
    cases.append(dict(event='cycle',adapter_id=42,mode=1,automatic=True,passed=True))
    for i,c in enumerate(cases):c.update(old_generation=2*i+1,fresh_generation=2*i+2)
    cycles=len(cases);images=2*cycles
    return adapters+cases+[dict(event='result',source='source',validation=False,synthetic_loss=True,actual_TDR=False,adapters=count,cycles=cycles,
        images=images,window_images=0,verified_bytes=images*33*17*4,verified_buffer_bytes=2*cycles*4097,created=2*cycles,destroyed=2*cycles,
        failed_creates=2*cycles,drains=cycles,errors=0,premature_creations=0,live_instances=0,live_devices=0,assertions=100,reversed_enumerations=3 if count>1 else 0)]


class VulkanRecoveryReceiptTests(unittest.TestCase):
    def test_single_and_multiple_adapter_recovery(self):
        for count in (1,2):validate_rows(fixture(count),'source',False,True)

    def test_generations_cannot_alias_or_be_reused(self):
        for mutate in (lambda r:r[1].update(fresh_generation=r[1]['old_generation']),
                       lambda r:r[2].update(old_generation=r[1]['old_generation'])):
            rows=fixture();mutate(rows)
            with self.assertRaises(ValueError):validate_rows(rows,'source',False)

    def test_fault_modes_automatic_policy_and_reorder_must_be_exercised(self):
        for mutate in (lambda r:r[3].update(mode=2),lambda r:r[4].update(automatic=False)):
            rows=fixture();mutate(rows)
            with self.assertRaises(ValueError):validate_rows(rows,'source',False)
        rows=fixture(2);rows[-1]['reversed_enumerations']=0
        with self.assertRaises(ValueError):validate_rows(rows,'source',False)

    def test_cleanup_order_and_failed_retry_counts_cannot_be_dropped(self):
        for key,value in (('destroyed',0),('failed_creates',0),('live_devices',1),('errors',1),('premature_creations',1)):
            rows=fixture();rows[-1][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):validate_rows(rows,'source',False)

    def test_stale_source_actual_tdr_claim_and_software_alias_rejected(self):
        for key,value in (('source','old'),('actual_TDR',True),('synthetic_loss',False),('validation',True)):
            rows=fixture();rows[-1][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):validate_rows(rows,'source',False)
        rows=fixture();rows[0]['software']=False
        with self.assertRaises(ValueError):validate_rows(rows,'source',False,True)

    def test_full_gpu_image_and_buffer_references_required(self):
        for key in ('images','verified_bytes','verified_buffer_bytes'):
            rows=fixture();rows[-1][key]-=1
            with self.subTest(key=key),self.assertRaises(ValueError):validate_rows(rows,'source',False)
        rows=fixture();rows.pop()
        with self.assertRaises(ValueError):validate_rows(rows,'source',False)


if __name__=='__main__':unittest.main()
