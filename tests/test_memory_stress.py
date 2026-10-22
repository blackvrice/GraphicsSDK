import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("memory_stress", ROOT/"tools/analyze_memory_stress.py")
stress = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stress)
FP = "a"*64
MiB = 1024*1024


def evidence(mode="gpu"):
    """Synthetic observations with a real retain/release shape, not native acceptance."""
    seconds = 900 if mode == "gpu" else 300
    header = dict(type="header", schema=1, mode=mode, sdk_sha256=FP, adapter="test adapter", luid_high=0, luid_low=1,
                  seconds=seconds, warmup=30, interval=5, ring=8, side=64, copy_bytes=262144)

    def sample(phase, elapsed, frames=0, **changes):
        result = dict(type="sample", phase=phase, elapsed=elapsed, frames=frames, rounds=frames//8,
                      objects=0, resources=0, allocation_bytes=0, pending=0, pending_refs=0,
                      private_bytes=100*MiB, working_set_bytes=80*MiB, handles=100,
                      gpu_local_hresult=0, gpu_nonlocal_hresult=0, gpu_local_bytes=10*MiB, gpu_nonlocal_bytes=5*MiB,
                      gpu_local_budget=2**30, gpu_nonlocal_budget=2**30)
        result.update(changes)
        return result

    records = [header, sample("baseline", 0), sample("cpu_held", .1, private_bytes=132*MiB), sample("cpu_released", .2),
               sample("gpu_held", .3, objects=1, resources=1, allocation_bytes=64*MiB, gpu_local_bytes=74*MiB),
               sample("gpu_released", .4), sample("measurement_start", 31, 800)]
    for elapsed in range(0, seconds, 5):
        frames = 800 + elapsed*16
        records.extend([sample("live", 31+elapsed, frames, objects=33, resources=32, allocation_bytes=4*MiB, pending=2, pending_refs=32),
                        sample("drained", 31+elapsed+.1, frames+8)])
    frames = records[-1]["frames"]
    records.extend([sample("final", 31+seconds+.1, frames),
                    dict(type="footer", elapsed=31+seconds+.2, measured_seconds=seconds+.2, frames=frames,
                         full_images=frames*3, verified_buffer_bytes=frames*262144,
                         validation_errors=0, statistics_contracts=True, status="passed")])
    return records


class MemoryAcceptanceTests(unittest.TestCase):
    def reject(self, mutate, message=None):
        records = evidence()
        mutate(records)
        with self.assertRaisesRegex((ValueError, KeyError), message or ".*"):
            stress.analyze(records, FP)

    def test_stable_observations_accept_all_modes(self):
        for mode in ("gpu", "validation", "warp"):
            with self.subTest(mode=mode):
                result = stress.analyze(evidence(mode), FP)
                self.assertTrue(result["accepted"])
                self.assertEqual(result["metrics"]["private_bytes"]["slope_per_minute"], 0)

    def test_smoke_cannot_satisfy_soak_gate(self):
        self.reject(lambda r: r[0].update(seconds=2), "Short smoke")

    def test_stale_sdk_cannot_reuse_old_campaign(self):
        self.reject(lambda r: r[0].update(sdk_sha256="b"*64), "fingerprint")

    def test_missing_footer_or_failed_process_is_rejected(self):
        self.reject(lambda r: r.pop(), "Header/footer")
        self.reject(lambda r: r[-1].update(status="failed"), "native validation")

    def test_no_validation_or_statistics_contract_result_is_rejected(self):
        self.reject(lambda r: r[-1].pop("validation_errors"), "native validation")
        self.reject(lambda r: r[-1].update(statistics_contracts=False), "native validation")

    def test_missing_and_nonfinite_process_counter_is_rejected(self):
        self.reject(lambda r: r[10].pop("private_bytes"), "invalid counter")
        self.reject(lambda r: r[10].update(private_bytes=float("nan")), "invalid counter")

    def test_fake_cpu_control_and_missing_release_are_rejected(self):
        self.reject(lambda r: r[2].update(private_bytes=100*MiB), "CPU observer")
        self.reject(lambda r: r[3].update(private_bytes=132*MiB), "CPU control release")

    def test_fake_gpu_control_and_missing_release_are_rejected(self):
        self.reject(lambda r: r[4].update(allocation_bytes=0), "GPU ownership")
        self.reject(lambda r: r[4].update(gpu_local_bytes=10*MiB), "GPU usage")
        self.reject(lambda r: r[5].update(gpu_local_bytes=74*MiB), "GPU usage")

    def test_resources_pending_references_and_objects_must_drain(self):
        for name in stress.OWNED:
            with self.subTest(name=name):
                self.reject(lambda r: r[8].update({name: 1}), "ownership")

    def test_no_work_and_partial_image_coverage_are_rejected(self):
        self.reject(lambda r: r[-1].update(full_images=1), "coverage")
        self.reject(lambda r: r[-1].update(verified_buffer_bytes=1), "coverage")
        self.reject(lambda r: r[6].update(frames=r[-1]["frames"], rounds=r[-1]["frames"]//8), "Non-monotonic")

    def test_missing_checkpoint_pair_or_large_gap_is_rejected(self):
        self.reject(lambda r: r.pop(8), "pairs")
        self.reject(lambda r: r.__delitem__(slice(7, 15)), "checkpoints")

    def test_native_gbv_error_cannot_pass(self):
        self.reject(lambda r: r[-1].update(validation_errors=1), "native validation")

    def test_monotonic_private_gpu_and_handle_leaks_fail_plateau(self):
        for name, growth in (("private_bytes", 2*MiB), ("gpu_local_bytes", MiB), ("gpu_nonlocal_bytes", MiB), ("handles", 2)):
            def leak(records):
                for i, record in enumerate(records[1:-1]):
                    if record.get("phase") == "drained":
                        record[name] += i*growth
            with self.subTest(name=name):
                self.reject(leak, "plateau gate")

    def test_warp_unsupported_dxgi_is_null_and_explicit(self):
        records = evidence("warp")
        for r in records[1:-1]:
            for segment in ("local", "nonlocal"):
                r.update({f"gpu_{segment}_hresult": -1, f"gpu_{segment}_bytes": None, f"gpu_{segment}_budget": None})
        result = stress.analyze(records, FP)
        self.assertEqual(result["metrics"]["gpu_local_bytes"], {"supported": False})
        records[0]["mode"] = "validation"
        with self.assertRaisesRegex(ValueError, "Hardware DXGI"):
            stress.analyze(records, FP)

    def test_inconsistent_dxgi_status_and_fake_zero_is_rejected(self):
        self.reject(lambda r: r[10].update(gpu_local_hresult=-1), "status/value")
        self.reject(lambda r: r[10].update(gpu_local_bytes=None), "status/value")

    def test_truncated_and_duplicate_json_fields_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)/"input.jsonl"
            path.write_bytes(b'{"type":"header"}')
            with self.assertRaisesRegex(ValueError, "Truncated"):
                stress.read_records(path)
            path.write_bytes(b'{"type":"header","type":"footer"}\n')
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                stress.read_records(path)


if __name__ == "__main__":
    unittest.main()
