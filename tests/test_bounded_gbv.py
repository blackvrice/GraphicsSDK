"""Acceptance-tool negatives; synthetic input is never native GPU evidence."""
import copy
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
import analyze_bounded_gbv as bounded

FP = "a"*64
MiB = 1024*1024


def evidence():
    def sample(phase, elapsed, frames=0, **changes):
        result = dict(phase=phase, elapsed=elapsed, frames=frames,
                      objects=0, resources=0, allocation_bytes=0, pending=0, pending_refs=0,
                      private_bytes=100*MiB, working_set_bytes=80*MiB, handles=100,
                      gpu_local_hresult=0, gpu_nonlocal_hresult=0,
                      gpu_local_bytes=10*MiB, gpu_nonlocal_bytes=5*MiB,
                      gpu_local_budget=2**30, gpu_nonlocal_budget=2**30)
        result.update(changes)
        return result

    pool = dict(objects=105, resources=104, allocation_bytes=11*MiB)
    records = [dict(case="bounded-reuse", schema=1, sdk_sha256=FP, seconds=300, warmup=30,
                    ring=8, side=64, copy_bytes=262144, gpu_state_tracking=True,
                    adapter="test adapter", luid_low=1, luid_high=0),
               sample("baseline", 0), sample("cpu_held", .1, private_bytes=132*MiB),
               sample("cpu_released", .2),
               sample("gpu_held", .3, objects=1, resources=1, allocation_bytes=64*MiB, gpu_local_bytes=74*MiB),
               sample("gpu_released", .4), sample("pool_ready", 1, **pool),
               sample("measurement_start", 31, 800, **pool)]
    records.extend(sample("drained", 31+t, 800+t*16, **pool) for t in range(5, 301, 5))
    frames=records[-1]["frames"]
    records.extend([sample("released", 331.1, frames),
                    dict(functional_passed=True, validation_counter="unfiltered_callback", validation_errors=0,
                         measured_seconds=300.2, frames=frames, full_images=frames*3,
                         verified_buffer_bytes=frames*262144)])
    return records


class BoundedAcceptanceTests(unittest.TestCase):
    def reject(self, mutate, message):
        records=evidence()
        mutate(records)
        with self.assertRaisesRegex(ValueError, message):
            bounded.analyze(records, FP)

    def test_accepts_only_fixed_working_set(self):
        result=bounded.analyze(evidence(), FP)
        self.assertTrue(result["bounded_working_set_accepted"])
        self.assertFalse(result["unbounded_texture_churn_accepted"])

    def test_short_or_disabled_validation_is_rejected(self):
        self.reject(lambda r:r[0].update(seconds=5), "Short smoke")
        self.reject(lambda r:r[0].update(warmup=1), "Short smoke")
        self.reject(lambda r:r[0].update(gpu_state_tracking=False), "state tracking")

    def test_sdk_identity_and_adapter_are_required(self):
        self.reject(lambda r:r[0].update(sdk_sha256="b"*64), "source")
        self.reject(lambda r:r[0].pop("adapter"), "adapter")

    def test_missing_footer_and_samples_fail(self):
        self.reject(lambda r:r.pop(), "sample phases")
        self.reject(lambda r:r.pop(8), "checkpoints")
        with self.assertRaisesRegex(ValueError, "Missing evidence"):
            bounded.analyze([], FP)

    def test_error_count_cannot_be_filtered_or_omitted(self):
        self.reject(lambda r:r[-1].update(validation_errors=1), "error coverage")
        self.reject(lambda r:r[-1].pop("validation_errors"), "error coverage")
        self.reject(lambda r:r[-1].update(validation_counter="stored_messages"), "error coverage")

    def test_missing_memory_and_failed_dxgi_fail(self):
        self.reject(lambda r:r[10].pop("private_bytes"), "counter")
        self.reject(lambda r:r[10].update(private_bytes=float("nan")), "counter")
        self.reject(lambda r:r[10].update(gpu_local_hresult=-1), "unavailable")
        self.reject(lambda r:r[10].update(gpu_nonlocal_bytes=None), "DXGI")

    def test_cpu_and_gpu_retention_controls_are_required(self):
        self.reject(lambda r:r[2].update(private_bytes=100*MiB), "CPU retain/release")
        self.reject(lambda r:r[3].update(private_bytes=132*MiB), "CPU retain/release")
        self.reject(lambda r:r[4].update(allocation_bytes=0), "GPU allocation")
        self.reject(lambda r:r[5].update(gpu_local_bytes=74*MiB), "GPU usage")

    def test_pending_or_changed_pool_and_final_leaks_fail(self):
        for name in bounded.OWNED:
            with self.subTest(name=name):
                self.reject(lambda r:r[10].update({name:r[10][name]+1}), "Working set")
                self.reject(lambda r:r[-2].update({name:1}), "not released")

    def test_reference_work_duration_and_checkpoint_gap_fail(self):
        self.reject(lambda r:r[-1].update(full_images=0), "references")
        self.reject(lambda r:r[-1].update(verified_buffer_bytes=0), "references")
        self.reject(lambda r:r[-1].update(measured_seconds=100), "duration")
        self.reject(lambda r:r[0].update(warmup=31), "Warmup")
        def slow(records):
            for r in records[7:-1]:
                r["frames"]=(r["frames"]//80)*8
            records[-1].update(frames=records[-2]["frames"], full_images=records[-2]["frames"]*3,
                               verified_buffer_bytes=records[-2]["frames"]*262144)
        self.reject(slow, "Insufficient measured work")
        def gap(records):
            records[8:12]=[copy.deepcopy(records[12])]*4
        self.reject(gap, "Checkpoint gap")

    def test_private_gpu_and_handle_growth_cannot_pass(self):
        for name, growth in (("private_bytes", 2*MiB), ("gpu_local_bytes", MiB),
                             ("gpu_nonlocal_bytes", MiB), ("handles", 2)):
            def leak(records):
                for i, r in enumerate(records[8:-2]):
                    r[name]+=i*growth
            with self.subTest(name=name):
                self.reject(leak, "Memory limit")


if __name__=="__main__":
    unittest.main()
