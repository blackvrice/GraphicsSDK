import pathlib
root=pathlib.Path(__file__).resolve().parents[5]
blocks={
'README.md':'''Product Vulkan [creation-output ownership](docs/VULKAN_CREATION_OWNERSHIP_REPORT.md)
is now hardened at 29 creation sites. Actual-implementation PFN tests reproduce 21
invalid cleanup attempts before the fix and zero afterward; separate NVIDIA/Intel
buffer error/readback tests also pass. This is a product correctness fix. Buffer
pooling remains experimental, and no new timing or physical-memory claim is made.''',
'docs/ROADMAP.md':'''The [product creation-ownership fix](VULKAN_CREATION_OWNERSHIP_REPORT.md)
now guards 29 single-output creation sites and preserves defined array/pipeline
failure cleanup. Product CTests 55, Python 526, before/after actual-implementation
PFN tests and separate hardware OOM/bind controls pass. Loader-level failure
injection, broader errors and pool/churn/performance acceptance remain separate;
this does not reopen the deferred long-memory/Intel AV investigation.''',
'docs/BUILD.md':'''The [product ownership profile](VULKAN_CREATION_OWNERSHIP_REPORT.md) uses
new product and before/after consumer roots with MSVC serial builds and the deferred
memory exclusion from initial CTest selection. The recorded
[build script](../benchmarks/reports/validation/2026-10-24-vulkan-creation-ownership/verification-scripts/build.py)
builds the current product rather than a pool candidate. Historical experiment tools
pin the old SDK source identity: reproduce them from the older commit/source ZIP or
preserved original source, not by relaxing their guards against the new product.''',
'docs/TESTING.md':'''The [creation-ownership tests](VULKAN_CREATION_OWNERSHIP_REPORT.md) add a private
helper CPU contract (3640 assertions), 27 actual-implementation failure/retry cases
compiled against before/after sources, and separate actual-GPU buffer failure/readback
processes. Product selected CTests are 55 (54 prior + 1 new); Python tests are 526.
The CPU PFNs load no driver. Hardware tests cover two GPUs with validation OFF/ON.
Instance/device/debug-messenger/surface failures are statically audited/helper-tested,
not loader-level injected. The three deferred memory tests remain excluded.''',
'docs/VULKAN.md':'''The [product creation-output fix](VULKAN_CREATION_OWNERSHIP_REPORT.md) uses
private `acquireOutput` to take ownership only after successful creation. Graphics
pipeline arrays retain their defined null/partial-success cleanup. Public ABI,
host-memory preference and dedicated resource allocation remain unchanged; no pool
policy is promoted. Historical product inputs are resolved through a verified
pre-fix snapshot by the new audit, without changing old inventories/reports.''',
'docs/BENCHMARK_DESIGN.md':'''The [product ownership profile](VULKAN_CREATION_OWNERSHIP_REPORT.md) combines
CPU-only actual-implementation PFN injection with separate real-GPU error/readback
checks. Those evidence types remain distinct. It reports a correctness fix without
new timing measurements and preserves the old baseline bytes/source identity for
historical comparisons. New performance comparisons must rebuild matched consumers.''',
'docs/BENCHMARK_REPORT.md':'''The [product creation-ownership report](VULKAN_CREATION_OWNERSHIP_REPORT.md)
records the adopted correctness fix: CPU invalid cleanup 21 to zero and guarded real
GPU invalid frees 12 to zero. Buffer pooling is not adopted and prior timing values
are not reattributed to the new product fingerprint. Old evidence/packages are preserved.''',
'docs/DEPENDENCY_LICENSES.md':'''The [product ownership inventory](../licenses/generated/windows-vulkan-creation-ownership-release/inventory.json)
records 909 conservative inputs/21 components with 36 compiler and 35 linker read logs.
Changed historical product inputs resolve only to a verified original source snapshot;
unchanged artifacts are still hash-checked directly. Exact-signature test stub generation
also records the pinned Vulkan registry input. No new dependency/vendor modification
or binary redistribution is introduced; this is not legal approval or symbol reachability.''',
}
for name,block in blocks.items():
    p=root/name;s=p.read_text(encoding='utf-8');assert block not in s
    p.write_bytes((s.rstrip()+'\n\n'+block+'\n').replace('\n','\r\n').encode())
print('PASS updated current product build/testing/provenance links')
