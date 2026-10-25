import pathlib
root=pathlib.Path(__file__).resolve().parents[5]
blocks={
'README.md':'''The [rebased allocation-failure stage](docs/VULKAN_POOL_REBASE_FAULT_REPORT.md)
freshly reruns the full fault matrix and adds mixed-context fallback/recording cases.
It passes 88 processes, 324 injected errors and 1,200 full readbacks on the unchanged
candidate. Exact fallback and rollback are verified; the sparse reservation policy
and fresh performance acceptance remain before any product promotion.''',
'docs/ROADMAP.md':'''The [current candidate allocation-fault matrix](VULKAN_POOL_REBASE_FAULT_REPORT.md)
now has fresh evidence for all 15 historical cases plus seven mixed-context cases:
264 cycles, exact fallback, repeated errors, reuse-before-pending-OOM and held-recording
rollback. Python tests are 614. Candidate sources remain unchanged and unadopted;
reservation policy and matched performance acceptance remain open. The long-memory
criterion and historical Intel AV investigation remain deferred.''',
'docs/BUILD.md':'''The [rebased fault profile](VULKAN_POOL_REBASE_FAULT_REPORT.md) freshly builds the
unchanged historical fault project and a separate mixed-context consumer against
`out/vulkan-pool-churn-install`. Product/candidate SDKs are not rebuilt in this stage.
MSVC Release uses new roots and serial compilation; actual installed-library hashes
and linker reads distinguish these fresh runs from old candidate evidence.''',
'docs/TESTING.md':'''The [rebased allocation-fault tests](VULKAN_POOL_REBASE_FAULT_REPORT.md) cover 88
clean processes/264 cycles with 324 synthetic errors, 1,200 full readbacks and 14
fail-closed controls. CPU plan tests rerun 914 assertions; Python tests are 614.
The candidate's 55 selected CTests remain prior evidence for unchanged bytes. No
deferred memory test, real heap exhaustion or historical Intel AV investigation is run.''',
'docs/VULKAN.md':'''The [mixed-context exact fallback contracts](VULKAN_POOL_REBASE_FAULT_REPORT.md)
verify that free-slot reuse leaves an allocation fault pending, the next expansion
consumes it, and successful fallback binds exactly 4KiB of new memory. Repeated
OOM, unknown errors and fallback bind failure preserve existing bindings/data and
growth retry. No allocator source or product policy changes in this stage.''',
'docs/DEPENDENCY_LICENSES.md':'''The [rebased allocation-fault inventory](../licenses/generated/windows-vulkan-pool-rebase-fault-release/inventory.json)
records 998 conservative inputs/21 components and five new compiler/five new linker
read logs, preserving the reviewed SDK/candidate closure. New own test code reuses
the existing guarded layer and binding observer. No new third-party dependency,
vendor modification or binary redistribution is introduced.''',
'docs/BENCHMARK_REPORT.md':'''The [fresh candidate OOM/fallback checks](VULKAN_POOL_REBASE_FAULT_REPORT.md)
validate correctness on the current candidate fingerprint. They add no timing data
and do not remove the earlier 6.2x sparse-reservation observation. Both SDK source
identities and prior measured results remain unchanged; pooling is still experimental.''',
}
for name,block in blocks.items():
    p=root/name;s=p.read_text(encoding='utf-8');assert block not in s;p.write_bytes((s.rstrip()+'\n\n'+block+'\n').replace('\n','\r\n').encode())
attributes='''
/experiments/vulkan_pool_rebase_fault/** text eol=crlf
/benchmarks/reports/2026-10-24-vulkan-pool-rebase-fault/** -text whitespace=cr-at-eol
/benchmarks/reports/validation/2026-10-24-vulkan-pool-rebase-fault/** -text whitespace=cr-at-eol,-blank-at-eol,-blank-at-eof
/benchmarks/results/2026-10-24-vulkan-pool-rebase-fault/** -text whitespace=cr-at-eol,-blank-at-eol,-blank-at-eof
/benchmarks/reports/sources/vulkan-pool-rebase-fault-*.zip -text
'''
p=root/'.gitattributes';p.write_bytes((p.read_text(encoding='utf-8').rstrip()+'\n'+attributes).replace('\n','\r\n').encode())
print('PASS current allocation-fault/provenance documentation updated')
