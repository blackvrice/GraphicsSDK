import pathlib
root=pathlib.Path(__file__).resolve().parents[5]
blocks={
'README.md':'''The [mixed-size pool study](docs/VULKAN_POOL_CHURN_REPORT.md) rebases the adaptive
candidate onto the hardened product and tests mixed churn, sparse occupancy and
retained recordings on both GPUs. It passes 224 injected bind errors and 35,392
full readbacks. Sparse reservation reaches 6.2x live native requirements; pooling
remains experimental and the production dedicated policy is unchanged.''',
'docs/ROADMAP.md':'''The [rebased pool churn stage](VULKAN_POOL_CHURN_REPORT.md) adds actual native
binding metadata, first-free-slot/growth checks, mixed sizes, sparse block survivors,
ended/pending recordings and 224 bind-error rollback cycles. The isolated candidate
passes 55 selected CTests; Python tests are 581. Product adoption still needs full
allocation-OOM/fallback retesting for the new fingerprint, reservation policy and
fresh matched performance acceptance. Long-memory/Intel AV investigations remain deferred.''',
'docs/BUILD.md':'''The [rebased pool experiment](VULKAN_POOL_CHURN_REPORT.md) uses new
`out/vulkan-pool-churn-source`, `-sdk`, `-install` and separate product/candidate
consumer roots. `prepare_vulkan_pool_churn.py` verifies the current product and
historical candidate before patching only the new copy. SDK and consumers build
serially with MSVC Release; the original product and old artifacts stay preserved.''',
'docs/TESTING.md':'''The [mixed-size churn profile](VULKAN_POOL_CHURN_REPORT.md) adds exact native
binding/slot/range/public-statistics checks and full buffer references in 24 clean
NVIDIA/Intel validation OFF/ON processes. New CPU model assertions are 172; Python
tests are 581. The rebased candidate passes the 55 selected product CTests with the
three deferred memory tests excluded at initial selection. Sparse survivor sets are
defined by native blocks and are not matched performance workloads.''',
'docs/VULKAN.md':'''The [current-product pool rebase](VULKAN_POOL_CHURN_REPORT.md) keeps all 29 product
creation guards and adds one for block allocation in an isolated candidate. Pool
policy headers retain their reviewed bytes. Mixed churn and retained recordings
pass exact binding/data checks, but one survivor per block retains 6.2x the live
native requirements in the recorded sparse case. No product promotion is made.''',
'docs/DEPENDENCY_LICENSES.md':'''The [rebased pool churn inventory](../licenses/generated/windows-vulkan-pool-churn-release/inventory.json)
records 996 conservative inputs/21 components and 34 compiler/33 linker read logs
from the new candidate SDK and product/candidate consumers, preserving the prior
closure. Read-only binding metadata reuses the own guarded layer and pinned Vulkan
declarations. No new dependency/vendor modification or binary redistribution is introduced.''',
'docs/BENCHMARK_REPORT.md':'''The [pool mixed-size/occupancy contracts](VULKAN_POOL_CHURN_REPORT.md) are
instrumented correctness observations, without new timings. Mixed/held logical
sequences match across product/candidate; block-based sparse survivor sets differ.
The 6.2x sparse reservation is native requested bytes, not OS residency. Prior
timings are not attributed to the rebased fingerprint, and pooling is not adopted.''',
}
for name,block in blocks.items():
    p=root/name;s=p.read_text(encoding='utf-8');assert block not in s;p.write_bytes((s.rstrip()+'\n\n'+block+'\n').replace('\n','\r\n').encode())
attributes='''
/experiments/vulkan_pool_churn/** text eol=crlf
/experiments/vulkan_pool_churn/candidate.patch -text whitespace=cr-at-eol,-blank-at-eol,-blank-at-eof
/benchmarks/reports/2026-10-24-vulkan-pool-churn/** -text whitespace=cr-at-eol
/benchmarks/reports/validation/2026-10-24-vulkan-pool-churn/** -text whitespace=cr-at-eol,-blank-at-eol,-blank-at-eof
/benchmarks/results/2026-10-24-vulkan-pool-churn/** -text whitespace=cr-at-eol,-blank-at-eol,-blank-at-eof
/benchmarks/reports/sources/vulkan-pool-churn-*.zip -text
'''
p=root/'.gitattributes';p.write_bytes((p.read_text(encoding='utf-8').rstrip()+'\n'+attributes).replace('\n','\r\n').encode())
print('PASS current pool correctness/provenance links updated')
