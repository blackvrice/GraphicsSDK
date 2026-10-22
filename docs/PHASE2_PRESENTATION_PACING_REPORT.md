# Phase 2 — small-window correctness and normal-loop frame pacing

2026-10-22, Windows x64 Release. The presentation consumer now handles one-pixel
client heights without an invalid scissor and caps normal-loop rendering with a
private high-resolution timer. SDK production code/API and root CMake are unchanged.
Phase 2 interactive visual, physical DPI, unexpected driver-loss and original GBV
memory acceptance remain open.

## Reproduced defect and implementation

The prior application divided client width/height by two for its scissor. A positive
one-pixel height therefore produced height zero and exited with `Scissor outside
target`. The preserved [baseline](../benchmarks/reports/validation/2026-10-22-phase2-presentation-pacing/baseline-tiny.jsonl)
uses parent commit `3d37ceb` application code with only a test stimulus inserted
before the first render. Requested 1×1 was clamped by the native nonclient frame
to 120×1, reproducing exit 1 with no successful footer. Its source and build/run
logs are retained separately from the fix.

`PresentationPolicy.hpp` now guarantees a positive, in-bounds rectangle for every
positive extent, with overflow-safe arithmetic. Zero extents remain suspended.
The CPU tests include 1×1 and UInt32 limits. Actual Win32 tests use 127×1, 129×2,
127×3 and 257×9 because the native title/frame imposes a minimum width.

The normal loop defaults to a 60fps cap, configurable with `--fps 1..240`, including
when VSync is disabled. A monotonic deadline is checked after every input wake.
Late frame starts schedule from the actual start and never accumulate catch-up
frames. Suspension waits for messages and resets the deadline when woken so a
restored active window can render immediately.

The loop uses an unnamed high-resolution waitable timer, relative 100ns deadlines
rounded upward, and a combined timer/message wait. A message wake cancels the
outstanding timer before dispatch. Timer ownership is local RAII; no timeBeginPeriod,
system setting, named global timer, driver reset or experimental feature is used.
Windows 10 1803+ timer support is required; creation/arm/wait failure exits clearly.
Reference contracts: [high-resolution timer creation](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw),
[relative timer units](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setwaitabletimer),
[combined message waits](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex).

The frame cap bounds application frame starts; it is not a display-FPS or minimum
throughput guarantee. The current flip-model swapchain does not supply an occlusion
signal for covered windows ([Microsoft status contract](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-status)).
No occlusion-detection acceptance or speculative native present-test API is added.

## Automated evidence

Final results are in [final-campaign](../benchmarks/reports/validation/2026-10-22-phase2-presentation-pacing/final-campaign/summary.json).
The six sequential fresh hardware/GBV/WARP × VSync processes each verify 49 full
images, including eight tiny-window images. Totals are **294 full images and
44,929,026 verified RGBA pixels**, 54 minimize/restore cycles, 12 controlled native
recoveries and 156 paused-step checks. Two hardware GBV runs report zero native
ERROR/CORRUPTION; validation is unavailable/null in the other modes. No validation
is reduced. The usual readback, state, coalescing, hide/show and safe-close contracts
also pass.

Two additional normal-loop hardware runs disable VSync and use 30fps/31 frames and
60fps/61 frames. Monotonic elapsed time must be at least all requested frame-start
intervals, and a real timed wait must occur. These are cap-correctness observations,
not a statistically repeated performance benchmark. A separate 1fps run receives
40 WM_NULL wake messages, then WM_CLOSE. The runner identifies only its own child
PID and `GraphicsSDKPresentation` class, excluding D3D/IME helper windows. Exactly
one frame is rendered; close must interrupt the wait and exit within 750ms.
Final observed loop times are 1.0067961s for 31 frames at the 30fps cap and
1.0208201s for 61 frames at the 60fps cap. The owned-window close completes in
0.031s. These local bounded observations are not generalized timing guarantees.

Eight actual native rejection controls cover old argument/output contracts plus
zero/high/malformed/duplicate FPS. Existing output bytes remain unchanged. A normal
60-frame hardware GBV smoke also exits with zero validation errors checked by the
application. Self-test image runs bypass pacing; their image counts are not presented
as pacing performance.

Final presentation CTest is **7/7**: six native lifecycle cases and a CPU-only policy
case with **2,278 assertions**. CPU coverage includes deadline boundaries, input wakes,
rounding, slow-frame scheduling, suspend reset, invalid FPS and tiny/large scissor
bounds. Core CTest **22/22**, D3D12-OFF **4/4**, Python **114/114** pass; four new Python
tests reject lost tiny coverage, early frame timing, missing readiness and inappropriate
acceptance claims. No actual human input or monitor/compositor observation is inferred.

## Reproduction and preservation

[Example instructions](../examples/presentation/README.md) describe normal execution,
`--fps`, registered CTests and the Python evidence runner. Actual environment, commands,
native outputs and build traces are retained in the
[validation directory](../benchmarks/reports/validation/2026-10-22-phase2-presentation-pacing/).
Final source/executable/installed SDK hashes are frozen before and after execution.
The early `campaign` used coarse millisecond message waits; it is preserved exploration
and excluded from the final-source results. The final high-resolution implementation
uses `final-campaign` and the `final-` checks/audit, without rewriting earlier data.

The [final inventory](../licenses/generated/windows-sdk-presentation-pacing-final-release/inventory.json)
covers **35 project compiler inputs / 1 own MIT component**, exact installed header/
library identity, both executable linker closures, CMake/manifest and notices. The
CPU policy executable links system prerequisites only. Kernel32/User32 remain Windows
prerequisites; no external package/download is added. SDK/common/stress checks and
the existing SDK ZIP content audit pass. Prior inventories and all six SDK ZIPs are
preserved; the SDK fingerprint remains
`833978d58dc9e18daf939d12ef24eb69ca965b323caab41fd30682131baa89f1`.

Previous presentation raw evidence/source archive remain unchanged; the current
presentation executable is rebuilt, with its old hash retained as historical metadata.
Common benchmark/other consumer results are previous same-SDK evidence, hash-verified
rather than rerun. [Summary and source archive evidence](../benchmarks/reports/2026-10-22-phase2-presentation-pacing/)
bind this change. No new SDK ZIP or performance superiority claim is introduced.
Original GBV memory failure and unperformed interactive/DPI/TDR/hot-unplug/Vulkan/
Metal acceptance remain unchanged.
