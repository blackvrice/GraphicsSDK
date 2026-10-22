import importlib.util
import pathlib
import sys
import unittest

TOOLS = pathlib.Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("adapter_validation", TOOLS / "run_adapter_validation.py")
runner = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(runner)
from test_presentation import valid_events


def adapter(identity=7, software=False):
    return {"event":"adapter", "adapter_id":identity, "name":"test adapter", "software":software,
            "vendor_id":1, "device_id":2, "dedicated_video_bytes":0, "shared_system_bytes":1000}


class AdapterEvidenceTests(unittest.TestCase):
    def test_unique_metadata_with_real_zero_id(self):
        entries=[adapter(0),adapter(7,True)]
        events=entries+[{"event":"adapter_list", "count":2, "sdk_fingerprint":"f"*64}]
        self.assertEqual(runner.validate_list(events,"f"*64),entries)

    def test_missing_or_stale_list_footer_fails(self):
        for events in ([], [adapter()], [adapter(),{"event":"adapter_list","count":2,"sdk_fingerprint":"f"*64}],
                       [adapter(),{"event":"adapter_list","count":1,"sdk_fingerprint":"a"*64}]):
            with self.subTest(events=events), self.assertRaises(ValueError): runner.validate_list(events,"f"*64)

    def test_duplicate_identity_fails(self):
        with self.assertRaises(ValueError):
            runner.validate_list([adapter(),adapter(),{"event":"adapter_list","count":2,"sdk_fingerprint":"f"*64}],"f"*64)

    def test_malformed_id_and_unavailable_metadata_are_not_zero(self):
        for change in ({"adapter_id":-1},{"adapter_id":2**64},{"adapter_id":True},{"software":"false"},
                       {"dedicated_video_bytes":None},{"shared_system_bytes":-1}):
            entry=adapter();entry.update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                runner.validate_list([entry,{"event":"adapter_list","count":1,"sdk_fingerprint":"f"*64}],"f"*64)

    def selected(self, software=False):
        entry=adapter(7,software)
        events=valid_events("warp" if software else "validation",False)
        events[0].update(adapter=entry["name"],adapter_id=7);events[-1]["adapter_id"]=7
        events[1:1]=[{"event":"recovered","adapter_id":7},{"event":"recovered","adapter_id":7}]
        return entry,events

    def test_hardware_and_warp_exact_selection(self):
        for software in (False,True):
            entry,events=self.selected(software);runner.validate_selection(events,entry)

    def test_initial_and_final_wrong_id_fail(self):
        for index in (0,-1):
            entry,events=self.selected();events[index]["adapter_id"]=8
            with self.assertRaises(ValueError):runner.validate_selection(events,entry)

    def test_same_name_is_not_identity(self):
        entry,events=self.selected();entry["adapter_id"]=8
        with self.assertRaises(ValueError):runner.validate_selection(events,entry)

    def test_recovery_fallback_is_rejected(self):
        entry,events=self.selected();events[1]["adapter_id"]=8
        with self.assertRaises(ValueError):runner.validate_selection(events,entry)

    def test_missing_recovery_receipt_fails(self):
        entry,events=self.selected();del events[1]
        with self.assertRaises(ValueError):runner.validate_selection(events,entry)

    def test_wrong_validation_or_software_mode_fails(self):
        entry,events=self.selected();events[0]["gbv"]=False
        with self.assertRaises(ValueError):runner.validate_selection(events,entry)
        entry,events=self.selected(True);entry["software"]=False
        with self.assertRaises(ValueError):runner.validate_selection(events,entry)


if __name__ == "__main__":unittest.main()
