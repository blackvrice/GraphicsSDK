import pathlib
root=pathlib.Path(__file__).resolve().parents[5]
blocks={
 'README.md': '''
The [adaptive Vulkan buffer reservation study](docs/PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md)
adds an isolated 1-slot-first growth candidate and fresh baseline/fixed/adaptive/SDL
comparison. It removes the fresh single-buffer 64KiB request while retaining batch
allocation savings. Production adoption, failure injection and broader churn acceptance remain open.
''',
 'docs/ROADMAP.md': '''
The [adaptive small-buffer reservation candidate](PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md)
now starts each live memory-type/size class at one slot and grows up to 64KiB.
Sparse boundary, retained-survivor/reset, slot lifetime and fresh four-variant measurements
are covered. The fresh single 4KiB request is 4KiB; a 1000-buffer batch uses 66 allocations.
Production adoption still requires fault-injected failure cleanup and broader size/churn
policy acceptance. Long memory and historical Intel AV investigations remain deferred.
''',
 'docs/BUILD.md': '''
For the next isolated allocator candidate, use the
[adaptive study build script](../benchmarks/reports/validation/2026-10-24-phase3-vulkan-adaptive-buffer-pool/verification-scripts/build.py)
and [generator](../tools/prepare_vulkan_adaptive_buffer_pool.py). The recorded profile uses
new source/build/install/benchmark/trace roots, MSVC `--parallel 1`, child-only VVL settings,
and the deferred memory exclusion from the start. Existing product and fixed-candidate
builds are preserved. Reruns require new roots and matching runner/audit paths.
''',
 'docs/TESTING.md': '''
The [adaptive buffer reservation study](PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md)
records 57 selected CTests and 478 Python tests. New native checks cover all bounded
power-of-two capacities, sparse growth boundaries, partial reuse, retained large-block
survivors, reset after empty and retained GPU data. The four-variant matrix has 16
correctness, 48 performance and 12 separate native-trace processes. The 3 deferred
`memory_reclamation_` cases remain excluded at initial CTest selection; no policy change
or long memory/Intel AV completion is implied. Failure injection remains an adoption gate.
''',
 'docs/VULKAN.md': '''
An [adaptive small-buffer candidate](PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md)
uses one-slot-first geometric growth per live memory type/stride, capped at 64KiB.
It is applied only to a copied SDK. Public ABI and product sources remain unchanged.
Empty blocks are freed, existing slots retain native lifetimes, and occupied large
blocks do not compact automatically. Request-byte accounting is not physical residency.
''',
 'docs/BENCHMARK_DESIGN.md': '''
The [adaptive reservation comparison](PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md)
adds a fourth freshly measured variant: original SDK, fixed slab, adaptive slab and
pinned SDL. It preserves the allocation workload source and separate create/release
CPU intervals, full-resource readback, randomized three-process repetitions, and
validation/native-trace separation. Policy-specific native call/byte expectations
are reviewed independently; neither public release nor native free measures residency.
''',
 'docs/BENCHMARK_REPORT.md': '''
The [adaptive small-buffer report](PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md)
records the next isolated implementation and fresh baseline/fixed/adaptive/SDL results.
One fresh 4KiB buffer requests 4KiB, and 1000 use 66 native allocations/4,124,672 bytes.
The first-buffer reservation objective is met; production adoption and broader failure/
occupancy acceptance remain open. No whole-SDK or physical-memory ranking is inferred.
''',
 'docs/DEPENDENCY_LICENSES.md': '''
The adaptive allocator experiment adds
[its own candidate inventory](../licenses/generated/windows-vulkan-adaptive-buffer-pool-candidate-release/inventory.json)
from the new SDK/installed-consumer compiler and linker reads plus the preserved fixed
candidate/SDK/SDL closure. It has 805 conservative inputs/21 components and 35 new compiler/
34 new linker read logs. No new third-party allocator or vendor changes are introduced.
This is source provenance, not symbol reachability or legal approval; candidate binaries
and vendor trees are excluded from the own-source ZIP.
''',
}
for name,block in blocks.items():
    p=root/name;s=p.read_text(encoding='utf-8');assert block.strip() not in s
    p.write_bytes((s.rstrip()+'\n\n'+block.strip()+'\n').replace('\n','\r\n').encode())
print('PASS updated current roadmap/build/testing/report links')
