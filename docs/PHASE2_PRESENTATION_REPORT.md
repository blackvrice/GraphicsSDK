# Phase 2 — Win32 presentation and window lifecycle

2026-10-22, Windows x64 Release. An installed-package application now provides an
actual window/message loop, deferred client-pixel resize, minimized/hidden pause,
safe close and observed-loss recovery. Automated lifecycle verification passes.
**Interactive visual acceptance, unexpected TDR/hot-unplug and the original failed
GBV memory gate remain open. Phase 2 is not complete.**

## Implementation

[Windowed.cpp](../examples/presentation/Windowed.cpp) uses public SDK graphics APIs
and Win32 window APIs only. Its separate CMake project finds `GraphicsSDK::Core`
from `out/install`; it has no native D3D12/DXGI interfaces or competitor dependencies.
The message callback records state only. Multiple size events collapse into one
resize on the next active frame. Acquired wrappers and the recorded command's
backbuffer references are released before resize. Frames reuse one command list,
waiting for each fence; this is an ownership example, not a throughput optimization.

Minimized, zero-extent and hidden windows submit no frames, with a message wait in
the normal loop. Close is processed before rendering and keeps HWND alive through
GPU teardown. The PerMonitorV2 manifest is checked against actual thread context.
The DPI handler applies the suggested OS rectangle, feeding the same deferred
client-pixel resize path. Reference contracts:
[WM_SIZE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-size),
[WM_DPICHANGED](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged),
[ResizeBuffers reference release](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers).

Observed DeviceLost drops graphics objects, explicitly recreates the device and
rebuilds GPU objects from retained CPU shader bytes. Normal execution has injection
disabled. Only `--self-test` enables the existing opt-in native RemoveDevice API.
Unsupported recovery or recreation failure exits 1, without a successful skip.

## Measured automatic coverage

Host: Intel Core i7-14700K, 68,473,667,584 RAM bytes, Windows build 26220, MSVC
19.44.35228, Windows SDK 10.0.26100.0, Release `/O2 /MD`. Hardware runs select
NVIDIA GeForce GTX 1660 SUPER, driver 32.0.16.2101. Intel UHD 770 driver metadata
is retained, but Intel hardware execution is not claimed here. WARP is separate
software coverage.

The [campaign](../benchmarks/reports/validation/2026-10-22-phase2-presentation/campaign/summary.json)
runs six fresh processes sequentially:

| Mode | Processes, VSync on/off | Full images | Verified RGBA pixels | Native recoveries | Validation errors |
|---|---:|---:|---:|---:|---:|
| Hardware, validation off | 2 | 82 | 14,964,026 | 4 | unavailable |
| Hardware, full GBV | 2 | 82 | 14,964,026 | 4 | 0 |
| WARP, validation off | 2 | 82 | 14,964,026 | 4 | unavailable |
| Total | 6 | 246 | 44,892,078 | 12 | GBV runs: 0 |

Each process compares all pixels/alpha of 41 images after GPU completion, from a
copy recorded before Present. A moving scissored orange rectangle on blue changes
content between frames. Widths 321/641/127/803 and final 639 exercise padded row
pitch and changing dimensions; padding is excluded from pixel counts. Both buffers
are presented. There is no FPS/frame-time/performance comparison claim.

Per process: eight ordinary minimize/restore cycles plus one with native loss while
minimized; 26 paused-step checks submit no frames. A separate active native loss
also recovers. Both observed native reasons are -2005270523. Three consecutive
size changes produce one GPU resize. Hide/show resumes content. Close submits no
extra frame. Teardown reports zero tracked SDK objects/resources/pending submissions
while HWND still exists. Those ownership counts are not driver memory reclamation.

The runner rejects missing/repeated images, incomplete lifecycle events, wrong mode/
VSync, stale extents, invalid row pitch, fake loss reasons, retained objects and
attempts to grant interactive acceptance. Four actual native negative controls
(unknown mode, zero frame count, mixed self-test/frame contract, existing output)
exit 1; the existing file remains byte-identical.

Registered CTests pass **6/6**, separately repeating those native contracts; their
images are not counted in the table. A normal-loop 60-frame hardware-GBV smoke also
exits cleanly, without readback or injection. It remains an automated observation.

## Regression, dependencies and preservation

[Build/run instructions](../examples/presentation/README.md) describe manual execution,
CTest and the evidence runner. A clean serial sample rebuild and raw command/native
logs are retained in [validation](../benchmarks/reports/validation/2026-10-22-phase2-presentation/).
Before/after hashes freeze consumer/shader source, runner/audit tool, executable,
SDK sources and installed headers/library.

SDK code, public headers and root CMake are unchanged. Source fingerprint:
`833978d58dc9e18daf939d12ef24eb69ca965b323caab41fd30682131baa89f1`.
Core and installed static library SHA-256 both equal
`3018622fb5a9656929e7b717a77809f0a168b4ff095e2e5392edebe98debbd8c`.

Fresh core CTest **22/22**, D3D12-OFF **4/4**, Python **110/110** (eight added tests)
and SDK/common/stress inventory checks pass. The
[presentation inventory](../licenses/generated/windows-sdk-presentation-release/inventory.json)
adds **33 compiler-read project inputs / 1 own MIT component**, installed header
origins, identical core/installed library bytes and actual linker closure. Adjacent
notices are retained. Win32/CRT/D3D12 are existing system prerequisites; no new
external download or redistributed runtime is introduced.

The current SDK ZIP and previous five SDK ZIPs are preserved and hash-checked.
No SDK ZIP is rebuilt for this example-only change. Common benchmark 72-test and
other consumer results are previous same-SDK evidence, hash-verified rather than
newly run. The installed presentation consumer itself is freshly built/run.
Earlier long stress and runtime comparisons retain their original acceptance.
[Summary/frozen-source evidence](../benchmarks/reports/2026-10-22-phase2-presentation/)
bind the new measurements and source archive without rewriting earlier reports.

## Acceptance still required

These are real programmatically manipulated Win32 windows, but readback observes
the backbuffer before presentation. It cannot certify compositor/monitor output,
tearing, smoothness, cover/uncover or human input responsiveness. No actual monitor
DPI transition occurred (`dpi_events=0`); manifest verification does not accept that
handler. A covered visible window does not use the minimized/hidden pause path;
the SDK currently exposes no occlusion status. Manual normal-loop review remains.

Controlled RemoveDevice is not unexpected driver TDR, hot-unplug or changed-adapter
selection. GBV was not reduced; the original resource-churn memory plateau was
neither rerun nor accepted. Vulkan/Linux and native Metal/macOS remain unimplemented.
