import copy
import pathlib
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from run_vulkan_markers import validate_rows


def fixture(denied=False):
    names=[] if denied else ['slice','임시-marker-🙂','','m'*200,'inside-pass','reused']
    return [dict(event='adapter',adapter_id=42,software=True),
            dict(event='observer',debug_instances=2,lookups=1,calls=len(names),forwarded=len(names)),
            *[dict(event='native_label',index=i,command=1,text=n) for i,n in enumerate(names)],
            dict(event='result',source='source',validation=False,denied_proc=denied,observed=True,
                 adapters=1,accepted_markers=len(names),native_calls=len(names),forwarded=len(names),
                 assertions=30,observer_errors=0,live_instances=0,live_devices=0)]


class VulkanMarkerReceiptTests(unittest.TestCase):
    def test_supported_and_missing_proc_are_separate_complete_contracts(self):
        for denied in (False,True):validate_rows(fixture(denied),'source',False,denied,True)

    def test_stale_source_missing_observer_or_incomplete_result_rejected(self):
        for mutate in (lambda r:r[-1].update(source='old'),lambda r:r[-1].update(observed=False),
                       lambda r:r.pop(),lambda r:r[1].update(lookups=0),lambda r:r[-1].update(live_devices=1)):
            rows=fixture();mutate(rows)
            with self.assertRaises(ValueError):validate_rows(rows,'source',False,False)

    def test_label_truncation_reordering_and_unforwarded_calls_rejected(self):
        for mutate in (lambda r:r[2].update(text='slice-tail'),lambda r:r[3].update(index=0),
                       lambda r:r[-1].update(forwarded=0),lambda r:r[2].update(command=0)):
            rows=fixture();mutate(rows)
            with self.assertRaises(ValueError):validate_rows(rows,'source',False,False)

    def test_hardware_not_accepted_as_software_and_denial_cannot_hide_calls(self):
        rows=fixture();rows[0]['software']=False
        with self.assertRaises(ValueError):validate_rows(rows,'source',False,False,True)
        rows=fixture(True);rows[-1]['native_calls']=1
        with self.assertRaises(ValueError):validate_rows(rows,'source',False,True)


if __name__=='__main__':unittest.main()
