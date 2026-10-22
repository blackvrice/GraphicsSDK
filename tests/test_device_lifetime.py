"""Synthetic parser tests; native evidence comes from separate Windows processes."""
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("device_lifetime", ROOT/"tools/memory_validation_probe/lifetime.py")
lifetime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(lifetime)
FP = "a"*64


def evidence(sdk=False, warp=False):
    mode = ("warp" if warp else "hardware") if sdk else ("warp-gbv" if warp else "hardware-gbv")
    header = dict(type="header", schema=1, mode=mode, cycles=2, gpu_validation=True, gpu_state_tracking=True)
    adapter = dict(adapter="test adapter", luid_high=0, luid_low=1, software=warp)
    if sdk:
        header.update(sdk_sha256=FP, batches_per_cycle=1, ring=8, side=64, copy_bytes=262144)
        records = [header, dict(type="adapter", **adapter)]
    else:
        header.update(native_only=True, iterations_per_cycle=1, **adapter)
        records = [header]

    def sample(phase, cycle, device_destructions, **changes):
        result = dict(type="sample", phase=phase, cycle=cycle, elapsed=len(records),
                      private_bytes=100000000, handles=100, device_destructions=device_destructions,
                      gpu_local_hresult=0, gpu_nonlocal_hresult=0, gpu_local_bytes=0, gpu_nonlocal_bytes=0)
        result.update(changes)
        records.append(result)
        return result

    if not sdk:
        sample("baseline", 0, 0, resource_creations=0, resource_destructions=0, validation_errors=0)
    for c in range(1, 3):
        if sdk:
            for i, phase in enumerate(("device_ready", "resources_released", "child_ready", "sdk_released_child_held", "last_alias_held", "native_device_destroyed", "settled")):
                r = sample(phase, c, c if i >= 5 else c-1, frames=(c-1)*8 if i == 0 else c*8, sdk_statistics_available=i < 3)
                if i < 3:
                    r.update(dict(zip(lifetime.OWNED, [1, 1, 65536, 0, 0] if i == 2 else [0]*5)))
        else:
            before = (c-1)*4
            for i, phase in enumerate(("device_ready", "alias_held", "alias_released", "resources_released", "runtime_modules", "device_released", "settled")):
                if phase == "runtime_modules":
                    records.append(dict(type=phase, cycle=c, paths=["test-runtime.dll"]))
                    continue
                sample(phase, c, c if i >= 5 else c-1,
                       resource_creations=before+[0, 1, 1, 4, 0, 4, 4][i],
                       resource_destructions=before+[0, 0, 1, 4, 0, 4, 4][i], validation_errors=0)
    footer = dict(type="footer", lifetime_contracts_passed=True, memory_acceptance_claim=False,
                  cycles=2, device_destructions=2, validation_errors=0)
    if sdk:
        footer.update(frames=16, full_images=48, verified_buffer_bytes=16*262144,
                      validation_counter="unfiltered_callback_before_device_teardown")
    else:
        footer.update(resource_creations=8, resource_destructions=8)
    return records+[footer]


class LifetimeParserTests(unittest.TestCase):
    def reject(self, change, message, sdk=False):
        records = evidence(sdk)
        change(records)
        with self.assertRaisesRegex(ValueError, message):
            lifetime.analyze(records, sdk, FP)

    def test_valid_native_and_sdk_scopes_remain_observations(self):
        for sdk in (False, True):
            for warp in (False, True):
                result = lifetime.analyze(evidence(sdk, warp), sdk, FP)
                self.assertTrue(result["lifetime_contracts_passed"])
                self.assertFalse(result["memory_acceptance_claim"])

    def test_missing_footer_cycle_or_callback_is_rejected(self):
        self.reject(lambda r:r.pop(), "Missing lifetime records")
        self.reject(lambda r:r.pop(4), "Missing lifetime records")
        self.reject(lambda r:r[5].update(resource_destructions=0), "destruction/alias")
        self.reject(lambda r:r[7].update(device_destructions=0), "device destruction")

    def test_early_resource_or_device_destruction_is_rejected(self):
        self.reject(lambda r:r[3].update(resource_destructions=1), "resource destruction/alias")
        self.reject(lambda r:r[5].update(device_destructions=1), "device destruction/alias")
        self.reject(lambda r:r[5].update(device_destructions=1), "device destruction/alias", sdk=True)

    def test_failed_or_reduced_validation_cannot_pass(self):
        self.reject(lambda r:r[0].update(gpu_state_tracking=False), "Validation configuration")
        self.reject(lambda r:r[5].update(validation_errors=1), "validation error")
        self.reject(lambda r:r[-1].update(validation_counter="stored_messages"), "error coverage", sdk=True)

    def test_missing_identity_module_or_failed_hardware_observation(self):
        self.reject(lambda r:r[0].pop("adapter"), "adapter identity")
        self.reject(lambda r:r[6].update(paths=[]), "runtime identity")
        self.reject(lambda r:r[2].update(gpu_local_hresult=-1, gpu_local_bytes=None), "Hardware observation")
        self.reject(lambda r:r[2].update(private_bytes=float("nan")), "process/lifetime")

    def test_sdk_ownership_and_full_references_are_required(self):
        self.reject(lambda r:r[3].update(objects=1), "not reclaimed", sdk=True)
        self.reject(lambda r:r[4].update(resources=0), "alias missing", sdk=True)
        self.reject(lambda r:r[-1].update(full_images=1), "reference coverage", sdk=True)
        self.reject(lambda r:r[-1].update(verified_buffer_bytes=1), "reference coverage", sdk=True)

    def test_post_device_statistics_cannot_be_fabricated(self):
        self.reject(lambda r:r[5].update(objects=0), "Invented post-device", sdk=True)
        self.reject(lambda r:r[7].update(sdk_statistics_available=True), "availability mismatch", sdk=True)

    def test_changed_source_order_or_final_callback_totals_are_rejected(self):
        self.reject(lambda r:r[0].update(sdk_sha256="b"*64), "fingerprint", sdk=True)
        self.reject(lambda r:r[3].update(cycle=2), "cycle order")
        self.reject(lambda r:r[-1].update(resource_destructions=7), "final resource")
        self.reject(lambda r:r[-1].update(memory_acceptance_claim=True), "Incomplete lifetime")

    def test_warp_unsupported_dxgi_is_explicit(self):
        records = evidence(warp=True)
        for r in records:
            if r.get("type") == "sample":
                r.update(gpu_local_hresult=-1, gpu_local_bytes=None, gpu_nonlocal_hresult=-1, gpu_nonlocal_bytes=None)
        self.assertTrue(lifetime.analyze(records)["lifetime_contracts_passed"])


if __name__ == "__main__":
    unittest.main()
