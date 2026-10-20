# Testing and acceptance

## Automated checks

`ctest --test-dir out/core -C Release --output-on-failure`:

* core_contracts: backend advertising, Unsupported errors, invalid shader inputs.
* d3d12_hardware: real adapter upload/copy/readback, triangle output, timestamps,
  state scopes, device/thread mismatch, stale snapshot rejection, resource retention,
  sampled texture/reflection/descriptor exhaustion, hidden-window swapchain/present/resize.
* d3d12_validation: same correctness checks with D3D12 debug layer/GPU-based validation;
  ERROR/CORRUPTION count must be zero after GPU completion.
* d3d12_warp: same functional tests on explicitly selected software adapter. Its timings
  do not enter hardware performance comparisons.

GPU/debug-layer unavailable returns 77 and CTest reports SKIP. A skipped test is not verified
GPU acceptance. Compile/runtime failures are errors. Tests check a red triangle center,
blue clear corner, green sampled result, all uploaded bytes in odd-pitch texture rows and
buffer copies. Resource wrappers and command lists are deliberately destroyed before GPU completion.

The original validation run exposed OBJECT_DELETED_WHILE_STILL_IN_USE at swapchain resize.
The fix signals a fresh queue-tail fence after DXGI work. The regression also rejects resize
while an unsubmitted command retains a backbuffer tracker.

2026-10-20 Windows acceptance: 4/4 D3D12 configuration CTests, 1/1 backend-disabled
configuration CTest and 3/3 Python statistics tests passed. GPU validation completed
44 assertions, including ERROR/CORRUPTION count zero. Native Linux/macOS and remote CI
were not run. Actual CTest logs are retained in `benchmarks/reports/validation`.

Python `unittest` checks percentiles, repeated run statistics, null GPU observations,
incompatible device/validation/category rejection and noisy regression decisions.
An independent `tests/consumer` uses installed headers and package exports only.
`examples/Triangle.cpp` renders and writes an actual PPM readback.

`gfx_debug_capture <absolute-path-to-gfx_tests.exe> --validation` captures Windows debug output
for otherwise abrupt native validation termination. Use it for diagnostics, outside performance runs.

## Remaining acceptance

Vulkan/Metal, Linux/macOS native build/runtime, real device removal injection, prolonged
memory-leak stress, multi-queue/parallel recording, occlusion/minimize/DPI/interactive resize,
external GPU import, and PIX/RenderDoc vendor profiler captures are unverified.
Hidden-window presentation does not establish interactive window-system acceptance.
Validation error count does not by itself prove absence of every leak or data race.
CI configuration is provided separately; creating the file does not mean a remote run passed.
