"""Synthetic rejection tests; these are not real runtime or GPU observations."""
import copy
import hashlib
import io
import json
import pathlib
import sys
import unittest
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/"tools/memory_validation_probe"))
import runtime_compare as runtime
from prepare_runtime import verify_package

SYSTEM = "C:/Windows/System32"
AGILITY = "D:/runtime"


def evidence(source="agility", mode="hardware-gbv"):
    gpu, warp = mode != "hardware-cpu", mode == "warp-gbv"
    root = AGILITY if source == "agility" else SYSTEM
    h = dict(type="header", schema=1, source=source, mode=mode, native_only=True, iterations=64, requested_hardware_index=0,
             requested_sdk_version=619 if source == "agility" else 0, actual_sdk_version=619 if source == "agility" else 616,
             device_flags=3 if gpu else 1, gpu_validation=gpu, gpu_validation_flags=0, experimental_features=0,
             runtime_path=AGILITY if source == "agility" else "-", configuration_provider=root+"/D3D12SDKLayers.dll",
             runtime_modules=[root+"/D3D12Core.dll", root+"/d3d12SDKLayers.dll", SYSTEM+"/d3d12.dll"],
             adapter="WARP" if warp else "hardware", software=warp, luid_low=2 if warp else 1, luid_high=0)
    records = [h]
    counts = [(0,0,0,0), (0,0,0,0), (0,1,0,0), (0,1,1,0), (64,193,193,0), (64,193,193,0), (64,193,193,1), (64,193,193,1)]
    phases = ("baseline", "counter_control", "alias_held", "alias_released", "checkpoint", "resources_released", "device_released", "settled")
    for i, (phase, values) in enumerate(zip(phases, counts)):
        r = dict(type="sample", phase=phase, elapsed=i, validation_errors_total=0 if i == 0 else 1,
                 private_bytes=100000000, handles=100, gpu_local_hresult=0, gpu_nonlocal_hresult=0, gpu_local_bytes=0, gpu_nonlocal_bytes=0)
        r.update(zip(("iterations", "resource_creations", "resource_destructions", "device_destructions"), values))
        records.append(r)
    records.append(dict(type="footer", functional_passed=True, memory_acceptance_claim=False,
                        validation_counter="unfiltered_callback_before_teardown", resource_creations=193, resource_destructions=193,
                        device_destructions=1, validation_errors_total=1, intentional_validation_errors=1, unexpected_validation_errors=0))
    return records


class RuntimeComparisonTests(unittest.TestCase):
    def reject(self, mutate, message):
        r = evidence();mutate(r)
        with self.assertRaisesRegex(ValueError, message):
            runtime.analyze(r, "agility", "hardware-gbv", AGILITY, SYSTEM, 619)

    def test_all_nine_combinations_are_observations(self):
        cases = []
        for source in runtime.SOURCES:
            for mode in runtime.MODES:
                result = runtime.analyze(evidence(source, mode), source, mode, AGILITY, SYSTEM, 619)
                self.assertFalse(result["memory_acceptance_claim"]);cases.append(result)
        runtime.validate_matrix(cases)

    def test_fallback_or_wrong_provider_cannot_pass(self):
        self.reject(lambda r:r[0].update(requested_hardware_index=1), "adapter selection")
        self.reject(lambda r:r[0].update(actual_sdk_version=616), "not active")
        self.reject(lambda r:r[0].update(configuration_provider=SYSTEM+"/D3D12SDKLayers.dll"), "provider/fallback")
        self.reject(lambda r:r[0].update(runtime_path="D:/other"), "not active")

    def test_mixed_missing_and_duplicate_modules_fail(self):
        self.reject(lambda r:r[0]["runtime_modules"].pop(), "mixed runtime")
        self.reject(lambda r:r[0]["runtime_modules"].append(SYSTEM+"/D3D12Core.dll"), "mixed runtime")
        self.reject(lambda r:r[0]["runtime_modules"].append(AGILITY+"/d3d12core.dll"), "mixed runtime")

    def test_reduced_or_misreported_validation_fails(self):
        self.reject(lambda r:r[0].update(device_flags=1), "validation configuration")
        self.reject(lambda r:r[0].update(device_flags=7), "validation configuration")
        self.reject(lambda r:r[0].update(gpu_validation_flags=1), "validation configuration")
        self.reject(lambda r:r[0].update(experimental_features=1), "validation configuration")
        self.reject(lambda r:r[0].update(gpu_validation_flags=False), "validation configuration")

    def test_missing_error_control_or_unexpected_error_fails(self):
        self.reject(lambda r:r[2].update(validation_errors_total=0), "counter control")
        self.reject(lambda r:r[6].update(validation_errors_total=2), "counter control")
        self.reject(lambda r:r[-1].update(intentional_validation_errors=0), "error accounting")

    def test_missing_checkpoints_and_native_alias_errors_fail(self):
        self.reject(lambda r:r.pop(5), "runtime checkpoints")
        self.reject(lambda r:r[3].update(resource_destructions=1), "destruction contract")
        self.reject(lambda r:r[7].update(device_destructions=0), "destruction contract")
        self.reject(lambda r:r[-1].update(device_destructions=True), "final counters")

    def test_hardware_counter_failures_and_incomplete_footer_fail(self):
        self.reject(lambda r:r[5].update(gpu_local_hresult=-1, gpu_local_bytes=None), "Hardware observation")
        self.reject(lambda r:r[5].update(private_bytes=float("nan")), "runtime counters")
        self.reject(lambda r:r.pop(), "Header/footer")
        self.reject(lambda r:r[-1].update(memory_acceptance_claim=True), "Incomplete runtime result")

    def test_warp_missing_dxgi_is_explicit(self):
        r = evidence(mode="warp-gbv")
        for row in r[1:-1]:
            row.update(gpu_nonlocal_hresult=-1, gpu_nonlocal_bytes=None)
        result = runtime.analyze(r, "agility", "warp-gbv", AGILITY, SYSTEM, 619)
        self.assertIsNone(result["nonlocal_growth_before_device_release"])

    def test_matrix_cannot_mix_adapters_or_versions(self):
        cases = [runtime.analyze(evidence(s,m), s,m, AGILITY,SYSTEM,619) for s in runtime.SOURCES for m in runtime.MODES]
        with self.assertRaisesRegex(ValueError, "Incomplete runtime matrix"):
            runtime.validate_matrix(cases[:-1])
        changed = copy.deepcopy(cases);changed[0]["luid_low"] = 99
        with self.assertRaisesRegex(ValueError, "Adapter changed"):
            runtime.validate_matrix(changed)
        changed = copy.deepcopy(cases);changed[0]["actual_sdk_version"] = 615
        with self.assertRaisesRegex(ValueError, "Global/factory"):
            runtime.validate_matrix(changed)


class PinnedPackageTests(unittest.TestCase):
    def test_modified_package_is_rejected_before_extraction(self):
        lock = json.loads(runtime.LOCK.read_text())
        with self.assertRaisesRegex(ValueError, "package hash/size"):
            verify_package(b"untrusted package", lock)

    def test_duplicate_case_alias_members_are_rejected(self):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as archive:
            archive.writestr("LICENSE.txt", b"one");archive.writestr("license.TXT", b"two")
        data = stream.getvalue()
        lock = {"package_sha256": hashlib.sha256(data).hexdigest(), "maximum_package_bytes": 65536}
        with self.assertRaisesRegex(ValueError, "Duplicate runtime"):
            verify_package(data, lock)


if __name__ == "__main__":
    unittest.main()
