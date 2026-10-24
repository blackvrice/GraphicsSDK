import pathlib
root=pathlib.Path(__file__).resolve().parents[5]
blocks={
'README.md':'''The [Vulkan allocator failure study](docs/PHASE3_VULKAN_BUFFER_POOL_FAULT_REPORT.md)
adds guarded synthetic OOM/bind errors and an isolated buffer output-ownership fix.
It reproduces 24 invalid frees in the old candidate and none after the fix, with 60
clean GPU failure/retry processes. Product-wide ownership and broader churn/performance
acceptance remain open; product sources are unchanged.''',
'docs/ROADMAP.md':'''The [guarded buffer failure study](PHASE3_VULKAN_BUFFER_POOL_FAULT_REPORT.md)
now covers pool OOM/exact fallback, repeated OOM, bind failures on fresh/growing/shared
slots, host/dedicated buffers and retained recordings on NVIDIA/Intel with validation
OFF/ON. A failed-output ownership defect was reproduced and fixed in a new isolated
candidate. Product-wide creation-output ownership audit/fix, mixed size/occupancy/churn
and performance acceptance remain before adoption. Long memory/Intel AV deferral is unchanged.''',
'docs/BUILD.md':'''The [failure-study build script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-buffer-pool-fault/verification-scripts/build.py)
builds a new hardened copy, then links the same guarded fault client to prior and new
SDK installations. It uses MSVC serial builds, new roots, child-only VVL settings and
the deferred memory CTest exclusion from initial selection. Existing source/build/result
roots are preserved; new reruns require matching new runner/audit paths.''',
'docs/TESTING.md':'''The [guarded allocator failure campaign](PHASE3_VULKAN_BUFFER_POOL_FAULT_REPORT.md)
adds 15 cases × two GPUs × validation OFF/ON, each with three failure/retry cycles.
Eight pre-fix controls deliberately detect guarded invalid frees; eight malformed
runtime controls fail closed. It records SDK CTests 57, consumer plan CTests 2, plan
assertions 914 per variant, and Python tests 506. Synthetic errors do not exhaust
real GPU memory or inject device loss. The three deferred memory tests remain excluded.''',
'docs/VULKAN.md':'''The [buffer failure study](PHASE3_VULKAN_BUFFER_POOL_FAULT_REPORT.md)
changes a separate adaptive candidate to acquire buffer memory output ownership only
after allocation succeeds. Guarded poison-output controls reproduce the prior invalid
free without forwarding fake handles to VVL/the driver. Slot rollback, native lifetime,
existing data and retry are verified. Product creation paths still need their own
output-ownership audit/fix; this experiment does not modify the product SDK.''',
'docs/BENCHMARK_DESIGN.md':'''The [buffer fault campaign](PHASE3_VULKAN_BUFFER_POOL_FAULT_REPORT.md)
is a correctness/cleanup experiment, not another CPU/GPU timing comparison. It separates
guard preparation, planned failure, retry/full GPU verification, release/idle and
teardown. Native attempt/success and request-byte conservation plus poisoned-output
guards distinguish a returned error from actual allocator cleanup. No physical-memory
or new performance claim is made.''',
'docs/BENCHMARK_REPORT.md':'''The [buffer allocator failure report](PHASE3_VULKAN_BUFFER_POOL_FAULT_REPORT.md)
records 60 clean failure/retry processes and eight guarded pre-fix regressions. The
isolated output-ownership fix reduces observed poisoned-handle free attempts from 24
to zero. New performance is not measured; product adoption, broader churn acceptance
and product creation-output auditing remain open.''',
'docs/DEPENDENCY_LICENSES.md':'''The [guarded fault-layer inventory](../licenses/generated/windows-vulkan-buffer-pool-fault-release/inventory.json)
records 886 conservative inputs/21 components and 35 new compiler/34 new linker read
logs for the isolated SDK and both installed consumers, plus preserved adaptive/SDK/SDL
closure. The layer/client are own sources; no new third-party dependency or vendor
modification is introduced. The source ZIP excludes binaries, vendor trees and VVL.''',
}
for name,block in blocks.items():
    p=root/name;s=p.read_text(encoding='utf-8');assert block not in s
    p.write_bytes((s.rstrip()+'\n\n'+block+'\n').replace('\n','\r\n').encode())
print('PASS updated current build/testing/roadmap links')
