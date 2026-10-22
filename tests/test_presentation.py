import copy
import importlib.util
import pathlib
import sys
import tempfile
import unittest

TOOLS = pathlib.Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("presentation_validation", TOOLS / "run_presentation_validation.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)
import audit_licenses as audit


def valid_events(mode="validation", vsync=True):
    extents = [(664,441)] * 2
    extents += [size for size in [(321,181),(641,359),(127,95),(803,451)] * 2 for _ in range(4)]
    extents += [size for size in [(127,1),(129,2),(127,3),(257,9)] for _ in range(2)]
    extents += [(639,357)] * 7
    images = [{"event":"image", "frame":i, "width":w, "height":h, "row_pitch":((w*4+255)//256)*256}
              for i, (w,h) in enumerate(extents)]
    return [{"event":"device", "adapter":"Test adapter", "warp":mode=="warp", "gbv":mode=="validation", "vsync":vsync},
            *images,
            *[{"event":"minimize_restore", "cycle":i, "paused_submissions":0} for i in range(8)],
            {"event":"tiny_extents", "cases":4, "full_images":8},
            {"event":"coalesced_resize", "events":3, "gpu_resizes":1},
            {"event":"observed_loss", "native_reason":-2005270523},
            {"event":"observed_loss", "native_reason":-2005270523},
            {"event":"minimized_loss_restore", "paused_submissions":0, "recovery_cycles":1},
            {"event":"hide_restore", "paused_submissions":0},
            {"event":"shutdown", "sdk_objects":0, "sdk_resources":0, "pending_submissions":0, "hwnd_alive":True},
            {"event":"result", "functional_passed":True, "frames":49, "full_images":49,
             "verified_pixels":sum(w*h for w,h in extents), "resizes":13, "minimize_restore_cycles":9,
             "paused_checks":26, "recovery_cycles":2, "validation_errors":0 if mode=="validation" else None,
             "per_monitor_v2":True, "interactive_acceptance":False, "unexpected_loss_acceptance":False}]


class LifecycleEvidenceTests(unittest.TestCase):
    def test_tiny_extent_coverage_cannot_be_dropped(self):
        events = valid_events()
        next(e for e in events if e["event"]=="tiny_extents")["cases"]=0
        with self.assertRaises(ValueError):
            runner.validate_events(events, "validation", True)
    def test_six_modes_and_null_for_unavailable_validation(self):
        for mode in ("gpu", "validation", "warp"):
            for vsync in (True, False):
                runner.validate_events(valid_events(mode, vsync), mode, vsync)

    def test_scope_cannot_be_upgraded_to_interactive_or_unexpected_loss(self):
        for key in ("interactive_acceptance", "unexpected_loss_acceptance"):
            events = valid_events(); events[-1][key] = True
            with self.subTest(key=key), self.assertRaises(ValueError):
                runner.validate_events(events, "validation", True)

    def test_missing_footer_image_or_loss_is_not_success(self):
        events = valid_events()
        for index in (-1, 1, next(i for i,e in enumerate(events) if e["event"]=="observed_loss")):
            changed = copy.deepcopy(events); del changed[index]
            with self.subTest(index=index), self.assertRaises(ValueError):
                runner.validate_events(changed, "validation", True)

    def test_errors_unavailable_validation_and_wrong_mode_fail(self):
        for value in (None, 1):
            events = valid_events(); events[-1]["validation_errors"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                runner.validate_events(events, "validation", True)
        with self.assertRaises(ValueError):
            runner.validate_events(valid_events(), "warp", True)

    def test_minimized_frames_stale_extent_and_missing_padding_fail(self):
        for mutate in (lambda e: e[-1].update(paused_checks=0),
                       lambda e: e[4].update(width=322),
                       lambda e: e[1].update(row_pitch=1),
                       lambda e: e[-1].update(verified_pixels=0)):
            events = valid_events(); mutate(events)
            with self.assertRaises(ValueError):
                runner.validate_events(events, "validation", True)

    def test_retained_objects_destroyed_hwnd_and_fake_loss_fail(self):
        for name, key, value in (("shutdown","sdk_objects",1), ("shutdown","hwnd_alive",False),
                                 ("observed_loss","native_reason",0), ("coalesced_resize","gpu_resizes",3)):
            events = valid_events(); next(e for e in events if e["event"]==name)[key]=value
            with self.subTest(key=key), self.assertRaises(ValueError):
                runner.validate_events(events, "validation", True)


class PacingEvidenceTests(unittest.TestCase):
    def events(self):
        return [{"event":"device", "warp":False, "gbv":False, "vsync":False},
                {"event":"loop_ready", "frames":1},
                {"event":"shutdown", "sdk_objects":0, "hwnd_alive":True},
                {"event":"interactive_exit", "frames":31, "target_fps":30, "loop_elapsed_ns":1000000020,
                 "timed_waits":30, "high_resolution_timer":True, "interactive_acceptance":False}]

    def test_real_clock_lower_bound_and_readiness(self):
        runner.validate_pacing(self.events(),30,31)

    def test_early_frames_no_wait_wrong_cap_and_claimed_acceptance_fail(self):
        for change in ({"loop_elapsed_ns":999999999}, {"timed_waits":0}, {"target_fps":60},
                       {"frames":30}, {"high_resolution_timer":False}, {"interactive_acceptance":True}):
            events=self.events(); events[-1].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                runner.validate_pacing(events,30,31)

    def test_missing_ready_or_injected_recovery_cannot_pass_normal_loop(self):
        events=self.events(); del events[1]
        with self.assertRaises(ValueError): runner.validate_pacing(events,30,31)
        events=self.events(); events.insert(2,{"event":"observed_loss"})
        with self.assertRaises(ValueError): runner.validate_pacing(events,30,31)


class InstalledConsumerAuditTests(unittest.TestCase):
    def test_installed_header_matches_exact_reviewed_public_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "include/graphics/Device.hpp"
            installed = root / "out/install/include/graphics/Device.hpp"
            source.parent.mkdir(parents=True); installed.parent.mkdir(parents=True)
            source.write_bytes(b"reviewed public header\r\n"); installed.write_bytes(source.read_bytes())
            record = audit.installed_presentation_header(root, installed)
            self.assertEqual(record["sha256"], record["verified_source"]["sha256"])
            installed.write_bytes(b"reviewed public header\n")
            with self.assertRaises(audit.AuditError):
                audit.installed_presentation_header(root, installed)
            installed.rename(installed.with_name("Foreign.hpp"))
            with self.assertRaises(audit.AuditError):
                audit.installed_presentation_header(root, installed.with_name("Foreign.hpp"))

    def test_stale_or_missing_installed_library_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / audit.ARTIFACTS["core"]
            installed = root / "out/install/lib/GraphicsSDKCore.lib"
            source.parent.mkdir(parents=True); installed.parent.mkdir(parents=True)
            source.write_bytes(b"reviewed SDK binary")
            with self.assertRaises(audit.AuditError):
                audit.verify_presentation_library(root)
            installed.write_bytes(source.read_bytes()); audit.verify_presentation_library(root)
            installed.write_bytes(b"older SDK binary")
            with self.assertRaises(audit.AuditError):
                audit.verify_presentation_library(root)


if __name__ == "__main__":
    unittest.main()
