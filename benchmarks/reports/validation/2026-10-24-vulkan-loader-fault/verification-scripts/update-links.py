import pathlib
root=pathlib.Path(__file__).resolve().parents[5]
blocks={
'README.md':'''The [real-loader failure profile](docs/VULKAN_LOADER_FAULT_REPORT.md) now covers
instance/device/messenger failures on NVIDIA/Intel and Win32 surface failures on
the supported NVIDIA adapter. It passes 96 synthetic error/retry cycles with zero
invalid cleanup; the old SDK reproduces 24 guarded invalid cleanup calls. Product
sources remain unchanged; this adds correctness evidence without a timing claim.''',
'docs/ROADMAP.md':'''The [loader initialization failure stage](VULKAN_LOADER_FAULT_REPORT.md) is now
covered by a separate explicit layer/installed SDK consumer: 32 clean processes,
96 failure/retry cycles, 8 guarded old-SDK regressions and 10 fail-closed controls.
Intel initialization is covered; surface injection runs on the adapter advertising
swapchain support (NVIDIA). Broader errors, mixed-size/occupancy/churn, performance
acceptance and Linux remain open. The long-memory/Intel AV deferral is unchanged.''',
'docs/BUILD.md':'''The [loader failure consumer](VULKAN_LOADER_FAULT_REPORT.md) links the existing
before/after product installations without rebuilding or changing product sources.
Its final recorded build script is `2026-10-24-vulkan-loader-fault/verification-scripts/build-v4.py`
under benchmark validation reports; serial MSVC Release builds use fresh roots.
Pilot/failed build roots are retained separately from final evidence.''',
'docs/TESTING.md':'''Following the earlier helper/PFN stage, [real loader failure injection](VULKAN_LOADER_FAULT_REPORT.md)
now covers instance/device/messenger on two GPUs and Win32 surface on the supported
NVIDIA adapter. New CPU plan assertions are 216; current Python tests are 550.
The 55-product-CTest result remains prior evidence for the unchanged SDK, not a new
run. Deferred memory tests remain registered and unselected. Native object conservation
and readback are bounded correctness checks, not physical-memory acceptance.''',
'docs/VULKAN.md':'''The [real-loader initialization contracts](VULKAN_LOADER_FAULT_REPORT.md) test
synthetic host/device OOM in instance, debug messenger, device and supported Win32
surface creation. The current 29-site ownership fix needs no further product change.
Dispatchable failures use null; non-dispatchable null/poison paths have guarded
cleanup controls. Successful retries preserve live guard data and zero VVL errors.''',
'docs/DEPENDENCY_LICENSES.md':'''The [loader-failure inventory](../licenses/generated/windows-vulkan-loader-fault-release/inventory.json)
extends the preserved conservative product inventory to 915 inputs/21 components,
adding six compiler and six linker read logs for new before/after consumers. The
same pinned Vulkan-Headers aggregator/Win32 declarations were explicitly reviewed.
No new third-party dependency or binary redistribution is introduced.''',
}
for name,block in blocks.items():
    p=root/name;s=p.read_text(encoding='utf-8');assert block not in s
    p.write_bytes((s.rstrip()+'\n\n'+block+'\n').replace('\n','\r\n').encode())
attributes='''
/tests/vulkan_loader_fault/** text eol=crlf
/benchmarks/reports/2026-10-24-vulkan-loader-fault/** -text whitespace=cr-at-eol
/benchmarks/reports/validation/2026-10-24-vulkan-loader-fault/** -text whitespace=cr-at-eol,-blank-at-eol,-blank-at-eof
/benchmarks/results/2026-10-24-vulkan-loader-fault*/** -text whitespace=cr-at-eol,-blank-at-eol,-blank-at-eof
/benchmarks/reports/sources/vulkan-loader-fault-*.zip -text
'''
p=root/'.gitattributes';p.write_bytes((p.read_text(encoding='utf-8').rstrip()+'\n'+attributes).replace('\n','\r\n').encode())
print('PASS current documentation and evidence attributes updated')
